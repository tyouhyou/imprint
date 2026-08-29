#include "test.hpp"

#include <windows.h>

#include "win_input.hpp"

/*
 * A-2 InputSource, win32 half, dummy-driven: synthetic window messages
 * (no window, no message pump) must translate into the exact input_event
 * stream the dispatcher contract promises. Only built on WIN32.
 */

using zb::shell::win_input::result;
using zb::shell::win_input::translate;

namespace
{
    zb::input::input_event run(const UINT msg, const WPARAM wp = 0, const LPARAM lp = 0)
    {
        // the caller would convert WM_MOUSEWHEEL's screen point to client
        // space; tests pass the identity point
        const POINT wheel_client{11, 22};
        zb::input::input_event ev;
        const result r = translate(msg, wp, lp, wheel_client, ev);
        EXPECT(r != result::not_handled);
        return ev;
    }
}

int test_win_input()
{
    // the centralized VK table
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_RETURN) == static_cast<int>(zb::input::key_code::enter));
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_TAB) == static_cast<int>(zb::input::key_code::tab));
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_ESCAPE) == static_cast<int>(zb::input::key_code::escape));
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_SPACE) == static_cast<int>(zb::input::key_code::space));
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_BACK) == static_cast<int>(zb::input::key_code::backspace));
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_DELETE) == static_cast<int>(zb::input::key_code::del));
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_UP) == static_cast<int>(zb::input::key_code::up));
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_DOWN) == static_cast<int>(zb::input::key_code::down));
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_LEFT) == static_cast<int>(zb::input::key_code::left));
    EXPECT(zb::shell::win_input::key_from_virtual_key(VK_RIGHT) == static_cast<int>(zb::input::key_code::right));
    EXPECT(zb::shell::win_input::key_from_virtual_key(0x41 /* 'A' */) == 0);  // unmapped

    // pointer events carry client coordinates and the button
    {
        const zb::input::input_event ev = run(WM_MOUSEMOVE, 0, MAKELPARAM(15, 25));
        EXPECT(ev.type == zb::input::input_type::mouse_move);
        EXPECT(ev.x == 15 && ev.y == 25);
        EXPECT(ev.touch_id == 0);
        EXPECT(ev.button == zb::input::mouse_button_t::none);
    }
    {
        const zb::input::input_event ev = run(WM_LBUTTONDOWN, 0, MAKELPARAM(3, 4));
        EXPECT(ev.type == zb::input::input_type::mouse_left_down);
        EXPECT(ev.button == zb::input::mouse_button_t::left);
        EXPECT(ev.x == 3 && ev.y == 4);
    }
    {
        const zb::input::input_event ev = run(WM_LBUTTONUP, 0, MAKELPARAM(3, 4));
        EXPECT(ev.type == zb::input::input_type::mouse_left_up);
        EXPECT(ev.button == zb::input::mouse_button_t::left);
    }
    {
        const zb::input::input_event ev = run(WM_RBUTTONDOWN, 0, MAKELPARAM(7, 8));
        EXPECT(ev.type == zb::input::input_type::mouse_right_down);
        EXPECT(ev.button == zb::input::mouse_button_t::right);
    }
    {
        const zb::input::input_event ev = run(WM_RBUTTONUP, 0, MAKELPARAM(7, 8));
        EXPECT(ev.type == zb::input::input_type::mouse_right_up);
        EXPECT(ev.button == zb::input::mouse_button_t::right);
    }

    // keyboard: mapped keys produce key_down with the key_code
    {
        const zb::input::input_event ev = run(WM_KEYDOWN, VK_RETURN);
        EXPECT(ev.type == zb::input::input_type::key_down);
        EXPECT(ev.key == static_cast<int>(zb::input::key_code::enter));
        EXPECT(ev.ch == 0);
    }
    // keyboard: unmapped keys are swallowed (no event, no DefWindowProc)
    {
        const POINT wheel_client{0, 0};
        zb::input::input_event ev;
        EXPECT(translate(WM_KEYDOWN, 0x41, 0, wheel_client, ev) == result::swallowed);
    }

    // characters: printable ASCII becomes a character event
    {
        const zb::input::input_event ev = run(WM_CHAR, 'a');
        EXPECT(ev.type == zb::input::input_type::key_down);
        EXPECT(ev.ch == 'a');
        EXPECT(ev.key == 0);
    }
    // characters: space keeps its navigation-key routing (no WM_CHAR event)
    {
        const POINT wheel_client{0, 0};
        zb::input::input_event ev;
        EXPECT(translate(WM_CHAR, ' ', 0, wheel_client, ev) == result::swallowed);
    }
    // characters: control characters are dropped
    {
        const POINT wheel_client{0, 0};
        zb::input::input_event ev;
        EXPECT(translate(WM_CHAR, '\t', 0, wheel_client, ev) == result::swallowed);
    }

    // wheel: normalized to signed notches, sub-notch deltas dropped,
    // coordinates from the pre-converted client point
    {
        const zb::input::input_event up = run(WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        EXPECT(up.type == zb::input::input_type::mouse_wheel);
        EXPECT(up.delta == 1);
        EXPECT(up.x == 11 && up.y == 22);

        const zb::input::input_event down = run(WM_MOUSEWHEEL, MAKEWPARAM(0, (unsigned short)-WHEEL_DELTA), 0);
        EXPECT(down.delta == -1);

        const zb::input::input_event two = run(WM_MOUSEWHEEL, MAKEWPARAM(0, 2 * WHEEL_DELTA), 0);
        EXPECT(two.delta == 2);

        const POINT wheel_client{0, 0};
        zb::input::input_event ev;
        EXPECT(translate(WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), 0, wheel_client, ev) ==
               result::swallowed);  // free-spinning sub-notch delta
    }

    // non-input messages stay with the shell's own switch
    {
        const POINT wheel_client{0, 0};
        zb::input::input_event ev;
        EXPECT(translate(WM_PAINT, 0, 0, wheel_client, ev) == result::not_handled);
        EXPECT(translate(WM_DESTROY, 0, 0, wheel_client, ev) == result::not_handled);
    }

    return test::report("win_input");
}
