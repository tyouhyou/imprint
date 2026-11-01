#include "test.hpp"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "x11_input.hpp"

/*
 * A-2 InputSource, x11 half, dummy-driven: synthetic X events (no server,
 * no Display) must translate into the exact input_event stream the
 * dispatcher contract promises. The KeySym->key_code table is tested
 * directly. The event-level KeyPress path is NOT driven here: with no
 * Display, libX11's internal keyboard mapping is uninitialized and the
 * keycode range check inside XLookupKeysym crashes (real shells always
 * see server-initialized mappings); the button, wheel and motion paths
 * read event fields only and are fully covered. The IME committed-text
 * relay (commit_events) is pure and driven directly. Only built where
 * the X11 backend builds.
 */

using zb::shell::x11_input::result;
using zb::shell::x11_input::translate;

int test_x11_input()
{
    // the centralized KeySym table
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_Return) == static_cast<int>(zb::input::key_code::enter));
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_Tab) == static_cast<int>(zb::input::key_code::tab));
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_Escape) == static_cast<int>(zb::input::key_code::escape));
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_space) == static_cast<int>(zb::input::key_code::space));
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_BackSpace) == static_cast<int>(zb::input::key_code::backspace));
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_Delete) == static_cast<int>(zb::input::key_code::del));
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_Up) == static_cast<int>(zb::input::key_code::up));
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_Down) == static_cast<int>(zb::input::key_code::down));
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_Left) == static_cast<int>(zb::input::key_code::left));
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_Right) == static_cast<int>(zb::input::key_code::right));
    // U-1: letters and digits are platform-independent key codes
    // (lowercase letters, verbatim digits); shifted letters arrive as
    // the uppercase keysym and normalize to lowercase
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_w) == 'w');
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_W) == 'w');
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_5) == '5');
    EXPECT(zb::shell::x11_input::key_from_keysym(XK_Home) == 0);  // unmapped

    // buttons: left press/release carry the button and position
    {
        XEvent e{};
        e.type = ButtonPress;
        e.xbutton.button = 1;
        e.xbutton.x = 7;
        e.xbutton.y = 9;
        zb::input::input_event ev;
        EXPECT(translate(e, ev) == result::handled);
        EXPECT(ev.type == zb::input::input_type::mouse_left_down);
        EXPECT(ev.button == zb::input::mouse_button_t::left);
        EXPECT(ev.x == 7 && ev.y == 9);
        EXPECT(ev.touch_id == 0);

        e.type = ButtonRelease;
        EXPECT(translate(e, ev) == result::handled);
        EXPECT(ev.type == zb::input::input_type::mouse_left_up);
    }

    // buttons: right press/release map symmetrically
    {
        XEvent e{};
        e.type = ButtonPress;
        e.xbutton.button = 3;
        e.xbutton.x = 12;
        e.xbutton.y = 34;
        zb::input::input_event ev;
        EXPECT(translate(e, ev) == result::handled);
        EXPECT(ev.type == zb::input::input_type::mouse_right_down);
        EXPECT(ev.button == zb::input::mouse_button_t::right);
        EXPECT(ev.x == 12 && ev.y == 34);

        e.type = ButtonRelease;
        EXPECT(translate(e, ev) == result::handled);
        EXPECT(ev.type == zb::input::input_type::mouse_right_up);
    }

    // the wheel arrives as button 4/5 presses: signed notches
    {
        XEvent e{};
        e.type = ButtonPress;
        e.xbutton.button = 4;
        e.xbutton.x = 2;
        e.xbutton.y = 3;
        zb::input::input_event ev;
        EXPECT(translate(e, ev) == result::handled);
        EXPECT(ev.type == zb::input::input_type::mouse_wheel);
        EXPECT(ev.delta == 1);
        EXPECT(ev.x == 2 && ev.y == 3);

        e.xbutton.button = 5;
        EXPECT(translate(e, ev) == result::handled);
        EXPECT(ev.delta == -1);
    }

    // button 2 (middle) is deliberately dropped
    {
        XEvent e{};
        e.type = ButtonPress;
        e.xbutton.button = 2;
        zb::input::input_event ev;
        EXPECT(translate(e, ev) == result::swallowed);
        e.type = ButtonRelease;
        EXPECT(translate(e, ev) == result::swallowed);
    }

    // motion carries the pointer position
    {
        XEvent e{};
        e.type = MotionNotify;
        e.xmotion.x = 40;
        e.xmotion.y = 50;
        zb::input::input_event ev;
        EXPECT(translate(e, ev) == result::handled);
        EXPECT(ev.type == zb::input::input_type::mouse_move);
        EXPECT(ev.x == 40 && ev.y == 50);
    }

    // non-input events stay with the shell's event loop
    {
        XEvent e{};
        e.type = Expose;
        zb::input::input_event ev;
        EXPECT(translate(e, ev) == result::not_handled);
        e.type = ClientMessage;
        EXPECT(translate(e, ev) == result::not_handled);
    }

    // FI-01 committed-text relay: one IME commit -> 0..n key_downs
    using zb::shell::x11_input::commit_events;

    // printable ASCII relay: a single code point is exactly the legacy
    // decorate_printable_key shape (key+ch in one event)
    {
        zb::input::input_event ev[4];
        EXPECT(commit_events(NoSymbol, "a", 1, ev, 4) == 1);
        EXPECT(ev[0].type == zb::input::input_type::key_down);
        zb::input::input_event legacy{};
        legacy.type = zb::input::input_type::key_down;
        EXPECT(zb::input::decorate_printable_key(legacy, 'a'));
        EXPECT(ev[0].key == legacy.key && ev[0].ch == legacy.ch);

        EXPECT(commit_events(NoSymbol, "W", 1, ev, 4) == 1);
        EXPECT(ev[0].ch == 'W' && ev[0].key == 'w');

        // punctuation carries ch without a key (layout-bound), same as
        // the legacy path
        EXPECT(commit_events(NoSymbol, "!", 1, ev, 4) == 1);
        EXPECT(ev[0].ch == '!' && ev[0].key == 0);
    }

    // CJK text: one key_down per code point, ch = code point, key = 0
    // (the T-2 / term_input precedent)
    {
        zb::input::input_event ev[4];
        // U+4E2D U+6587, six UTF-8 bytes
        EXPECT(commit_events(NoSymbol, "\xe4\xb8\xad\xe6\x96\x87", 6, ev, 4) == 2);
        EXPECT(ev[0].type == zb::input::input_type::key_down);
        EXPECT(ev[0].ch == 0x4E2D && ev[0].key == 0);
        EXPECT(ev[1].ch == 0x6587 && ev[1].key == 0);
    }

    // non-BMP code points relay the same way
    {
        zb::input::input_event ev[4];
        // U+1F600, four UTF-8 bytes
        EXPECT(commit_events(NoSymbol, "\xf0\x9f\x98\x80", 4, ev, 4) == 1);
        EXPECT(ev[0].ch == 0x1F600 && ev[0].key == 0);
    }

    // space is a navigation key, never a character (B1): a pure space
    // commit produces only the keysym's space key event
    {
        zb::input::input_event ev[4];
        EXPECT(commit_events(NoSymbol, " ", 1, ev, 4) == 0);
        EXPECT(commit_events(XK_space, " ", 1, ev, 4) == 1);
        EXPECT(ev[0].type == zb::input::input_type::key_down);
        EXPECT(ev[0].key == static_cast<int>(zb::input::key_code::space));
        EXPECT(ev[0].ch == 0);
    }

    // navigation keys stay reachable through the input context: the
    // keysym appends one ch=0 key event; XLookupBoth produces both halves
    {
        zb::input::input_event ev[4];
        // Enter: the committed "\n" is a control character and drops
        EXPECT(commit_events(XK_Return, "\n", 1, ev, 4) == 1);
        EXPECT(ev[0].key == static_cast<int>(zb::input::key_code::enter));
        EXPECT(ev[0].ch == 0);

        // a letter as XLookupBoth: text event first, keysym event (ch = 0)
        // appended after it
        EXPECT(commit_events(XK_a, "a", 1, ev, 4) == 2);
        EXPECT(ev[0].ch == 'a' && ev[0].key == 'a');
        EXPECT(ev[1].key == 'a' && ev[1].ch == 0);

        // an unmapped keysym adds nothing
        EXPECT(commit_events(XK_Home, "x", 1, ev, 4) == 1);
        EXPECT(ev[0].ch == 'x');

        // keysym-only commit (XLookupKeySymVal shape)
        EXPECT(commit_events(XK_Tab, nullptr, 0, ev, 4) == 1);
        EXPECT(ev[0].key == static_cast<int>(zb::input::key_code::tab));
    }

    // invalid UTF-8 and control characters produce nothing
    {
        zb::input::input_event ev[4];
        EXPECT(commit_events(NoSymbol, "\x80", 1, ev, 4) == 0);          // stray continuation
        EXPECT(commit_events(NoSymbol, "\xe4\xb8", 2, ev, 4) == 0);      // truncated sequence
        EXPECT(commit_events(NoSymbol, "\xed\xa0\x80", 3, ev, 4) == 0);  // UTF-8-encoded surrogate
        EXPECT(commit_events(NoSymbol, "\x01", 1, ev, 4) == 0);          // C0
        EXPECT(commit_events(NoSymbol, "\x7f", 1, ev, 4) == 0);          // DEL
        EXPECT(commit_events(NoSymbol, "\xc2\x85", 2, ev, 4) == 0);      // C1 (U+0085)
        // a committed U+FFFD is three bytes: relayed, not dropped
        EXPECT(commit_events(NoSymbol, "\xef\xbf\xbd", 3, ev, 4) == 1);
        EXPECT(ev[0].ch == 0xFFFD);
    }

    // cap truncation: the return value is the total needed, at most cap
    // events are written; a zero cap writes nothing (out may be null)
    {
        zb::input::input_event ev[4];
        EXPECT(commit_events(NoSymbol, "abc", 3, ev, 2) == 3);
        EXPECT(ev[0].ch == 'a' && ev[1].ch == 'b');
        EXPECT(commit_events(NoSymbol, "abc", 3, nullptr, 0) == 3);
        EXPECT(commit_events(NoSymbol, "abc", 3, ev, 4) == 3);
        EXPECT(ev[2].ch == 'c');
    }

    return test::report("x11_input");
}
