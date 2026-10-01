// Installed-config smoke (Batch G P1.5): a foreign mini project that
// consumes the INSTALLED package — cmake --install to a staging prefix,
// then find_package(imprint CONFIG) — and drives the same headless
// frame as the add_subdirectory smoke. The point is the packaging:
// missing targets, broken include layout, or unresolved link deps in
// the export set fail here, not at a real consumer.
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
        std::printf("installed smoke: window surface not usable\n");
        return 1;
    }
    std::printf("installed smoke: painted a 64x32 headless frame via find_package(imprint)\n");
    return 0;
}
