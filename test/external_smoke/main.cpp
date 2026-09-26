// External-consumption smoke (Batch G P1): this file plays the role of
// a FOREIGN application — its own project, no in-tree includes beyond
// the public headers — that links the imprint libraries through the
// imprint:: aliases (ARCHITECTURE §3.2) and drives one headless frame,
// the same CanvasWindow pattern the test battery uses. The point is
// the packaging: if the gates, the aliases, or the link surface break
// for an out-of-tree consumer, this build fails first.
#include <cstdio>

#include "imapp.hpp"
#include "imui.hpp"

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
    std::printf("external smoke: painted a 64x32 headless frame\n");
    return 0;
}
