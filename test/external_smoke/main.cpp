// External-consumption smoke (Batch G P1): this file plays the role of
// a FOREIGN application — its own project, no in-tree includes beyond
// the public headers — that links the imprint libraries through the
// imprint:: aliases (ARCHITECTURE §3.2) and drives one headless frame,
// the same CanvasWindow pattern the test battery uses. The point is
// the packaging: if the gates, the aliases, or the link surface break
// for an out-of-tree consumer, this build fails first.
#include <cstdio>

#include "html.hpp"
#include "imapp.hpp"
#include "imui.hpp"
#include "ui_file.hpp"

using namespace zb::app;

int main()
{
    CanvasWindow app;
    app.create(64, 32);
    app.paint();

    if (app.data() == nullptr || app.width() != 64 || app.height() != 32)
    {
        std::printf("external smoke: window surface not usable\n");
        return 1;
    }

    // A-28: the front-end gates must not change the link surface. A build
    // with a front-end on parses a valid document; the feature-off build
    // still links the same symbols and rejects every document (the stub
    // contract, code-contract §5.4).
    bool html_ok = false;
    zb::ui::ui_node html_doc =
        zb::ui::parse_html("<body><label text=\"smoke\"/></body>", &html_ok);
    if (html_ok != (IMUI_HAS_HTML != 0))
    {
        std::printf("external smoke: HTML front-end contract violated (ok=%d)\n",
                    html_ok ? 1 : 0);
        return 1;
    }
    bool ui_ok = false;
    zb::ui::ui_node ui_doc = zb::ui::parse_ui_text("label text=\"smoke\"", &ui_ok);
    if (ui_ok != (IMUI_HAS_UI_FILE != 0))
    {
        std::printf("external smoke: .ui front-end contract violated (ok=%d)\n",
                    ui_ok ? 1 : 0);
        return 1;
    }
    (void)html_doc;
    (void)ui_doc;

    std::printf("external smoke: painted a 64x32 headless frame\n");
    return 0;
}
