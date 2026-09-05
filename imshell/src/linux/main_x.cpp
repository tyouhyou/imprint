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
#include "shell/input_source.hpp"
#include "shell/platform_font.hpp"
#include "shell/presenter.hpp"
#include "x11_input.hpp"

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

    zb::shell::install_platform_font();
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
    const Colormap colormap =
        XCreateColormap(display, RootWindow(display, screen), vi.visual, AllocNone);
    swa.colormap = colormap;
    const Window window = XCreateWindow(
        display, RootWindow(display, screen),
        0, 0, width, height, 1,
        vi.depth, InputOutput, vi.visual,
        CWBackPixel | CWBorderPixel | CWColormap, &swa);

    // the framebuffer is fixed-size: lock the window to it so a resize
    // cannot crop the buffer with no redraw
    XSizeHints hints{};
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.max_width = width;
    hints.min_height = hints.max_height = height;
    XSetWMNormalHints(display, window, &hints);

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
    // every input event; the "painted" event asks the shell to present.
    // The "what do I blit" decision is the shared A-2 seam.
    app->on_painted([&display, &app, &window, &xi, &gc](const void *)
    {
        int x = 0, y = 0, w = 0, h = 0;
        // dirty_region fills x/y/w/h through its out-params: the call
        // MUST be its own statement. Inline in region_to_present's
        // argument list, the compiler may evaluate the later x/y/w/h
        // arguments before the first argument fills them (unspecified
        // order) -- the shell then reads 0,0 0x0 and never presents
        // (this exact bug shipped: the X11 window stayed black)
        const bool dirty = app->dirty_region(x, y, w, h);
        const zb::shell::present_rect r = zb::shell::region_to_present(
            dirty, x, y, w, h, xi->width, xi->height);
        if (r.w <= 0)
        {
            return;  // nothing was drawn, nothing to present
        }
        XPutImage(display, window, gc, xi, r.x, r.y, r.x, r.y, r.w, r.h);
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
        if (event.type == ClientMessage && event.xclient.message_type == wm_delete)
        {
            break;
        }
        // A-2 InputSource: the event -> input_event mapping (key codes,
        // characters, wheel buttons) lives in x11_input::translate,
        // dummy-driven unit-tested; the loop only feeds the app through
        // the shared seam
        zb::input::input_event ev;
        if (zb::shell::x11_input::translate(event, ev) == zb::shell::x11_input::result::handled)
        {
            zb::shell::feed_input(*app, ev);
        }
        switch (event.type)
        {
        case Expose:
        {
            if (!hasExposed)
            {
                hasExposed = true;
                app->paint();  // trigger the first paint into win->data()
            }
            else
            {
                // re-exposure (the window was un-occluded): the server
                // lost our pixels -- the framebuffer still holds the
                // last frame, put it back
                XPutImage(display, window, gc, xi, 0, 0, 0, 0, xi->width, xi->height);
                XFlush(display);
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

    // XDestroyImage frees ximage->data with Xfree; the buffer belongs to
    // the app's Graphics (delete[]) and is freed again by its destructor
    xi->data = nullptr;
    XDestroyImage(xi);
    XFreeGC(display, gc);
    XFreeColormap(display, colormap);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}