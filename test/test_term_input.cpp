// Terminal InputSource tests (P4 demo target): the pure parser's keys,
// SGR mouse mapping (cells -> buffer pixels via the cell-size reply),
// streaming across chunk boundaries, and the letterbox drop rule.
#include "test.hpp"

#include <string>
#include <vector>

#include "shell/term_input.hpp"

namespace
{
    std::vector<zb::input::input_event> feed(zb::shell::term_input::parser &p,
                                             const std::string &bytes)
    {
        std::vector<zb::input::input_event> out;
        p.feed(bytes.data(), bytes.size(), out);
        return out;
    }
}

int test_term_input()
{
    using zb::input::input_event;
    using zb::input::input_type;
    using zb::input::key_code;

    // single-byte keys
    {
        zb::shell::term_input::parser p(320, 240);
        auto ev = feed(p, "a");
        EXPECT(ev.size() == 1 && ev[0].type == input_type::key_down);
        EXPECT(ev[0].ch == 'a' && ev[0].key == 0);

        ev = feed(p, "\r\t\x7f\x03");
        EXPECT(ev.size() == 4);
        EXPECT(ev[0].key == (int)key_code::enter);
        EXPECT(ev[1].key == (int)key_code::tab);
        EXPECT(ev[2].key == (int)key_code::backspace);
        EXPECT(ev[3].key == (int)key_code::escape);  // Ctrl-C
    }

    // arrows in CSI and SS3 forms; a sequence split across chunks
    {
        zb::shell::term_input::parser p(320, 240);
        auto ev = feed(p, "\x1b[A\x1b[B\x1b[C\x1b[D");
        EXPECT(ev.size() == 4);
        EXPECT(ev[0].key == (int)key_code::up);
        EXPECT(ev[1].key == (int)key_code::down);
        EXPECT(ev[2].key == (int)key_code::right);
        EXPECT(ev[3].key == (int)key_code::left);

        ev = feed(p, "\x1bOB");
        EXPECT(ev.size() == 1 && ev[0].key == (int)key_code::down);

        // streaming: the escape arrives before its parameters
        zb::shell::term_input::parser q(320, 240);
        EXPECT(feed(q, "\x1b").empty());
        EXPECT(feed(q, "[").empty());
        ev = feed(q, "A");
        EXPECT(ev.size() == 1 && ev[0].key == (int)key_code::up);
    }

    // SGR mouse with the 10x20 fallback geometry
    {
        zb::shell::term_input::parser p(320, 240);
        auto ev = feed(p, "\x1b[<0;3;2M");
        EXPECT(ev.size() == 1 && ev[0].type == input_type::mouse_left_down);
        EXPECT(ev[0].x == 25 && ev[0].y == 30);  // (3-1)*10+5, (2-1)*20+10
        EXPECT(ev[0].touch_id == 0);

        ev = feed(p, "\x1b[<0;3;2m");
        EXPECT(ev.size() == 1 && ev[0].type == input_type::mouse_left_up);

        ev = feed(p, "\x1b[<2;1;1M");
        EXPECT(ev.size() == 1 && ev[0].type == input_type::mouse_right_down);

        ev = feed(p, "\x1b[<32;4;1M");
        EXPECT(ev.size() == 1 && ev[0].type == input_type::mouse_move);

        ev = feed(p, "\x1b[<64;1;1M\x1b[<65;1;1M");
        EXPECT(ev.size() == 2 && ev[0].type == input_type::mouse_wheel);
        EXPECT(ev[0].delta == 1 && ev[1].delta == -1);
    }

    // cell-size reply rescales the pointer map; outside the buffer drops
    {
        zb::shell::term_input::parser p(320, 240);
        EXPECT(feed(p, "\x1b[6;16;8t").empty());  // 8x16 cells
        auto ev = feed(p, "\x1b[<0;3;2M");
        EXPECT(ev.size() == 1 && ev[0].x == 20 && ev[0].y == 24);

        // cell (45, 2) maps to x = (45-1)*8+4 = 356 beyond 320: dropped
        ev = feed(p, "\x1b[<0;45;2M");
        EXPECT(ev.empty());
    }

    return test::report("term_input");
}
