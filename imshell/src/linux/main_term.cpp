// Terminal (SIXEL) shell main (P4 demo target, ARCHITECTURE §3).
// Structure mirrors main_fb through the A-2 seams: the app owns the
// buffer, this shell polls stdin (raw mode), maps bytes through the
// pure term_input parser (SGR mouse + keys), and presents — a full
// sixel frame at the cursor home per painted event (no dirty regions
// in the demo target). The presenter is shell/sixel.cpp, the input
// translator shell/term_input.cpp; both are unit-tested (test_sixel /
// test_term_input), this main is only fd glue.
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "app_maker.hpp"
#include "logging.hpp"
#include "shell/input_source.hpp"
#include "shell/platform_font.hpp"
#include "shell/sixel.hpp"
#include "shell/term_input.hpp"

using namespace zb::app;

namespace
{
    // the demo buffer size (the FB shell's precedent: screen geometry
    // is shell-owned); IM_TERM_SIZE=WxH overrides
    void resolve_buffer_size(int &w, int &h)
    {
        w = 320;
        h = 240;
        if (const char *env = std::getenv("IM_TERM_SIZE"))
        {
            if (std::sscanf(env, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0)
            {
                w = 320;
                h = 240;
            }
        }
    }

    // raw mode + SGR mouse reporting + cell-size query + hidden cursor
    class terminal_session
    {
    public:
        ~terminal_session() { restore(); }

        bool begin()
        {
            if (tcgetattr(STDIN_FILENO, &saved_) != 0)
            {
                return false;
            }
            termios raw = saved_;
            cfmakeraw(&raw);
            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
            {
                return false;
            }
            // mouse press/release/motion + SGR encoding, cell-size query
            std::fputs("\x1b[?1000;1002;1006h\x1b[16t\x1b[?25l", stdout);
            std::fflush(stdout);
            return true;
        }

        void restore()
        {
            if (!restored_)
            {
                restored_ = true;
                std::fputs("\x1b[?25h\x1b[?1000;1002;1006l", stdout);
                std::fflush(stdout);
                tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
            }
        }

    private:
        termios saved_{};
        bool restored_ = false;
    };
}

int main(int argc, char *argv[])
{
    zb::Logging::set_log_handle([](const zb::Logging_Level &level, const std::string &message)
    {
        std::printf("%s", message.c_str());
    });

    LD << "run on terminal (SIXEL)";

    int buffer_w = 0;
    int buffer_h = 0;
    resolve_buffer_size(buffer_w, buffer_h);

    terminal_session session;
    if (!session.begin())
    {
        // headless host or no tty: fail instead of a loop that can
        // never receive input
        LE << "terminal unavailable (stdin is not a tty), exiting";
        return 1;
    }

    zb::shell::install_platform_font();
    auto app = make_app();
    app->create_window(static_cast<uint32_t>(buffer_w),
                       static_cast<uint32_t>(buffer_h));
    const auto window = app->window();

    // the app requests to quit by closing its window (e.g. a QUIT button)
    bool app_closed = false;
    app->on_closed([&app_closed]() { app_closed = true; });

    // painted callback: the frame is owed, present at the next loop tick
    bool frame_owed = false;
    app->on_painted([&frame_owed](const void *) { frame_owed = true; });

    zb::shell::term_input::parser parser(buffer_w, buffer_h);
    std::vector<zb::input::input_event> events;

    // the first frame goes out immediately, like the FB shell
    app->paint();
    std::fputs("\x1b[H", stdout);
    zb::shell::sixel::write_frame(window->data(), buffer_w, buffer_h);

    while (!app_closed)
    {
        // 30ms poll: input arrives when it arrives, the frame presents
        // when the painted callback says one is owed
        pollfd fds{STDIN_FILENO, POLLIN, 0};
        if (poll(&fds, 1, 30) > 0)
        {
            char chunk[256];
            const ssize_t n = ::read(STDIN_FILENO, chunk, sizeof(chunk));
            if (n <= 0)
            {
                break;  // stdin closed
            }
            events.clear();
            parser.feed(chunk, static_cast<std::size_t>(n), events);
            // the parser maps into buffer pixels already; the events go
            // through the shared feed_input seam (A-2)
            for (const zb::input::input_event &ev : events)
            {
                if (app_closed)
                {
                    break;
                }
                zb::shell::feed_input(*app, ev);
            }
        }
        if (frame_owed)
        {
            frame_owed = false;
            std::fputs("\x1b[H", stdout);  // cursor home, overwrite
            zb::shell::sixel::write_frame(window->data(), buffer_w, buffer_h);
        }
    }

    session.restore();
    return 0;
}
