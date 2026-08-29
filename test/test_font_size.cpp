#include "test.hpp"

#include "imui.hpp"
#include "text/ttf_provider.hpp"
#include "ttf_glyphs.hpp"

using namespace zb::ui;

// Process-wide default glyph provider seam (code-contract 2.4, plan 2,
// USE_FONT_SIZE). Compiled only when IMCORE_HAS_TTF_SUBSET is on
// (test/CMakeLists gates the source); the battery never starts a shell
// (the only install point), so this suite owns the install/remove cycle
// and restores the empty default at the end.
int test_font_size()
{
    // nothing installed: widgets stay on the deterministic 5x7 bitmap
    {
        EXPECT(default_glyph_provider().get() == nullptr);
        Label l;
        l.set_text("A");
        EXPECT(l.measure().height == 7);
    }

    // install the platform font: new widgets render at the rasterized
    // line height instead of the 5x7 metrics
    set_default_glyph_provider(ttf_subset_provider());
    {
        EXPECT(default_glyph_provider().get() != nullptr);
        Label l;
        l.set_text("A");
        EXPECT(l.measure().height == kTtfLineHeight);
        EXPECT(l.measure().width > 0);
    }

    // an explicit provider wins over the process default
    {
        Label l;
        l.set_text("A");
        l.set_glyph_provider(zb::make_shared<BitmapProvider>());
        EXPECT(l.measure().height == 7);
    }

    // remove the default: back to the 5x7 bitmap
    set_default_glyph_provider(zb::SharedPtr<GlyphProvider>());
    {
        EXPECT(default_glyph_provider().get() == nullptr);
        Label l;
        l.set_text("A");
        EXPECT(l.measure().height == 7);
    }

    return test::report("font_size");
}
