#include "win_input.hpp"

#include <windowsx.h>

namespace zb::shell::win_input
{
    // TJ-C4: the modifier chords ride the key_down (code-contract
    // §3.8) — GetKeyState is the source of truth at event time (the
    // async tab is wrong during the same-message processing)
    void mods_from_win(zb::input::input_event &out)
    {
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            out.mods |= zb::input::mod_ctrl;
        }
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
        {
            out.mods |= zb::input::mod_shift;
        }
        if ((GetKeyState(VK_MENU) & 0x8000) != 0)
        {
            out.mods |= zb::input::mod_alt;
        }
        const bool meta = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                          (GetKeyState(VK_RWIN) & 0x8000) != 0;
        if (meta)
        {
            out.mods |= zb::input::mod_meta;
        }
    }

    // the centralized VK_* -> key_code mapping; 0 = unmapped
    int key_from_virtual_key(const WPARAM vk)
    {
        switch (vk)
        {
        case VK_RETURN:
            return static_cast<int>(zb::input::key_code::enter);
        case VK_TAB:
            return static_cast<int>(zb::input::key_code::tab);
        case VK_ESCAPE:
            return static_cast<int>(zb::input::key_code::escape);
        case VK_SPACE:
            return static_cast<int>(zb::input::key_code::space);
        case VK_BACK:
            return static_cast<int>(zb::input::key_code::backspace);
        case VK_DELETE:
            return static_cast<int>(zb::input::key_code::del);
        case VK_UP:
            return static_cast<int>(zb::input::key_code::up);
        case VK_DOWN:
            return static_cast<int>(zb::input::key_code::down);
        case VK_LEFT:
            return static_cast<int>(zb::input::key_code::left);
        case VK_RIGHT:
            return static_cast<int>(zb::input::key_code::right);
        default:
            // letters and digits are platform-independent key codes on
            // both edges (U-1): their VK codes are ASCII, letters
            // normalize to lowercase
            if (vk >= 'A' && vk <= 'Z')
            {
                return static_cast<int>(vk + 0x20);
            }
            if (vk >= '0' && vk <= '9')
            {
                return static_cast<int>(vk);
            }
            return 0;
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
            // letters/digits emit through WM_CHAR instead (one merged
            // key_down carrying key + ch, U-1) -- emitting here too
            // would deliver two key_down events per keystroke
            if ((key >= 'a' && key <= 'z') || (key >= '0' && key <= '9'))
            {
                return result::swallowed;
            }
            out.type = zb::input::input_type::key_down;
            mods_from_win(out);
            out.key = key;
            return result::handled;
        }
        case WM_KEYUP:
        {
            // U-1: keys ride both edges so hosts can pair down/up for
            // key-state (hold-to-walk); ch is always 0 on keyup
            const int key = key_from_virtual_key(wparam);
            if (key == 0)
            {
                return result::swallowed;
            }
            out.type = zb::input::input_type::key_up;
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
            // TJ-C4: the Ctrl chords ride WM_CHAR's control byte —
            // letters/digits emit through WM_CHAR, so WM_KEYDOWN never
            // sees them: 0x01-0x1A minus the functional set (tab,
            // enter, backspace; 0x1B never posts) maps to its letter
            // with mod_ctrl, the same table as the terminal
            // translator. GetKeyState is the same-message truth for
            // shift/alt/meta.
            if (c >= 0x01 && c <= 0x1A && c != '\t' && c != '\r' &&
                c != 0x08)
            {
                out.type = zb::input::input_type::key_down;
                out.key = 'a' + (c - 1);
                mods_from_win(out);
                return result::handled;
            }
            if (c >= 0x20 && c <= 0x7e && c != ' ')
            {
                out.type = zb::input::input_type::key_down;
                // U-1: letters/digits also carry the platform-independent
                // key code (lowercase), so this single event pairs with
                // its WM_KEYUP; punctuation stays key == 0 (layout-bound)
                zb::input::decorate_printable_key(out, c);
                return result::handled;
            }
            // FI-01 committed-text relay: a Unicode window posts one
            // WM_CHAR per UTF-16 unit, so IME commits reach this branch
            // whole. BMP code points outside the ASCII and C1 control
            // areas relay as ch = code point with key = 0 (the
            // T-2/term_input precedent)
            if ((c >= 0xA0 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFF))
            {
                out.type = zb::input::input_type::key_down;
                out.ch = c;
                return result::handled;
            }
            // surrogate units are not standalone code points: a committed
            // astral character arrives as a pair and is dropped until the
            // cluster/advance extension (A-25); C1 controls (0x7F-0x9F)
            // and C0 keep the long-standing drop rule
            return result::swallowed;
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
