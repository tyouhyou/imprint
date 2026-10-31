// A-28: IMPRINT_FEATURE_UI_FILE=OFF. The .ui parser is compiled out and this
// stub keeps the public surface link-complete (the C-ABI declarative entry
// and ui_embed call parse_ui_text unconditionally; code-contract §5.4). Every
// document is rejected (ok = false), so the existing "yields no widget"
// init-path failure handles it -- no new error path.
#include "ui_file.hpp"

#include "logging.hpp"

namespace zb::ui
{
    ui_node parse_ui_text(const char *text, bool *ok)
    {
        (void)text;
        if (ok != nullptr)
        {
            *ok = false;
        }
        LE << "parse_ui_text: this build was configured with IMPRINT_FEATURE_UI_FILE=OFF";
        return ui_node{};
    }
}  // namespace zb::ui
