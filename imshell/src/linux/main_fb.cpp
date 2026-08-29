#include <cstdio>
#include <unistd.h>
#include "app_maker.hpp"
#include "linux/fb.hpp"
#include "logging.hpp"
#include "shell/presenter.hpp"

using namespace zb::app;

int main(int argc, char *argv[])
{
    zb::Logging::set_log_handle([](const zb::Logging_Level &level, const std::string &message)
    {
        std::printf("%s", message.c_str());
    });

    LI << "run on FB";

    FB fb;
    if (!fb.ok())
    {
        // headless host or missing /dev/fb0: fail instead of spinning a
        // loop that can never present a frame
        LE << "framebuffer unavailable, exiting";
        return 1;
    }
    auto app = make_app();
    app->create_window(320, 240);

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
        const zb::shell::present_rect r = zb::shell::region_to_present(
            app->dirty_region(x, y, w, h), x, y, w, h, window->width(), window->height());
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
