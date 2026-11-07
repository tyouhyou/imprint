#include "test.hpp"

#import <Cocoa.h>

#include "input.hpp"
#include "mac_input.hpp"

/*
 * A-2 InputSource, mac keyboard half, dummy-driven: synthetic NSEvents
 * (no window, no run loop) must translate into the exact input_event
 * stream the dispatcher contract promises -- the §3.8 chord table the
 * full-audit's M3 defect lived in. Only built on APPLE.
 */

using zb::shell::mac_input::nsevent_to_key;

namespace
{
    // AppKit's documented key-event payload: `chars` is what the
    // platform hands over (control bytes under Ctrl, the unmodified
    // letter under Cmd), `ignoring` the plain key the layout produces
    NSEvent *key_event(const NSEventType type, const NSEventModifierFlags flags,
                       NSString *chars, NSString *ignoring,
                       const unsigned short code)
    {
        return [NSEvent keyEventWithType:type
                                location:NSZeroPoint
                           modifierFlags:flags
                               timestamp:0
                            windowNumber:0
                                 context:nil
                              characters:chars
             charactersIgnoringModifiers:ignoring
                                isARepeat:NO
                                  keyCode:code];
    }

    zb::input::input_event translate(const NSEventType type,
                                     const NSEventModifierFlags flags,
                                     NSString *chars, NSString *ignoring,
                                     const unsigned short code,
                                     bool *handled = nullptr)
    {
        NSEvent *const e = key_event(type, flags, chars, ignoring, code);
        zb::input::input_event ev;
        const bool ok = nsevent_to_key(e, ev);
        if (handled != nullptr)
        {
            *handled = ok;
        }
        return ev;
    }
}  // namespace

int test_mac_input()
{
    using namespace zb::input;
    const NSEventType down = NSEventTypeKeyDown;
    const NSEventType up = NSEventTypeKeyUp;

    // §3.8 chords rebuild from the control byte (M3): key + mod_ctrl,
    // ch = 0 -- the table that used to drop the whole event
    {
        bool ok = true;
        const auto ev = translate(down, NSEventModifierFlagControl,
                                  @"\x03", @"c", 8, &ok);
        EXPECT(ok);
        EXPECT(ev.key == 'c');
        EXPECT(ev.mods == mod_ctrl);
        EXPECT(ev.ch == 0);
        EXPECT(ev.type == input_type::key_down);
    }
    {
        const auto ev = translate(down, NSEventModifierFlagControl,
                                  @"\x16", @"v", 9);
        EXPECT(ev.key == 'v');
        EXPECT((ev.mods & mod_ctrl) != 0);
        EXPECT(ev.ch == 0);
    }
    {
        const auto ev = translate(down, NSEventModifierFlagControl,
                                  @"\x01", @"a", 0);
        EXPECT(ev.key == 'a');
        EXPECT(ev.ch == 0);
    }

    // plain printable: ch on key_down, key half on both edges
    {
        const auto ev = translate(down, 0, @"c", @"c", 8);
        EXPECT(ev.key == 'c');
        EXPECT(ev.ch == 'c');
        EXPECT(ev.mods == 0);
    }

    // under Cmd the characters string carries the unmodified letter
    // (Cmd+C -> "c"): the chord keeps its key half but never injects ch
    {
        const auto ev = translate(down, NSEventModifierFlagCommand,
                                  @"c", @"c", 8);
        EXPECT(ev.key == 'c');
        EXPECT(ev.mods == mod_meta);
        EXPECT(ev.ch == 0);
    }

    // U-1: key_up carries both the key half and the modifier mask
    {
        const auto ev = translate(up, NSEventModifierFlagControl,
                                  @"\x03", @"c", 8);
        EXPECT(ev.key == 'c');
        EXPECT(ev.mods == mod_ctrl);
        EXPECT(ev.ch == 0);
        EXPECT(ev.type == input_type::key_up);
    }
    {
        const auto ev = translate(up, NSEventModifierFlagShift,
                                  @"C", @"c", 8);
        EXPECT(ev.key == 'c');
        EXPECT(ev.mods == mod_shift);
        EXPECT(ev.ch == 0);
    }

    // navigation keys come from the key codes and coexist with mods
    {
        const auto ev = translate(down, NSEventModifierFlagShift,
                                  @"\uF700", @"\uF700", 126);
        EXPECT(ev.key == static_cast<int>(key_code::up));
        EXPECT(ev.mods == mod_shift);
        EXPECT(ev.ch == 0);
    }

    // nav keys route through the switch, not the control-byte table:
    // backspace's 0x08 stays excluded from the chord rebuild
    {
        const auto ev = translate(down, 0, @"\x08", @"\x7f", 51);
        EXPECT(ev.key == static_cast<int>(key_code::backspace));
        EXPECT(ev.ch == 0);
    }

    // space keeps its navigation routing (B1) and never becomes text
    {
        const auto ev = translate(down, 0, @" ", @" ", 49);
        EXPECT(ev.key == static_cast<int>(key_code::space));
        EXPECT(ev.ch == 0);
    }

    // punctuation keeps the character half even under Ctrl, as on win
    {
        const auto ev = translate(down, NSEventModifierFlagControl,
                                  @"!", @"!", 7);
        EXPECT(ev.ch == '!');
        EXPECT(ev.key == 0);
    }

    // FI-01 committed-text relay: a BMP unit outside ASCII/C1 relays
    // as ch with key = 0
    {
        const auto ev = translate(down, 0, @"\u4f60", @"\u4f60", 0);
        EXPECT(ev.ch == 0x4f60);
        EXPECT(ev.key == 0);
    }

    // the AppKit function-key band is key semantics, never text
    {
        bool ok = true;
        const auto ev = translate(down, 0, @"\uF708", @"\uF708", 96, &ok);
        EXPECT(!ok);  // F5 maps to no nav key: dropped like win_input's
        EXPECT(ev.ch == 0);
    }

    // a committed astral character's lead surrogate is dropped (A-25
    // note: no cluster/advance extension yet); the astral literal
    // reaches the shell as its UTF-16 lead surrogate at index 0
    {
        bool ok = true;
        const auto ev = translate(down, 0, @"\U0001F600", @"\U0001F600", 0, &ok);
        EXPECT(!ok);
        EXPECT(ev.ch == 0);
    }

    // an unmapped key with no character produces nothing
    {
        bool ok = true;
        translate(down, 0, @"", @"", 200, &ok);
        EXPECT(!ok);
    }

    return 0;
}
