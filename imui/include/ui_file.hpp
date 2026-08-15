#pragma once

#include <string>

#include "ui_builder.hpp"

namespace zb::ui
{
    /*
     * Parses a descriptive UI document (the .ui text format; grammar and
     * property table in docs/code-contract.md §builder).
     *
     * Returns the document root. A single top-level container
     * (panel/column/row) is returned as the root itself so its container
     * properties (spacing/padding/wrap) apply to the build() host;
     * otherwise the root is a pseudo node (type "root") whose children
     * are the top-level widgets.
     *
     * Tolerant like the builders: line-level problems (bare values,
     * unknown tokens, unterminated strings) are logged and the line is
     * skipped; the document keeps parsing. `ok` is set to false only
     * when the document yields no widget at all (empty file / every
     * line dropped) — build-time validators treat that as an error.
     */
    ui_node parse_ui_text(const char *text, bool *ok);
}  // namespace zb::ui
