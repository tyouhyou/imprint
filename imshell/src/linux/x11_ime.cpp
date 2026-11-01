// FI-01: the Display-side IME pipeline for the X11 shell. Pure plumbing
// around XIM/XIC -- open, filter, focus, lookup -- with every failure
// degrading to the shell's legacy XLookupString path (one warning, no
// throw). The committed text itself is turned into input_events by the
// pure x11_input::commit_events, which is where the event-shape contract
// lives.
#include "x11_ime.hpp"

#include <X11/Xlocale.h>  // XSetLocaleModifiers

#include <clocale>

#include "logging.hpp"
#include "x11_input.hpp"

namespace zb::shell::x11_ime
{
    namespace
    {
        // untrusted-input hardening: a commit larger than this is host
        // input gone wrong and is dropped, not relayed
        constexpr int max_commit_bytes = 4096;
    }

    bool open(Display *display, const Window client, context &ctx)
    {
#ifndef X_HAVE_UTF8_STRING
        // without the UTF-8 lookup a commit cannot be decoded, so the IM
        // is never opened and the shell keeps its legacy path
        (void)display;
        (void)client;
        (void)ctx;
        LW << "Xlib lacks Xutf8LookupString: IME relay disabled, committed "
              "text stays on the legacy XLookupString path";
        return false;
#else
        // only LC_CTYPE moves to the user's locale: LC_NUMERIC must stay
        // "C" or the framework's number formatting and the design-file
        // parsers change under the whole process
        setlocale(LC_CTYPE, "");
        // empty modifiers honor XMODIFIERS (ibus/fcitx/uim announce here)
        XSetLocaleModifiers("");

        ctx.client = client;
        ctx.im = XOpenIM(display, nullptr, nullptr, nullptr);
        if (ctx.im == nullptr)
        {
            LW << "XOpenIM failed: no input method on this display, "
                  "committed text stays on the legacy XLookupString path";
            return false;
        }
        // root-window preedit (2026-11-01 ruling): the IM server paints
        // its own composition window, the shell builds no composition UI
        ctx.ic = XCreateIC(ctx.im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                           XNClientWindow, client, XNFocusWindow, client, nullptr);
        if (ctx.ic == nullptr)
        {
            LW << "XCreateIC failed: no input context for the shell window, "
                  "committed text stays on the legacy XLookupString path";
            XCloseIM(ctx.im);
            ctx.im = nullptr;
            ctx.client = 0;
            return false;
        }
        return true;
#endif
    }

    void close(context &ctx)
    {
        if (ctx.ic != nullptr)
        {
            XDestroyIC(ctx.ic);
            ctx.ic = nullptr;
        }
        if (ctx.im != nullptr)
        {
            XCloseIM(ctx.im);
            ctx.im = nullptr;
        }
        ctx.client = 0;
    }

    bool filter(const context &ctx, XEvent *event)
    {
        return ctx.ic != nullptr && XFilterEvent(event, ctx.client) != False;
    }

    void set_focus(const context &ctx, const bool focused)
    {
        if (ctx.ic == nullptr)
        {
            return;
        }
        if (focused)
        {
            XSetICFocus(ctx.ic);
        }
        else
        {
            XUnsetICFocus(ctx.ic);
        }
    }

    int lookup(const context &ctx, XEvent *event, zb::input::input_event *out, const int cap)
    {
#ifdef X_HAVE_UTF8_STRING
        if (ctx.ic == nullptr || event->type != KeyPress)
        {
            return 0;
        }
        // first attempt on a stack buffer; on XBufferOverflow the IC
        // reports the size it needs and the lookup is retried once
        char small_buf[128];
        KeySym keysym = NoSymbol;
        Status status = XLookupNone;
        const char *chars = small_buf;
        int len = Xutf8LookupString(ctx.ic, &event->xkey, small_buf,
                                    static_cast<int>(sizeof(small_buf)), &keysym, &status);
        if (status == XBufferOverflow)
        {
            if (len <= 0 || len > max_commit_bytes)
            {
                LW << "IME commit requests " << len << " bytes, above the "
                   << max_commit_bytes << " byte cap: dropped";
                return 0;
            }
            char retry[max_commit_bytes];
            chars = retry;
            len = Xutf8LookupString(ctx.ic, &event->xkey, retry, max_commit_bytes, &keysym, &status);
            if (status == XBufferOverflow)
            {
                LW << "IME commit overflowed the retry buffer: dropped";
                return 0;
            }
        }
        // reduce the IC status to the (keysym, chars, len) shape
        // commit_events consumes; keysym_return is only meaningful for
        // XLookupBoth / XLookupKeySym
        KeySym committed_keysym = NoSymbol;
        int chars_len = 0;
        switch (status)
        {
        case XLookupChars:
            chars_len = len;
            break;
        case XLookupBoth:
            committed_keysym = keysym;
            chars_len = len;
            break;
        case XLookupKeySym:
            committed_keysym = keysym;
            break;
        case XLookupNone:
        default:
            return 0;
        }
        return zb::shell::x11_input::commit_events(committed_keysym, chars, chars_len, out, cap);
#else
        (void)ctx;
        (void)event;
        (void)out;
        (void)cap;
        return 0;
#endif
    }
}
