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
 * read event fields only and are fully covered. Only built where the X11
 * backend builds.
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

    return test::report("x11_input");
}
