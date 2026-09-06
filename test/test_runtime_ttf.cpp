#include "test.hpp"

#include "test_alloc_count.hpp"

#include "imui.hpp"
#include "text/runtime_ttf_provider.hpp"

#include <cstdio>
#include <vector>

using namespace zb::ui;

// Runtime TTF provider (batch L-5; code-contract 2.4, 8). Compiled only
// when IMCORE_HAS_TTF_RUNTIME is on (test/CMakeLists gates the source);
// the deterministic font is the in-repo Inter (IM_TEST_RUNTIME_TTF_FONT).
int test_runtime_ttf()
{
    // init path: unreadable files and bad fonts throw (contract 1.1)
    {
        bool threw = false;
        try
        {
            TtfFamily::from_file("no_such_font_file.ttf");
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);

        threw = false;
        try
        {
            const unsigned char junk[16] = {0};
            TtfFamily::from_memory(junk, sizeof(junk));
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);
    }

    TtfFamily family = TtfFamily::from_file(IM_TEST_RUNTIME_TTF_FONT);

    // per-size providers: equal px is the same instance, all sizes share
    // the family cache; the size range is the init-path gate
    {
        const auto a = family.provider_for(16);
        const auto b = family.provider_for(16);
        const auto c = family.provider_for(24);
        EXPECT(a.get() == b.get());
        EXPECT(a.get() != c.get());

        bool threw = false;
        try
        {
            const auto ignored = family.provider_for(0);
            (void)ignored;
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);
        threw = false;
        try
        {
            const auto ignored = family.provider_for(1000);
            (void)ignored;
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);
    }

    const auto p16 = family.provider_for(16);

    // coverage: ASCII is covered, a CJK code unit (numeric literal, so
    // the font-subset scanner never sees a source string) is not and
    // falls through the chain to 5x7
    EXPECT(p16->covers(u'A'));
    EXPECT(p16->covers(u'a'));
    EXPECT(!p16->covers(char16_t(0x4E2D)));

    // line metrics follow the requested pixel size (no linegap formula)
    EXPECT(p16->line_metrics().width == 0);
    EXPECT(p16->line_metrics().height == 16);
    EXPECT(p16->line_metrics().ascent > 0);
    EXPECT(family.provider_for(24)->line_metrics().height == 24);

    // measure never rasterizes: proportional advances, uncovered units
    // advance zero
    {
        EXPECT(family.rasterization_count() == 0);
        const text_metrics wide = p16->measure(u"WW", 2);
        const text_metrics narrow = p16->measure(u"II", 2);
        EXPECT(wide.width > narrow.width);
        EXPECT(wide.height == 16);
        const char16_t mixed[] = {u'A', 0x4E2D, u'B', 0};
        const text_metrics with_gap = p16->measure(mixed, 3);
        EXPECT(with_gap.width == p16->measure(u"AB", 2).width);
        EXPECT(family.rasterization_count() == 0);
    }

    // cache: the first draw rasterizes each distinct code unit, the
    // redraw is warm (no new rasterizations, no allocations), pixels land
    {
        auto g = core::Graphics::make_ptr(300, 60);
        g->fill(core::colors::White);

        const long long r0 = family.rasterization_count();
        p16->write(*g, u"Hello glyph cache", 17, 4, 40, core::colors::Black);
        EXPECT(family.rasterization_count() - r0 > 0);

        int painted = 0;
        for (int y = 0; y < 60; ++y)
        {
            for (int x = 0; x < 300; ++x)
            {
                if (test::pixel_at(*g, x, y) != core::colors::White.pixel)
                {
                    ++painted;
                }
            }
        }
        EXPECT(painted > 0);

        const long long r1 = family.rasterization_count();
        {
            test::scoped_alloc_count c;
            p16->write(*g, u"Hello glyph cache", 17, 4, 40, core::colors::Black);
            EXPECT(c.delta() == 0);  // warm draw: zero allocations (§8)
        }
        EXPECT(family.rasterization_count() == r1);
        EXPECT(family.cache_bytes() > 0);
    }

    // budget eviction: a 1-byte budget drops everything on every insert,
    // so the redraw re-rasterizes and the byte load stays tiny
    {
        TtfFamily tiny = TtfFamily::from_file(IM_TEST_RUNTIME_TTF_FONT, 1);
        auto tp = tiny.provider_for(16);
        auto g = core::Graphics::make_ptr(120, 40);
        tp->write(*g, u"abc", 3, 2, 30, core::colors::Black);
        const long long r0 = tiny.rasterization_count();
        tp->write(*g, u"abc", 3, 2, 30, core::colors::Black);
        EXPECT(tiny.rasterization_count() - r0 >= 3);
        EXPECT(tiny.cache_bytes() < 4096);
    }

    // widget seam: the label rides the provider; a mixed-coverage string
    // keeps the uncovered unit at zero advance (chain end: 5x7 skips it)
    {
        Label l;
        l.set_text("Hello");
        l.set_glyph_provider(family.provider_for(16));
        EXPECT(l.measure().height == 16);

        const char16_t mixed[] = {u'a', 0x4E2D, u'b', 0};
        l.set_text(std::u16string(mixed));
        EXPECT(l.measure().width == p16->measure(u"ab", 2).width);
    }

    // from_memory borrows: the same bytes rasterize the same advances
    {
        std::FILE *f = std::fopen(IM_TEST_RUNTIME_TTF_FONT, "rb");
        EXPECT(f != nullptr);
        std::vector<unsigned char> bytes;
        if (f != nullptr)
        {
            std::fseek(f, 0, SEEK_END);
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            bytes.resize(static_cast<size_t>(n));
            const size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
            EXPECT(got == bytes.size());
        }
        if (!bytes.empty())
        {
            TtfFamily mem = TtfFamily::from_memory(bytes.data(), bytes.size());
            const auto mp = mem.provider_for(16);
            EXPECT(mp->measure(u"Hello", 5).width == p16->measure(u"Hello", 5).width);
            auto g = core::Graphics::make_ptr(120, 40);
            mp->write(*g, u"Hi", 2, 2, 30, core::colors::Black);
            EXPECT(mem.rasterization_count() == 2);
        }
    }

    return test::report("runtime_ttf");
}
