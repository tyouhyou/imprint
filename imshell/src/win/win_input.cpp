#include "win_input.hpp"

#include <windowsx.h>

namespace zb::shell::win_input
{
    // the centralized VK_* -> key_code mapping; 0 = unmapped
    int key_from_virtual_key(const WPARAM vk)
    {
        switch (vk)
        {
            case VK_RETURN: return static_cast<int>(zb::input::key_code::enter);
            case VK_TAB: return static_cast<int>(zb::input::key_code::tab);
            case VK_ESCAPE: return static_cast<int>(zb::input::key_code::escape);
            case VK_SPACE: return static_cast<int>(zb::input::key_code::space);
            case VK_BACK: return static_cast<int>(zb::input::key_code::backspace);
            case VK_DELETE: return static_cast<int>(zb::input::key_code::del);
            case VK_UP: return static_cast<int>(zb::input::key_code::up);
            case VK_DOWN: return static_cast<int>(zb::input::key_code::down);
            case VK_LEFT: return static_cast<int>(zb::input::key_code::left);
            case VK_RIGHT: return static_cast<int>(zb::input::key_code::right);
            default: return 0;
        }
    }

    namespace
    {
        zb::input::input_event pointer_event(const zb::input::input_type type, const LPARAM lparam)
        {
            zb::input::input_event ev;
            ev.type = type;
            ev.touch_id = 0;
            ev.x = GET_X_LPARAM(lparam);
            ev.y = GET_Y_LPARAM(lparam);
            return ev;
        }
    }

    result translate(const UINT msg, const WPARAM wparam, const LPARAM lparam,
                     const POINT &wheel_client, zb::input::input_event &out)
    {
        out = zb::input::input_event{};
        switch (msg)
        {
            case WM_MOUSEMOVE:
                out = pointer_event(zb::input::input_type::mouse_move, lparam);
                return result::handled;
            case WM_LBUTTONDOWN:
                out = pointer_event(zb::input::input_type::mouse_left_down, lparam);
                out.button = zb::input::mouse_button_t::left;
                return result::handled;
            case WM_LBUTTONUP:
                out = pointer_event(zb::input::input_type::mouse_left_up, lparam);
                out.button = zb::input::mouse_button_t::left;
                return result::handled;
            case WM_RBUTTONDOWN:
                out = pointer_event(zb::input::input_type::mouse_right_down, lparam);
                out.button = zb::input::mouse_button_t::right;
                return result::handled;
            case WM_RBUTTONUP:
                out = pointer_event(zb::input::input_type::mouse_right_up, lparam);
                out.button = zb::input::mouse_button_t::right;
                return result::handled;
            case WM_KEYDOWN:
            {
                const int key = key_from_virtual_key(wparam);
                if (key == 0)
                {
                    // the old WndProc ate unmapped keydowns without
                    // DefWindowProc -- keep that discipline
                    return result::swallowed;
                }
                out.type = zb::input::input_type::key_down;
                out.key = key;
                return result::handled;
            }
            case WM_CHAR:
            {
                // printable characters arrive after TranslateMessage; the
                // space key keeps its key_code::space routing for
                // navigation activation (dispatcher B1), other printable
                // ASCII becomes a character event
                const int c = static_cast<int>(wparam);
                if (c < 0x20 || c > 0x7e || c == ' ')
                {
                    return result::swallowed;
                }
                out.type = zb::input::input_type::key_down;
                out.ch = c;
                return result::handled;
            }
            case WM_MOUSEWHEEL:
            {
                // normalize to signed notches (WHEEL_DELTA = one notch):
                // the shell-independent unit every consumer may rely on
                // (A-15); sub-notch deltas from free-spinning wheels drop
                const int delta = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
                if (delta == 0)
                {
                    return result::swallowed;
                }
                out.type = zb::input::input_type::mouse_wheel;
                out.delta = delta;
                out.touch_id = 0;
                out.x = wheel_client.x;
                out.y = wheel_client.y;
                return result::handled;
            }
            default:
                return result::not_handled;
        }
    }
}
