#include "clipboard.hpp"

namespace zb::ui
{
    namespace
    {
        // the borrowed, host-owned sink (set_clipboard_sink). Plain
        // storage like the logging handle: the UI contract is
        // single-threaded and the sink is installed once at startup
        ClipboardSink *g_clipboard_sink = nullptr;
    }  // namespace

    void set_clipboard_sink(ClipboardSink *sink)
    {
        g_clipboard_sink = sink;
    }

    ClipboardSink *clipboard_sink()
    {
        return g_clipboard_sink;
    }
}  // namespace zb::ui
