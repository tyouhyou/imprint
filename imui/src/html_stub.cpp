// A-28: IMPRINT_FEATURE_HTML=OFF. The parser is compiled out and this stub
// keeps the public surface link-complete: the C-ABI declarative entry and
// the design-file tools call parse_html unconditionally, and a feature gate
// must never turn a missing front-end into a link error (code-contract §5.4).
// Every document is rejected (ok = false), so the existing "yields no
// widget" init-path failure handles it -- no new error path.
#include "html.hpp"

#include "logging.hpp"

namespace zb::ui
{
    ui_node parse_html(const char *html, bool *ok, html_page *page)
    {
        (void)html;
        (void)page;
        if (ok != nullptr)
        {
            *ok = false;
        }
        LE << "parse_html: this build was configured with IMPRINT_FEATURE_HTML=OFF";
        return ui_node{};
    }
}  // namespace zb::ui
