#include "x11_input.hpp"

#include <X11/Xutil.h>  // XLookupString (Xlib.h has XLookupKeysym only)
#include <X11/keysym.h>

namespace zb::shell::x11_input
{
    namespace
    {
        zb::input::input_event pointer_event(const zb::input::input_type type, const int x, const int y)
        {
            zb::input::input_event ev;
            ev.type = type;
            ev.touch_id = 0;
            ev.x = x;
            ev.y = y;
            return ev;
        }
    }

// the centralized KeySym -> key_code mapping; 0 = unmapped (then a
// printable character may still be produced)
int key_from_keysym(const KeySym ks)
{
    switch (ks)
    {
        case XK_Return: return static_cast<int>(zb::input::key_code::enter);
        case XK_Tab: return static_cast<int>(zb::input::key_code::tab);
        case XK_Escape: return static_cast<int>(zb::input::key_code::escape);
        case XK_space: return static_cast<int>(zb::input::key_code::space);
        case XK_BackSpace: return static_cast<int>(zb::input::key_code::backspace);
        case XK_Delete: return static_cast<int>(zb::input::key_code::del);
        case XK_Up: return static_cast<int>(zb::input::key_code::up);
        case XK_Down: return static_cast<int>(zb::input::key_code::down);
        case XK_Left: return static_cast<int>(zb::input::key_code::left);
        case XK_Right: return static_cast<int>(zb::input::key_code::right);
        default: return 0;
    }
}

result translate(const XEvent &event, zb::input::input_event &out)
    {
        out = zb::input::input_event{};
        switch (event.type)
        {
            case KeyPress:
            {
                out.type = zb::input::input_type::key_down;
                out.key = key_from_keysym(XLookupKeysym(const_cast<XKeyPressedEvent *>(&event.xkey), 0));
                if (out.key == 0)
                {
                    // printable character (latin-1 from XLookupString;
                    // only single-byte, handles shift via the modifier
                    // state). Keys that produced a key field keep their
                    // key semantics (space is the navigation-activation
                    // key, not a character)
                    char buf[4];
                    const int len = XLookupString(const_cast<XKeyPressedEvent *>(&event.xkey), buf, sizeof(buf), nullptr, nullptr);
                    if (len == 1 && static_cast<unsigned char>(buf[0]) >= 0x20 &&
                        static_cast<unsigned char>(buf[0]) <= 0x7e)
                    {
                        out.ch = buf[0];
                    }
                }
                return (out.key != 0 || out.ch != 0) ? result::handled : result::swallowed;
            }
            case ButtonPress:
            {
                const auto *bp = &event.xbutton;
                if (bp->button == 1)
                {
                    out = pointer_event(zb::input::input_type::mouse_left_down, bp->x, bp->y);
                    out.button = zb::input::mouse_button_t::left;
                    return result::handled;
                }
                if (bp->button == 3)
                {
                    // right press: mapped symmetrically to the release
                    // (Win32 sends both, the dispatcher ignores them for
                    // now but the pair must arrive consistently)
                    out = pointer_event(zb::input::input_type::mouse_right_down, bp->x, bp->y);
                    out.button = zb::input::mouse_button_t::right;
                    return result::handled;
                }
                if (bp->button == 4 || bp->button == 5)
                {
                    // the wheel arrives as button 4/5 presses; delta
                    // carries the direction and x/y the pointer (the
                    // dispatcher routes the wheel by position)
                    out = pointer_event(zb::input::input_type::mouse_wheel, bp->x, bp->y);
                    out.delta = (bp->button == 4) ? 1 : -1;
                    return result::handled;
                }
                return result::swallowed;  // middle button & friends
            }
            case ButtonRelease:
            {
                const auto *br = &event.xbutton;
                if (br->button == 1)
                {
                    out = pointer_event(zb::input::input_type::mouse_left_up, br->x, br->y);
                    out.button = zb::input::mouse_button_t::left;
                    return result::handled;
                }
                if (br->button == 3)
                {
                    out = pointer_event(zb::input::input_type::mouse_right_up, br->x, br->y);
                    out.button = zb::input::mouse_button_t::right;
                    return result::handled;
                }
                return result::swallowed;
            }
            case MotionNotify:
            {
                out = pointer_event(zb::input::input_type::mouse_move, event.xmotion.x, event.xmotion.y);
                return result::handled;
            }
            default:
                return result::not_handled;
        }
    }
}
