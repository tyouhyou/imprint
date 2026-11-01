#pragma once

#include <X11/Xlib.h>

#include "input.hpp"

namespace zb::shell::x11_ime
{
    /*
     * FI-01: the platform IME owns composition (2026-11-01 ruling) -- the
     * shell builds no composition UI of its own. The input method runs
     * with root-window preedit (XIMPreeditNothing|XIMStatusNothing), so
     * the IM server paints its own lookup/preedit window and the shell
     * only relays committed text as input_events. Every call is safe on
     * a context whose open() failed: the XIM/XIC handles are null and
     * the functions degrade to "not active".
     */
    struct context
    {
        XIM im = nullptr;
        XIC ic = nullptr;
        Window client = 0;
    };

    /*
     * Opens the input method and an input context for the shell window.
     * Only LC_CTYPE is moved to the user's locale: LC_NUMERIC must stay
     * "C" or the framework's number formatting and the design-file
     * parsers change under the process. Any failed step logs one warning
     * and returns false with the context left closed -- the shell then
     * runs its legacy XLookupString path unchanged. Also returns false
     * (before touching Xlib) when the platform Xlib lacks the UTF-8
     * lookup (no X_HAVE_UTF8_STRING): without it a commit cannot be
     * decoded, so the IM is never opened.
     */
    bool open(Display *display, Window client, context &ctx);

    /* Teardown symmetric with open(): destroys the IC, closes the IM. */
    void close(context &ctx);

    /*
     * True when the input method consumed the event for itself
     * (composition bookkeeping, preedit keystrokes); a filtered event
     * must not reach the translator.
     */
    bool filter(const context &ctx, XEvent *event);

    /*
     * Focus tracking: the IM server only routes keys to an IC with the
     * focus set.
     */
    void set_focus(const context &ctx, bool focused);

    /*
     * Turns one KeyPress into committed-text events through the input
     * context and relays them via x11_input::commit_events. Returns the
     * total number of events produced; at most `cap` are written to
     * `out`. A commit larger than the 4096-byte cap is untrusted host
     * input and is dropped with a warning, as is an event whose status
     * the IC reports as unusable.
     */
    int lookup(const context &ctx, XEvent *event, zb::input::input_event *out, int cap);
}
