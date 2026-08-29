#include "test.hpp"

#include "test_alloc_count.hpp"

#include "imui.hpp"
#include "text/ttf_provider.hpp"
#include "ttf_glyphs.hpp"

using namespace zb::ui;

// TTF subset provider (batch S2; code-contract 2.4, plan 2). Compiled
// only when IMCORE_HAS_TTF_SUBSET is on (test/CMakeLists gates the
// source); the table content depends on the configured TTF_FONT, so the
// assertions hold for any usable font.
int test_ttf_subset()
{
    // table shape: non-empty, strictly sorted (binary-search invariant)
    {
        EXPECT(kTtfGlyphCount > 0);
        for (size_t i = 1; i < kTtfGlyphCount; ++i)
        {
            EXPECT(kTtfGlyphs[i - 1].ch < kTtfGlyphs[i].ch);
        }
        EXPECT(kTtfAscent > 0);
        EXPECT(kTtfLineHeight >= kTtfAscent);
    }

    TtfSubsetProvider p;

    // coverage + metrics over ASCII (every text font draws these)
    {
        EXPECT(p.covers(u'A'));
        EXPECT(p.covers(u'0'));

        const text_metrics one = p.measure(u"A", 1);
        EXPECT(one.width > 0);
        EXPECT(one.height == kTtfLineHeight);
        EXPECT(one.ascent == kTtfAscent);

        const text_metrics two = p.measure(u"AA", 2);
        EXPECT(two.width == 2 * one.width);

        const text_metrics lm = p.line_metrics();
        EXPECT(lm.width == 0);
        EXPECT(lm.height == kTtfLineHeight);
        EXPECT(lm.ascent == kTtfAscent);
    }

    // write paints coverage pixels; the hot path allocates nothing
    {
        auto g = core::Graphics::make_ptr(64, 32);
        g->fill(core::colors::White);
        {
            test::scoped_alloc_count c;
            p.write(*g, u"A", 1, 4, 24, core::colors::Black);
            EXPECT(c.delta() == 0);
        }
        int painted = 0;
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 64; ++x)
            {
                if (test::pixel_at(*g, x, y) != core::colors::White.pixel)
                {
                    ++painted;
                }
            }
        }
        EXPECT(painted > 0);
    }

    // widgets opt in through the provider seam: once installed, text
    // metrics come from the table, not from the 5x7 fallback
    {
        Label l;
        l.set_text("A");
        l.set_glyph_provider(ttf_subset_provider());
        EXPECT(l.measure().height == kTtfLineHeight);
    }

    return test::report("ttf_subset");
}
