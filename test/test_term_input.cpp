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
        // U-1: letters carry the platform-independent key code alongside
        // ch (the terminal protocol itself has no key-up)
        EXPECT(ev[0].ch == 'a' && ev[0].key == 'a');

        ev = feed(p, "\r\t\x7f\x03");
        EXPECT(ev.size() == 4);
        EXPECT(ev[0].key == static_cast<int>(key_code::enter));
        EXPECT(ev[1].key == static_cast<int>(key_code::tab));
        EXPECT(ev[2].key == static_cast<int>(key_code::backspace));
        EXPECT(ev[3].key == static_cast<int>(key_code::escape));  // Ctrl-C

        // space is a navigation key (§4.2), never a character -- the same
        // routing as the desktop shells, so it activates the focused widget
        ev = feed(p, " ");
        EXPECT(ev.size() == 1 && ev[0].key == static_cast<int>(key_code::space) &&
               ev[0].ch == 0);

        // U-1 decoration through the shared helper: lowercase key code for
        // letters, verbatim digits, punctuation ch-only
        ev = feed(p, "Z5!");
        EXPECT(ev.size() == 3);
        EXPECT(ev[0].key == 'z' && ev[0].ch == 'Z');
        EXPECT(ev[1].key == '5' && ev[1].ch == '5');
        EXPECT(ev[2].key == 0 && ev[2].ch == '!');
    }

    // arrows in CSI and SS3 forms; a sequence split across chunks
    {
        zb::shell::term_input::parser p(320, 240);
        auto ev = feed(p, "\x1b[A\x1b[B\x1b[C\x1b[D");
        EXPECT(ev.size() == 4);
        EXPECT(ev[0].key == static_cast<int>(key_code::up));
        EXPECT(ev[1].key == static_cast<int>(key_code::down));
        EXPECT(ev[2].key == static_cast<int>(key_code::right));
        EXPECT(ev[3].key == static_cast<int>(key_code::left));

        ev = feed(p, "\x1bOB");
        EXPECT(ev.size() == 1 && ev[0].key == static_cast<int>(key_code::down));

        // streaming: the escape arrives before its parameters
        zb::shell::term_input::parser q(320, 240);
        EXPECT(feed(q, "\x1b").empty());
        EXPECT(feed(q, "[").empty());
        ev = feed(q, "A");
        EXPECT(ev.size() == 1 && ev[0].key == static_cast<int>(key_code::up));
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

    // X-12: CSI accumulation is bounded at 64 parameter/intermediate
    // bytes. A longer sequence is swallowed through its final byte and
    // the parser returns to ground, so hostile stdin cannot grow state
    // and the *next* sequence still maps.
    {
        // the bound itself is accepted: 64 param bytes + final still maps
        zb::shell::term_input::parser p(320, 240);
        auto ev = feed(p, "\x1b[" + std::string(64, '1') + "A");
        EXPECT(ev.size() == 1 && ev[0].key == static_cast<int>(key_code::up));

        // one byte past it: the whole sequence is dropped, no event
        ev = feed(p, "\x1b[" + std::string(65, '1') + "A");
        EXPECT(ev.empty());

        // ... and the parser recovered: a normal sequence right after maps
        ev = feed(p, "\x1b[B");
        EXPECT(ev.size() == 1 && ev[0].key == static_cast<int>(key_code::down));

        // ground is unaffected by the discard: a plain letter still works
        ev = feed(p, "q");
        EXPECT(ev.size() == 1 && ev[0].ch == 'q');
    }

    // X-12: an over-long sequence split across feed() calls is still
    // discarded, and reset() clears an in-progress discard (the shell
    // calls it on teardown/re-init)
    {
        zb::shell::term_input::parser p(320, 240);
        EXPECT(feed(p, "\x1b[" + std::string(40, '1')).empty());
        // the overflow lands in the second chunk, then the final byte
        EXPECT(feed(p, std::string(40, '1') + "A").empty());
        auto ev = feed(p, "\x1b[C");
        EXPECT(ev.size() == 1 && ev[0].key == static_cast<int>(key_code::right));

        // mid-discard reset returns to ground without waiting for a final
        // byte: the next byte parses as ordinary ground input
        EXPECT(feed(p, "\x1b[" + std::string(80, '1')).empty());
        p.reset();
        ev = feed(p, "z");
        EXPECT(ev.size() == 1 && ev[0].ch == 'z');
        // and a following escape sequence starts clean (no stale csi_)
        ev = feed(p, "\x1b[A");
        EXPECT(ev.size() == 1 && ev[0].key == static_cast<int>(key_code::up));
    }

    return test::report("term_input");
}
