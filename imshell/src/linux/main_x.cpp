#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <vector>
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
#include "shell/presentation.hpp"
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

    // I-2a: the window is user-resizable (no size hints) -- the
    // fixed-size buffer is presented scaled-to-fit, so a resize can
    // never crop anything
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

    // I-2a presentation state: the fixed buffer mapped into the current
    // window; the window opens at buffer size, so the mapping starts 1:1
    // (zero-copy). A resized window rebuilds the mapping and the
    // dest-sized scratch image the manual resample loop writes into
    const int buf_w = win->width();
    const int buf_h = win->height();
    zb::shell::presentation pres = zb::shell::presentation_fit(width, height, buf_w, buf_h);
    std::vector<zb::ui::core::Color> scratch;
    XImage *sx = nullptr;  // the scratch image (data owned by `scratch`)

    auto is_one_to_one = [&pres, buf_w, buf_h]()
    {
        return pres.x == 0 && pres.y == 0 && pres.w == buf_w && pres.h == buf_h;
    };

    // presents one buffer-space region: 1:1 blits the app buffer
    // directly, a scaled window resamples the region's dest footprint
    // into the scratch image first
    auto present_region = [&](const int x, const int y, const int w, const int h)
    {
        if (is_one_to_one())
        {
            XPutImage(display, window, gc, xi, x, y, x, y, w, h);
            XFlush(display);
            return;
        }
        const zb::shell::present_rect d = zb::shell::presentation_region(
            pres, zb::shell::present_rect{x, y, w, h});
        if (d.w <= 0 || d.h <= 0 || sx == nullptr)
        {
            return;
        }
        zb::shell::resample_presentation(pres, d, win->data(), sx->data);
        XPutImage(display, window, gc, sx, d.x, d.y, d.x, d.y, d.w, d.h);
        XFlush(display);
    };

    // a new client size: rebuild the mapping and the scratch, clear the
    // (grown or shrunk) letterbox, re-present everything
    auto on_resize = [&](const int win_w, const int win_h)
    {
        if (win_w <= 0 || win_h <= 0 || (win_w == pres.w && win_h == pres.h))
        {
            return;  // moves also send ConfigureNotify -- only a size change rebuilds
        }
        pres = zb::shell::presentation_fit(win_w, win_h, buf_w, buf_h);
        if (is_one_to_one())
        {
            if (sx != nullptr)
            {
                sx->data = nullptr;  // the bytes belong to `scratch`
                XDestroyImage(sx);
                sx = nullptr;
            }
            scratch.clear();
            scratch.shrink_to_fit();
        }
        else
        {
            scratch.assign(static_cast<size_t>(pres.w) * pres.h, {});
            if (sx != nullptr)
            {
                sx->data = nullptr;
                XDestroyImage(sx);
            }
            sx = XCreateImage(display, vi.visual, vi.depth, ZPixmap, 0,
                              reinterpret_cast<char *>(scratch.data()),
                              pres.w, pres.h, 32, 0);
            if (sx == nullptr)
            {
                LE << "XCreateImage failed for the scaled presentation";
                return;
            }
            zb::shell::resample_presentation(
                pres, zb::shell::present_rect{0, 0, pres.w, pres.h},
                win->data(), scratch.data());
        }
        XClearWindow(display, window);  // the letterbox shows the black background
        present_region(0, 0, buf_w, buf_h);
    };

    // rendering loop protocol (see IApp): this shell is event-driven --
    // paint() is requested on the first Expose, and the app repaints after
    // every input event; the "painted" event asks the shell to present.
    // The "what do I blit" decision is the shared A-2 seam.
    app->on_painted([&](const void *)
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
            dirty, x, y, w, h, buf_w, buf_h);
        if (r.w <= 0)
        {
            return;  // nothing was drawn, nothing to present
        }
        present_region(r.x, r.y, r.w, r.h);
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
        // dummy-driven unit-tested; the loop only maps the point into
        // the buffer (I-2a) and feeds the app through the shared seam
        zb::input::input_event ev;
        if (zb::shell::x11_input::translate(event, ev) == zb::shell::x11_input::result::handled)
        {
            // I-2a: window pixels -> buffer pixels; a pointer event on
            // the letterbox is not app input and is dropped (keyboard
            // events carry no position and always pass)
            int bx = ev.x;
            int by = ev.y;
            if (!zb::shell::maps_pointer(ev.type) ||
                pres.to_buffer(ev.x, ev.y, bx, by))
            {
                ev.x = bx;
                ev.y = by;
                zb::shell::feed_input(*app, ev);
            }
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
                present_region(0, 0, buf_w, buf_h);
            }
            break;
        }
        case ConfigureNotify:
        {
            on_resize(event.xconfigure.width, event.xconfigure.height);
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
    // the app's Graphics (delete[]) and is freed again by its destructor.
    // The scratch image's bytes belong to `scratch` the same way.
    xi->data = nullptr;
    XDestroyImage(xi);
    if (sx != nullptr)
    {
        sx->data = nullptr;
        XDestroyImage(sx);
    }
    XFreeGC(display, gc);
    XFreeColormap(display, colormap);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}