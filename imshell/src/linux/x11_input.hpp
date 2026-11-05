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

    /*
     * TJ-C4: the modifier chords (code-contract §3.8) — ORs X11's
     * state mask (Control/Shift/Mod1/Mod4) into `out.mods` as the
     * framework's ctrl/shift/alt/meta bits. Exported pure for the
     * display-less test, like key_from_keysym.
     */
    void mods_from_x11(unsigned int state, zb::input::input_event &out);

    /*
     * FI-01 committed-text relay: turns one IME commit -- the keysym and
     * the UTF-8 string an input context reports for a KeyPress -- into
     * the key_down events the framework contract promises, and returns
     * the total number produced. At most `cap` of them are written to
     * `out`; a caller detects truncation by comparing the return value
     * against `cap`. Pure and allocation-free like translate(): the
     * caller passes a fixed array, no Display is needed.
     *
     * Event shapes (seam contract, input.hpp):
     *   - printable ASCII code points except space go through
     *     decorate_printable_key, so a single-code-point commit is
     *     exactly the legacy key+ch event;
     *   - non-ASCII code points (non-BMP included) relay as
     *     ch = code point, key = 0 (the term_input T-2 precedent);
     *   - space is a navigation key, never a character (B1): its key
     *     event comes from the keysym side alone;
     *   - control characters (C0, DEL, C1) and invalid UTF-8 produce
     *     nothing; decoding goes through the framework decoder
     *     (decode_utf8_next), which never reads past `utf8 + len`;
     *   - when the text side produced no event (bare keysym lookup, or
     *     a commit that was only control characters / space), a keysym
     *     that maps to a key_code appends one key_down with ch = 0, so
     *     Enter/Tab/... stay reachable through the input context; with
     *     XLookupBoth the text event already carries key+ch (the legacy
     *     shape) and the keysym half adds nothing.
     */
    int commit_events(KeySym keysym, const char *utf8, int len,
                      zb::input::input_event *out, int cap);
}
