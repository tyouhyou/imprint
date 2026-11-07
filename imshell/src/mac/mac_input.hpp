// macOS shell, A-2 InputSource keyboard half (the win_input /
// x11_input shape): NSEvent -> input_event as a unit-testable seam --
// no window, no run loop. The pointer half stays in run_mac.mm: it is
// bound to the view's presentation state.
//
// 32bpp-only shell, like run_mac.mm.

#import <Cocoa.h>

#include "input.hpp"

namespace zb::shell::mac_input
{
    /*
     * Navigation keys map from the hardware key codes (stable across
     * layouts); printable text comes from the characters string
     * (already layout aware); Ctrl chords ride its control bytes and
     * Ctrl/Cmd never inject text (code-contract §3.8, TJ-C4). Returns
     * false when the event produces nothing the framework understands.
     */
    bool nsevent_to_key(NSEvent *nse, zb::input::input_event &out);
}
