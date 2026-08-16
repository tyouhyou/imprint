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

// Construction probe (batch J1): every widget currently allocates its
// default bitmap fallback provider once (widget.hpp:480). This suite
// records the per-widget figure; the batch J6 commit removes the
// allocation and tightens this assertion to 0.
int test_widget_size()
{
    std::printf("host ABI sizeof: Widget=%zu Label=%zu Button=%zu Checkbox=%zu "
                "Slider=%zu ListBox=%zu TextInput=%zu Panel=%zu FlexPanel=%zu\n",
                sizeof(Widget), sizeof(Label), sizeof(Button), sizeof(Checkbox),
                sizeof(Slider), sizeof(ListBox), sizeof(TextInput), sizeof(Panel),
                sizeof(FlexPanel));

    {
        test::scoped_alloc_count c;
        for (int i = 0; i < 100; ++i)
        {
            auto w = std::make_unique<Widget>();
        }
        std::printf("allocations for 100 bare Widget constructs: %lld\n", c.delta());
        // each construct = make_unique + make_shared control block; the
        // batch J6 commit removes the per-widget provider and tightens
        // this to 0
        EXPECT(c.delta() <= 200);
        EXPECT(c.delta() > 0);
    }

    return test::report("widget_size");
}