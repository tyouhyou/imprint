#include "test.hpp"

#include <cstdint>

#include "test_alloc_count.hpp"

#include "imui.hpp"

using namespace zb::ui;

// Inline size baseline (batch J1): the header must stay bounded so the
// NDS cross build (4MB RAM) keeps thousands of widgets affordable. The
// static assert pins the budget per ABI (host baselines recorded below).
// Layout differs per compiler: gcc x86_64 measures 264 at HEAD (P/H
// batch growth: P-1 paint_dress 24 + H-5 aspect 4 + P-2b sidecar rework;
// was 232); MSVC x64 Release packs identically, while MSVC Debug
// (_ITERATOR_DEBUG_LEVEL) embeds one _Container_proxy pointer per STL
// container -- Widget carries two (id_, text_), so Debug measures 280.
// The 64-bit gate is a desktop tripwire, the hard resource budget is the
// 32-bit line below (wasm32 measures 180, NDS ARM 184: both keep
// headroom).
#if UINTPTR_MAX == 0xffffffffu
// 32-bit ABI budget (NDS); 16bpp builds are smaller still.
static_assert(sizeof(Widget) <= 192, "Widget inline size must stay under the 32-bit batch J budget");
#else
// 64-bit host gate: MSVC Debug baseline 280 + one pointer of headroom.
// The A-10 subtree_shadow_reach_ field (2026-10-03) was re-packed into
// the 2-byte alignment hole between the dirty bools and the dirty ints
// as uint16_t (shadow_spec bounds the pad at < 765), so it costs zero
// on every ABI and this gate is unchanged.
static_assert(sizeof(Widget) <= 288, "Widget inline size must stay under the 64-bit batch J budget");
#endif

// Construction probe (batch J1, tightened by batch J6): widgets share
// one process-level bitmap fallback provider, so a bare widget
// construction allocates nothing beyond the object itself. Stack
// widgets prove the zero-allocation ctor; make_unique proves the object
// is the only remaining allocation.
//
// MSVC Debug exception: the STL allocates one _Container_proxy per
// container under _ITERATOR_DEBUG_LEVEL=2, so Widget's two string
// members (id_, text_) add two allocations per construction that no
// other toolchain pays. The measured baseline is pinned below; the
// zero-allocation gate holds wherever the STL does not instrument
// containers (gcc/clang, MSVC Release).
#if defined(_MSC_VER) && defined(_DEBUG)
constexpr long long kStackConstructAllocs = 200;  // 100 x 2 debug proxies
constexpr long long kMakeUniqueAllocs = 300;      // + the object itself
#else
constexpr long long kStackConstructAllocs = 0;
constexpr long long kMakeUniqueAllocs = 100;
#endif

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
        EXPECT(c.delta() == kStackConstructAllocs);
    }
    {
        test::scoped_alloc_count c;
        for (int i = 0; i < 100; ++i)
        {
            auto w = std::make_unique<Widget>();
        }
        std::printf("allocations for 100 make_unique Widget constructs: %lld\n", c.delta());
        // only the object itself allocates (J6 removed the per-widget
        // provider); MSVC Debug adds the two per-container debug proxies
        EXPECT(c.delta() == kMakeUniqueAllocs);
    }

    return test::report("widget_size");
}