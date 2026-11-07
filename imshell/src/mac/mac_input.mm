// macOS shell, A-2 InputSource keyboard half: the unit-tested seam
// (win_input / x11_input shape). See mac_input.hpp; the body moved
// verbatim out of run_mac.mm when the mac_input suite demanded a
// linkable TU.

#import <Cocoa/Cocoa.h>

#include "input.hpp"
#include "mac_input.hpp"

namespace zb::shell::mac_input
{
    bool nsevent_to_key(NSEvent *nse, zb::input::input_event &out)
    {
        out = zb::input::input_event{};
        out.type = (nse.type == NSEventTypeKeyUp)
                       ? zb::input::input_type::key_up
                       : zb::input::input_type::key_down;
        // TJ-C4: the modifier chords (code-contract §3.8) — the
        // event's modifierFlags is the same-event truth; the Command
        // flag maps to mod_meta (the mac convention) while the
        // §3.8 clipboard bindings stay on Control, the platform-
        // faithful reading
        if ((nse.modifierFlags & NSEventModifierFlagControl) != 0)
        {
            out.mods |= zb::input::mod_ctrl;
        }
        if ((nse.modifierFlags & NSEventModifierFlagShift) != 0)
        {
            out.mods |= zb::input::mod_shift;
        }
        if ((nse.modifierFlags & NSEventModifierFlagOption) != 0)
        {
            out.mods |= zb::input::mod_alt;
        }
        if ((nse.modifierFlags & NSEventModifierFlagCommand) != 0)
        {
            out.mods |= zb::input::mod_meta;
        }
        switch (nse.keyCode)
        {
            case 36: out.key = static_cast<int>(zb::input::key_code::enter); break;  // return
            case 48: out.key = static_cast<int>(zb::input::key_code::tab); break;
            case 53: out.key = static_cast<int>(zb::input::key_code::escape); break;
            case 49: out.key = static_cast<int>(zb::input::key_code::space); break;
            case 51: out.key = static_cast<int>(zb::input::key_code::backspace); break;
            case 117: out.key = static_cast<int>(zb::input::key_code::del); break;   // forward delete
            case 126: out.key = static_cast<int>(zb::input::key_code::up); break;
            case 125: out.key = static_cast<int>(zb::input::key_code::down); break;
            case 123: out.key = static_cast<int>(zb::input::key_code::left); break;
            case 124: out.key = static_cast<int>(zb::input::key_code::right); break;
            default: break;
        }
        // printable ASCII (layout-aware characters string): ch on key_down,
        // plus the platform-independent lowercase key code on both edges
        // (U-1, through the shared decorate_printable_key). Space keeps its
        // navigation-key routing (B1).
        if (out.key != static_cast<int>(zb::input::key_code::space))
        {
            NSString *chars = nse.characters;
            if (chars.length > 0)
            {
                const int c = static_cast<int>([chars characterAtIndex:0]);
                // §3.8 chords (TJ-C4): with Control held the characters
                // string carries the ASCII control byte (Ctrl+C -> "\x03")
                // and the plain letter is unrecoverable from it, so the
                // chord is rebuilt from the byte -- the same table as
                // win_input's WM_CHAR and the terminal translator. A chord
                // rides key + mods only, ch = 0 (§3.8: Ctrl+C/V/A arrive
                // as key 'c'/'v'/'a' with mod_ctrl).
                // 0x08/0x09/0x0D stay excluded: the sibling tables rule
                // those chords unreachable, and the real backspace/tab/
                // enter keys route through the nav switch above.
                if (out.key == 0 && c >= 0x01 && c <= 0x1A &&
                    c != 0x08 && c != '\t' && c != '\r')
                {
                    out.key = 'a' + (c - 1);
                }
                else if (c >= 0x20 && c <= 0x7e)
                {
                    // Ctrl/Meta must not inject text: under Command the
                    // characters string carries the unmodified letter
                    // (Cmd+C -> "c"), and a §3.8 chord rides key + mods
                    // with ch = 0 -- a letter/digit keeps its key half
                    // but drops ch while ctrl or meta is held (decorate
                    // reports the letter/digit half through its return;
                    // punctuation stays a character event, as on win).
                    if (zb::input::decorate_printable_key(out, c) &&
                        (out.mods & (zb::input::mod_ctrl | zb::input::mod_meta)) != 0)
                    {
                        out.ch = 0;
                    }
                }
            }
        }
        // FI-01 committed-text relay (macOS half): the platform IM owns
        // composition and hands the committed text back in the characters
        // string; the shell relays it. key_down only (key_up carries no
        // ch, U-1): the first UTF-16 unit outside the ASCII and C1 control
        // areas relays as ch = code point with key = 0 (the T-2 /
        // term_input precedent). The AppKit function-key band
        // (0xF700-0xF8FF: arrows, F-keys, forward-delete) carries key
        // semantics, not committed text, and stays ch = 0. A committed
        // astral character arrives as a surrogate pair and its lead unit
        // is dropped until the cluster/advance extension (A-25).
        if (out.type == zb::input::input_type::key_down)
        {
            NSString *chars = nse.characters;
            if (chars.length > 0)
            {
                const unichar c = [chars characterAtIndex:0];
                if ((c >= 0xA0 && c <= 0xD7FF) ||
                    (c >= 0xE000 && c <= 0xFFFF && !(c >= 0xF700 && c <= 0xF8FF)))
                {
                    out.ch = c;
                }
            }
        }
        return out.key != 0 || out.ch != 0;
    }
}
