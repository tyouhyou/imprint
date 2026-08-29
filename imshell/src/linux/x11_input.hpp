#pragma once

#include <X11/Xlib.h>

#include "input.hpp"

namespace zb::shell::x11_input
{
    enum class result
    {
        not_handled,  // not an input event (Expose, ClientMessage, ...): the event loop keeps it
        handled,      // translated: `out` carries the event to feed
        swallowed,    // an input-shaped event the framework deliberately drops
    };

    /*
     * A-2 InputSource: translates one X event into an input_event. The
     * key_code mapping (KeySym -> key_code), the printable-character
     * rule (latin-1 via XLookupString, single byte, space keeps its key
     * routing) and the wheel-as-button mapping (4/5 -> +/-1 notch) live
     * here once, and the whole mapping is dummy-driven unit-testable
     * with synthetic XEvents: the function is pure -- it needs no X
     * server, no Display, no window.
     */
    result translate(const XEvent &event, zb::input::input_event &out);

    /*
     * the centralized KeySym -> key_code mapping (0 = unmapped); exported
     * separately because the KeySym lookup inside a synthetic XEvent needs
     * a real keyboard mapping, which a display-less test cannot provide
     */
    int key_from_keysym(KeySym ks);
}
