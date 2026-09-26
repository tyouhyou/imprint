// Linux framebuffer host loop, extracted from main_fb.cpp (code-contract
// §11): the framework-mode main is a thin wrapper over run(); a library
// consumer calls run() from their own main. Idle-poll loop, no input
// source (B2), and the shell-owned 320x240 panel size are unchanged.
#include <unistd.h>
#include "core/error.hpp"
#include "linux/fb.hpp"
#include "logging.hpp"
#include "shell/platform_font.hpp"
#include "shell/presenter.hpp"
#include "shell/run.hpp"

namespace zb::shell
{
    int run(zb::SharedPtr<zb::app::IApp> app, const run_options &options)
    {
        FB fb;
        if (!fb.ok())
        {
            // headless host or missing /dev/fb0: fail instead of spinning a
            // loop that can never present a frame
            throw zb::ui::error("FB shell: framebuffer unavailable (headless host or missing /dev/fb0)");
        }
        zb::shell::install_platform_font();
        if (app->window() == nullptr)
        {
            // the FB shell's own panel size: screen geometry is shell-owned
            // (ARCHITECTURE §3.1); run_options may only constrain it
            const uint32_t w = options.width != 0 ? options.width : 320;
            const uint32_t h = options.height != 0 ? options.height : 240;
            app->create_window(w, h);
        }

        const auto window = app->window();

        // the app requests to quit by closing its window (e.g. a QUIT button)
        bool app_closed = false;
        app->on_closed([&app_closed]() { app_closed = true; });

        // idle-poll loop: paint (and present via the painted callback) only
        // when the app owns a frame. The FB backend has no input source at
        // all (no keyboard, no pointer); use the X11 backend for input (B2).
        // The "what do I blit" decision is the shared A-2 seam.
        app->on_painted([&fb, &app, &window](const void *)
        {
            int x = 0, y = 0, w = 0, h = 0;
            // own statement: see the evaluation-order note in run_x11.cpp
            const bool dirty = app->dirty_region(x, y, w, h);
            const zb::shell::present_rect r = zb::shell::region_to_present(
                dirty, x, y, w, h, window->width(), window->height());
            if (r.w <= 0)
            {
                return;  // nothing was drawn, nothing to present
            }
            fb.draw(static_cast<char *>(window->data()), window->width(), window->height(), r.x, r.y, r.w, r.h);
        });

        while (!app_closed)
        {
            if (app->is_dirty())
            {
                app->paint();
            }
            usleep(30000); // ~33 fps
        }

        return 0;
    }
}
