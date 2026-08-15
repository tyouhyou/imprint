#include <cstdio>
#include <unistd.h>
#include "app_maker.hpp"
#include "linux/fb.hpp"
#include "logging.hpp"

using namespace zb::app;

int main(int argc, char *argv[])
{
    zb::Logging::set_log_handle([](const zb::Logging_Level &level, const std::string &message)
    {
        std::printf("%s", message.c_str());
    });

    LI << "run on FB";

    FB fb;
    auto app = make_app();
    app->create_window(320, 240);

    const auto window = app->window();

    // the app requests to quit by closing its window (e.g. a QUIT button)
    bool app_closed = false;
    app->on_closed([&app_closed]() { app_closed = true; });

    // idle-poll loop: paint (and present via the painted callback) only
    // when the app owns a frame. The FB backend has no input source at
    // all (no keyboard, no pointer); use the X11 backend for input (B2).
    app->on_painted([&fb, &window](const void *)
    {
        fb.draw(static_cast<char *>(window->data()), window->width(), window->height());
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
