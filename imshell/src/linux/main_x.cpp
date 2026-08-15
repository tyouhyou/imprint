#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include "imcore.hpp"
#include "app_maker.hpp"
#include "logging.hpp"
#include "input.hpp"

using namespace zb::app;

int start();

int main(int argc, char *argv[])
{
    zb::Logging::set_log_handle([](const zb::Logging_Level &level, const std::string &message)
    {
        std::cerr << message;
    });

    LD << "run on X";
    return start();
}

int start()
{
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        LE << "Cannot open display";
        return 1;
    }

    // flush out any errors in DEBUG builds
#ifdef DEBUG
    XSynchronize(display, True);
#endif

    const auto app = make_app();
    app->create_window();
    const auto win = app->window();

    const int width = win->width();
    const int height = win->height();
    const int screen = DefaultScreen(display);

    // the framework framebuffer is 32bpp, find a matching TrueColor visual
    XVisualInfo vi{};
    if (!XMatchVisualInfo(display, screen, 32, TrueColor, &vi))
    {
        LE << "No 32-bit TrueColor visual found on this display";
        XCloseDisplay(display);
        return 2;
    }
    LI << "selected 32-bit visual: " << vi.depth;

    XSetWindowAttributes swa{};
    swa.background_pixel = BlackPixel(display, screen);
    swa.border_pixel = WhitePixel(display, screen);
    // a non-default visual (e.g. a 32bpp visual on a 24bpp root, which is
    // the common case on desktop Xorg) requires an explicit colormap,
    // otherwise XCreateWindow fails with BadMatch
    swa.colormap = XCreateColormap(display, RootWindow(display, screen), vi.visual, AllocNone);
    const Window window = XCreateWindow(
        display, RootWindow(display, screen),
        0, 0, width, height, 1,
        vi.depth, InputOutput, vi.visual,
        CWBackPixel | CWBorderPixel | CWColormap, &swa);

    XStoreName(display, window, win->title().c_str());
    const Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, const_cast<Atom *>(&wm_delete), 1);

    XSelectInput(display, window,
                 ExposureMask | StructureNotifyMask | KeyPressMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
    XMapWindow(display, window);

    GC gc = XCreateGC(display, window, 0, nullptr);

    // XImage directly maps the framework framebuffer (BGRA little-endian == X 32bit TrueColor on x86)
    XImage *xi = XCreateImage(
        display, vi.visual, vi.depth, ZPixmap, 0,
        static_cast<char *>(win->data()),
        width, height, 32, 0);
    if (xi == nullptr)
    {
        LE << "XCreateImage failed";
        XCloseDisplay(display);
        return 3;
    }

    // rendering loop protocol (see IApp): this shell is event-driven --
    // paint() is requested on the first Expose, and the app repaints after
    // every input event; the "painted" event asks the shell to present
    app->on_painted([&display, &window, &xi, &gc](const void *)
    {
        XPutImage(display, window, gc, xi, 0, 0, 0, 0, xi->width, xi->height);
        XFlush(display);
    });

    auto hasExposed = false;

    // the app requests to quit by closing its window (e.g. a QUIT button)
    bool app_closed = false;
    app->on_closed([&app_closed]() { app_closed = true; });

    XEvent event{};
    while (!app_closed)
    {
        XNextEvent(display, &event);
        switch (event.type)
        {
        case KeyPress:
        {
            // navigation/editing keys -> key field; printable ASCII ->
            // ch field (chars are routed to the focused widget; see
            // dispatcher B1)
            const KeySym ks = XLookupKeysym(&event.xkey, 0);
            zb::input::input_event ev;
            ev.type = zb::input::input_type::key_down;
            switch (ks)
            {
            case XK_Return:
                ev.key = static_cast<int>(zb::input::key_code::enter);
                break;
            case XK_Tab:
                ev.key = static_cast<int>(zb::input::key_code::tab);
                break;
            case XK_Escape:
                ev.key = static_cast<int>(zb::input::key_code::escape);
                break;
            case XK_space:
                ev.key = static_cast<int>(zb::input::key_code::space);
                break;
            case XK_BackSpace:
                ev.key = static_cast<int>(zb::input::key_code::backspace);
                break;
            case XK_Delete:
                ev.key = static_cast<int>(zb::input::key_code::del);
                break;
            case XK_Up:
                ev.key = static_cast<int>(zb::input::key_code::up);
                break;
            case XK_Down:
                ev.key = static_cast<int>(zb::input::key_code::down);
                break;
            case XK_Left:
                ev.key = static_cast<int>(zb::input::key_code::left);
                break;
            case XK_Right:
                ev.key = static_cast<int>(zb::input::key_code::right);
                break;
            default:
                break;
            }
            // printable character (latin-1 from XLookupString; only
            // single-byte, handles shift via the modifier state). Keys
            // that produced a key field keep their key semantics (space
            // is the navigation-activation key, not a character)
            if (ev.key == 0)
            {
                char buf[4];
                if (const int len = XLookupString(&event.xkey, buf, sizeof(buf), nullptr, nullptr);
                    len == 1 && static_cast<unsigned char>(buf[0]) >= 0x20 &&
                    static_cast<unsigned char>(buf[0]) <= 0x7e)
                {
                    ev.ch = buf[0];
                }
            }
            if (ev.key != 0 || ev.ch != 0)
            {
                app->input(ev);
            }
            break;
        }
        case ClientMessage:
        {
            if (event.xclient.message_type == wm_delete)
            {
                goto endwhile;
            }
            break;
        }
        case ButtonPress:
        {
            if (auto *bp = reinterpret_cast<XButtonEvent *>(&event); bp != nullptr)
            {
                if (bp->button == 1)
                {
                    zb::input::input_event ev;
                    ev.type = zb::input::input_type::mouse_left_down;
                    ev.touch_id = 0;
                    ev.button = zb::input::mouse_button_t::left;
                    ev.x = bp->x;
                    ev.y = bp->y;
                    app->input(ev);
                }
            }
            break;
        }
        case ButtonRelease:
        {
            if (auto *br = reinterpret_cast<XButtonEvent *>(&event); br != nullptr)
            {
                if (br->button == 1) // Left mouse button
                {
                    zb::input::input_event ev;
                    ev.type = zb::input::input_type::mouse_left_up;
                    ev.touch_id = 0;
                    ev.button = zb::input::mouse_button_t::left;
                    ev.x = br->x;
                    ev.y = br->y;
                    app->input(ev);
                }
                if (br->button == 3) // Right mouse button
                {
                    zb::input::input_event ev;
                    ev.type = zb::input::input_type::mouse_right_up;
                    ev.touch_id = 0;
                    ev.button = zb::input::mouse_button_t::right;
                    ev.x = br->x;
                    ev.y = br->y;
                    app->input(ev);
                }
            }
            break;
        }
        case MotionNotify:
        {
            if (auto *mm = reinterpret_cast<XMotionEvent *>(&event); mm != nullptr)
            {
                zb::input::input_event ev;
                ev.type = zb::input::input_type::mouse_move;
                ev.x = mm->x;
                ev.y = mm->y;
                ev.touch_id = 0;
                app->input(ev);
            }
            break;
        }
        case Expose:
        {
            if (!hasExposed)
            {
                hasExposed = true;
                app->paint(); // trigger the first paint into win->data()
            }
            break;
        }
        case DestroyNotify:
        {
            LD << "window destroyed";
            break;
        }
        default:
            break;
        }
        if (app_closed)
        {
            break;  // the app closed the window during event dispatch
        }
        // XNextEvent already blocks while no event is pending, so a sleep
        // here only delays input/paint response by 30ms per event
    }
endwhile:

    XDestroyImage(xi);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}