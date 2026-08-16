#include "test.hpp"

#include <cstdint>

#include "test_alloc_count.hpp"

#include "imui.hpp"

using namespace zb::ui;

// Inline size baseline (batch J1): the header must stay bounded so the
// NDS cross build (4MB RAM) keeps thousands of widgets affordable. The
// static assert pins the budget per ABI (the printed values are the
// host baseline recorded in CONTEXT.md).
#if UINTPTR_MAX == 0xffffffffu
// 32-bit ABI budget (NDS); 16bpp builds are smaller still.
static_assert(sizeof(Widget) <= 192, "Widget inline size must stay under the 32-bit batch J budget");
#else
// 64-bit host baseline (216 measured on x86_64): one pointer of headroom.
static_assert(sizeof(Widget) <= 224, "Widget inline size must stay under the 64-bit batch J budget");
#endif

// Construction probe (batch J1, tightened by batch J6): widgets share
// one process-level bitmap fallback provider, so a bare widget
// construction allocates nothing beyond the object itself. Stack
// widgets prove the zero-allocation ctor; make_unique proves the object
// is the only remaining allocation.
int test_widget_size()
{
    std::printf("host ABI sizeof: Widget=%zu Label=%zu Button=%zu Checkbox=%zu "
                "Slider=%zu ListBox=%zu TextInput=%zu Panel=%zu FlexPanel=%zu\n",
                sizeof(Widget), sizeof(Label), sizeof(Button), sizeof(Checkbox),
                sizeof(Slider), sizeof(ListBox), sizeof(TextInput), sizeof(Panel),
                sizeof(FlexPanel));

    {
        // warm the process-level fallback singleton (J6)
        Widget warm;
    }
    {
        test::scoped_alloc_count c;
        for (int i = 0; i < 100; ++i)
        {
            Widget w;
        }
        std::printf("allocations for 100 stack Widget constructs: %lld\n", c.delta());
        EXPECT(c.delta() == 0);
    }
    {
        test::scoped_alloc_count c;
        for (int i = 0; i < 100; ++i)
        {
            auto w = std::make_unique<Widget>();
        }
        std::printf("allocations for 100 make_unique Widget constructs: %lld\n", c.delta());
        // only the object itself allocates (J6 removed the per-widget
        // provider)
        EXPECT(c.delta() == 100);
    }

    return test::report("widget_size");
}