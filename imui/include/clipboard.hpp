#pragma once

#include <cstddef>
#include <string>

namespace zb::ui
{
    /*
     * FI-02 clipboard seam (code-contract §3.8): the widgets route
     * copy/paste through one host-installed sink. Module-level like the
     * logging sink; the pointer is borrowed for the process lifetime
     * and the host owns it.
     *
     * A null sink (the default) makes copy silently drop and paste a
     * no-op. A sink may be set-only — get_text returns empty — which
     * disables paste but not copy (the SIXEL OSC 52 case).
     */
    struct ClipboardSink
    {
        virtual ~ClipboardSink() = default;
        // stores the UTF-8 text (n bytes) as the clipboard content
        virtual void set_text(const char *utf8, size_t n) = 0;
        // the current clipboard content as UTF-8 (empty = nothing to
        // paste; a set-only sink always returns empty)
        virtual std::string get_text() const = 0;
    };

    // installs the process-wide sink (borrowed; nullptr restores the
    // default no-op behavior)
    void set_clipboard_sink(ClipboardSink *sink);

    // the installed sink, or nullptr
    [[nodiscard]] ClipboardSink *clipboard_sink();
}
