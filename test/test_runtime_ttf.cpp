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

    // borrowed-buffer length validation (W-5): a buffer that does not contain
    // the sfnt tables it declares must be rejected before the parser reads
    // it, not discovered halfway through
    {
        std::vector<unsigned char> font;
        std::FILE *f = std::fopen(IM_TEST_RUNTIME_TTF_FONT, "rb");
        EXPECT(f != nullptr);
        if (f != nullptr)
        {
            std::fseek(f, 0, SEEK_END);
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n > 0)
            {
                font.resize(static_cast<size_t>(n));
                const size_t got = std::fread(font.data(), 1, font.size(), f);
                EXPECT(got == font.size());
                font.resize(got);
            }
            std::fclose(f);
        }
        EXPECT(!font.empty());

        // every truncation below the real length is rejected — including
        // one that still spans a complete-looking header
        bool threw = false;
        try
        {
            const auto ok = TtfFamily::from_memory(font.data(), font.size());
            (void)ok;
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(!threw);

        threw = false;
        try
        {
            const auto cut = TtfFamily::from_memory(font.data(), font.size() / 2);
            (void)cut;
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);

        threw = false;
        try
        {
            const auto cut = TtfFamily::from_memory(font.data(), 24);
            (void)cut;
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

    // coverage: ASCII is covered, a CJK code point (numeric literal, so
    // the font-subset scanner never sees a source string) is not and
    // falls through the chain to 5x7
    EXPECT(p16->covers(U'A'));
    EXPECT(p16->covers(U'a'));
    EXPECT(!p16->covers(0x4E2D));

    // line metrics follow the requested pixel size (no linegap formula)
    EXPECT(p16->line_metrics().width == 0);
    EXPECT(p16->line_metrics().height == 16);
    EXPECT(p16->line_metrics().ascent > 0);
    EXPECT(family.provider_for(24)->line_metrics().height == 24);

    // measure never rasterizes: proportional advances, uncovered code
    // points advance zero
    {
        EXPECT(family.rasterization_count() == 0);
        const char32_t ww[] = {U'W', U'W'};
        const char32_t ii[] = {U'I', U'I'};
        const text_metrics wide = p16->measure(ww, 2);
        const text_metrics narrow = p16->measure(ii, 2);
        EXPECT(wide.width > narrow.width);
        EXPECT(wide.height == 16);
        const char32_t mixed[] = {U'A', 0x4E2D, U'B'};
        const char32_t ab[] = {U'A', U'B'};
        const text_metrics with_gap = p16->measure(mixed, 3);
        EXPECT(with_gap.width == p16->measure(ab, 2).width);
        EXPECT(family.rasterization_count() == 0);
    }

    // cache: the first draw rasterizes each distinct code point, the
    // redraw is warm (no new rasterizations, no allocations), pixels land
    {
        auto g = core::Graphics::make_ptr(300, 60);
        g->fill(core::colors::White);

        const char32_t hello[] = {U'H', U'e', U'l', U'l', U'o', U' ', U'g',
                                  U'l', U'y', U'p', U'h', U' ', U'c', U'a',
                                  U'c', U'h', U'e'};
        const long long r0 = family.rasterization_count();
        p16->write(*g, hello, 17, 4, 40, core::colors::Black);
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
            p16->write(*g, hello, 17, 4, 40, core::colors::Black);
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
        const char32_t abc[] = {U'a', U'b', U'c'};
        tp->write(*g, abc, 3, 2, 30, core::colors::Black);
        const long long r0 = tiny.rasterization_count();
        tp->write(*g, abc, 3, 2, 30, core::colors::Black);
        EXPECT(tiny.rasterization_count() - r0 >= 3);
        EXPECT(tiny.cache_bytes() < 4096);
    }

    // widget seam: the label rides the provider; a mixed-coverage string
    // keeps the uncovered code point at zero advance (chain end: 5x7
    // skips it)
    {
        Label l;
        l.set_text("Hello");
        l.set_glyph_provider(family.provider_for(16));
        EXPECT(l.measure().height == 16);

        l.set_text("a\xE4\xB8\xAD"
                   "b");
        const char32_t ab[] = {U'a', U'b'};
        EXPECT(l.measure().width == p16->measure(ab, 2).width);
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
            const char32_t hello[] = {U'H', U'e', U'l', U'l', U'o'};
            EXPECT(mp->measure(hello, 5).width == p16->measure(hello, 5).width);
            auto g = core::Graphics::make_ptr(120, 40);
            const char32_t hi[] = {U'H', U'i'};
            mp->write(*g, hi, 2, 2, 30, core::colors::Black);
            EXPECT(mem.rasterization_count() == 2);
        }
    }

    // per-widget font size (code-contract §2.4): set_font_size resolves
    // the family provider eagerly and measure follows; 0 clears to the
    // process default; missing family / out-of-range throw (init path)
    {
        clear_font_family();
        EXPECT(!has_font_family());
        Label w0;
        bool threw = false;
        try
        {
            w0.set_font_size(16);
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);

        set_font_family(family);
        EXPECT(has_font_family());
        Label w;
        EXPECT(w.font_size() == 0);
        w.set_text("Hello");
        w.set_font_size(24);
        EXPECT(w.font_size() == 24);
        EXPECT(w.measure().height == 24);
        // the same memoized instance as the direct provider path
        Label direct;
        direct.set_text("Hello");
        direct.set_glyph_provider(family.provider_for(24));
        EXPECT(w.measure().width == direct.measure().width);
        w.set_font_size(16);
        EXPECT(w.font_size() == 16);
        EXPECT(w.measure().height == 16);
        w.set_font_size(0);
        EXPECT(w.font_size() == 0);
        threw = false;
        try
        {
            w.set_font_size(129);
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);
        // explicit-family overload needs no process family
        clear_font_family();
        Label w2;
        w2.set_text("Hi");
        w2.set_font_size(20, family);
        EXPECT(w2.font_size() == 20);
        EXPECT(w2.measure().height == 20);
        clear_font_family();
    }

    // builder integration: the font_size prop resolves via the process
    // family; tolerance (no family / out-of-range) keeps the provider
    // and never throws
    {
        set_font_family(family);
        FlexPanel host;
        auto doc = column({label("Hi").named("t").font_size(24)});
        build(host, doc);
        Widget *t = host.find_by_id("t");
        EXPECT(t != nullptr);
        EXPECT(t->font_size() == 24);
        clear_font_family();

        FlexPanel host2;
        auto doc2 = column({label("Hi").named("u").font_size(24)});
        build(host2, doc2);
        Widget *u = host2.find_by_id("");
        EXPECT(u != nullptr);
        EXPECT(u->font_size() == 0);

        set_font_family(family);
        FlexPanel host3;
        auto doc3 = column({label("Hi").named("v").font_size(1000)});
        build(host3, doc3);
        Widget *v = host3.find_by_id("v");
        EXPECT(v != nullptr);
        EXPECT(v->font_size() == 0);
        clear_font_family();
    }

    // A-25: a non-BMP code point resolves through covers() like any
    // other code point. Inter carries no emoji glyph, so the answer is
    // negative — it measures 0 (uncovered code points advance nothing)
    // and the drop semantics stand; a covering provider receives the
    // whole code point (locked by the stub checks in
    // test_text_input/test_text_area)
    {
        const auto p = family.provider_for(16);
        EXPECT(!p->covers(0x1F600));

        const char32_t with_emoji[] = {U'H', 0x1F600, U'i'};
        // the emoji contributes nothing: measure == "H" + "i" alone
        const char32_t plain[] = {U'H', U'i'};
        EXPECT(p->measure(with_emoji, 3).width == p->measure(plain, 2).width);

        // drawing the uncovered emoji routes once through the cache keyed
        // by the code point and lands on glyph 0: one probe, an empty
        // coverage entry, zero cache bytes — the drop, code-point-shaped
        core::Graphics g(64, 32, nullptr);
        const long long before = family.rasterization_count();
        const size_t bytes_before = family.cache_bytes();
        const char32_t emoji[] = {0x1F600};
        p->write(g, emoji, 1, 1, 20, core::colors::Black);
        EXPECT(family.rasterization_count() - before == 1);
        EXPECT(family.cache_bytes() == bytes_before);
    }

    return test::report("runtime_ttf");
}
