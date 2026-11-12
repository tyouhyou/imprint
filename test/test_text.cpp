#include "test.hpp"

#include "imcore.hpp"

#include "text/bitmap_provider.hpp"
#include "text/glyph_provider.hpp"
#include "text/utf8.hpp"

#include "widget.hpp"

using namespace zb::ui;

// a fake primary provider that covers only 'A' and draws a solid square;
// metrics match the bitmap glyphs (7x7 advance 4) so mixed runs align
class StubProvider : public GlyphProvider
{
public:
    bool covers(const char16_t ch) const override { return ch == u'A'; }
    text_metrics measure(const char16_t *, const int len) const override
    {
        return {4 * len, 7, 7};
    }
    text_metrics line_metrics() const override { return {0, 7, 7}; }
    void write(core::Graphics &g, const char16_t *, const int len,
               const int x, const int y, const core::Color &color) const override
    {
        for (int i = 0; i < len; ++i)
        {
            g.fill_rect(x + i * 4, y - 7, x + i * 4 + 3, y - 1, color);
        }
    }
};

// like StubProvider but the glyph face leaves its last column empty, so
// the +1px double-strike of a unit lands on an unpainted column
class GappedProvider : public GlyphProvider
{
public:
    bool covers(const char16_t ch) const override { return ch == u'A'; }
    text_metrics measure(const char16_t *, const int len) const override
    {
        return {4 * len, 7, 7};
    }
    text_metrics line_metrics() const override { return {0, 7, 7}; }
    void write(core::Graphics &g, const char16_t *, const int len,
               const int x, const int y, const core::Color &color) const override
    {
        for (int i = 0; i < len; ++i)
        {
            g.fill_rect(x + i * 4, y - 7, x + i * 4 + 2, y - 1, color);
        }
    }
};

// double-advance stub: the run-provider suite drives it through
// text_style_run (borrowed, stack-scoped)
class WideAProvider : public GlyphProvider
{
public:
    bool covers(const char16_t ch) const override { return ch == u'A'; }
    text_metrics measure(const char16_t *, const int len) const override
    {
        return {8 * len, 7, 7};
    }
    text_metrics line_metrics() const override { return {0, 7, 7}; }
    void write(core::Graphics &, const char16_t *, const int, const int,
               const int, const core::Color &) const override
    {
    }
};

// lifts the protected measure seam for the style-run suites
struct ProbeWidget : Widget
{
    using Widget::text_advance;
};

int test_text()
{
    // UTF-8 decoding: ASCII stays itself
    {
        const char *p = u8"abc";
        EXPECT(static_cast<char32_t>(u'a') == decode_utf8_next(p));
        EXPECT(static_cast<char32_t>(u'b') == decode_utf8_next(p));
        EXPECT(static_cast<char32_t>(u'c') == decode_utf8_next(p));
        EXPECT(*p == '\0');
    }

    // 2-byte sequence (e-acute U+00E9 = 0xC3 0xA9)
    {
        const char *p = "\xC3\xA9";
        EXPECT(decode_utf8_next(p) == 0xE9);
        EXPECT(*p == '\0');
    }

    // 3-byte sequence (CJK U+4E2D = 0xE4 0xB8 0xAD)
    {
        const char *p = "\xE4\xB8\xAD";
        EXPECT(decode_utf8_next(p) == 0x4E2D);
        EXPECT(*p == '\0');
    }

    // 4-byte sequence (U+1F600) produces a surrogate pair via utf8_to_utf16
    {
        const char *p = "\xF0\x9F\x98\x80";
        EXPECT(decode_utf8_next(p) == 0x1F600);
        EXPECT(*p == '\0');
        const auto u16 = utf8_to_utf16("\xF0\x9F\x98\x80");
        EXPECT(u16.size() == 2);
        if (u16.size() == 2)
        {
            EXPECT(u16[0] == 0xD83D);
            EXPECT(u16[1] == 0xDE00);
        }
    }

    // invalid bytes become U+FFFD, consuming one byte each
    {
        const char *p = "\x80\xFF";
        EXPECT(decode_utf8_next(p) == 0xFFFD);
        EXPECT(decode_utf8_next(p) == 0xFFFD);
        EXPECT(*p == '\0');
    }

    // truncated sequences: the lead byte is reported invalid
    {
        const char *p = "\xE4\xB8";  // missing third byte
        EXPECT(decode_utf8_next(p) == 0xFFFD);
    }

    // overlong encoding of '/' (0xC0 0xAF) is rejected
    {
        const char *p = "\xC0\xAF";
        EXPECT(decode_utf8_next(p) == 0xFFFD);
    }

    // a UTF-8 encoded surrogate (0xED 0xA0 0x80 = U+D800) is rejected;
    // only the lead byte is consumed
    {
        const char *p = "\xED\xA0\x80"
                        "A";
        EXPECT(decode_utf8_next(p) == 0xFFFD);
        EXPECT(*p == '\xA0');
    }

    // an out-of-range sequence (U+110000 = 0xF4 0x90 0x80 0x80) is
    // rejected the same way
    {
        const char *p = "\xF4\x90\x80\x80";
        EXPECT(decode_utf8_next(p) == 0xFFFD);
        EXPECT(*p == '\x90');
    }

    // utf16_to_utf8 substitutes lone surrogates with U+FFFD
    {
        std::u16string lone_lead;
        lone_lead += static_cast<char16_t>(0xD800);
        EXPECT(utf16_to_utf8(lone_lead) == "\xEF\xBF\xBD");
        std::u16string lone_trail;
        lone_trail += static_cast<char16_t>(0xDC00);
        EXPECT(utf16_to_utf8(lone_trail) == "\xEF\xBF\xBD");
    }

    // utf8_to_utf16 on null / empty
    {
        EXPECT(utf8_to_utf16(nullptr).empty());
        EXPECT(utf8_to_utf16("").empty());
        EXPECT(utf8_to_utf16(nullptr, 4).empty());
        EXPECT(utf8_to_utf16("abc", 0).empty());
    }

    // decode_utf8_prev: the backward mirror -- steps back one whole
    // sequence, exhausted at the bound returns U+FFFD without moving
    {
        const char buf[] = "abc";
        const char *p = buf + 3;
        EXPECT(decode_utf8_prev(p, buf) == 'c');
        EXPECT(decode_utf8_prev(p, buf) == 'b');
        EXPECT(decode_utf8_prev(p, buf) == 'a');
        EXPECT(p == buf);
        EXPECT(decode_utf8_prev(p, buf) == 0xFFFD);
        EXPECT(p == buf);  // exhausted: no move
        const char *np = nullptr;
        EXPECT(decode_utf8_prev(np, buf) == 0xFFFD);

        const char cn[] = "a\xE4\xB8\xAD\xF0\x9F\x98\x80";  // a 中 😀
        const char *q = cn + 8;
        EXPECT(decode_utf8_prev(q, cn) == 0x1F600);
        EXPECT(q == cn + 4);
        EXPECT(decode_utf8_prev(q, cn) == 0x4E2D);
        EXPECT(q == cn + 1);
        EXPECT(decode_utf8_prev(q, cn) == static_cast<char32_t>('a'));
        EXPECT(q == cn);

        // an invalid tail byte is one U+FFFD of its own, one byte back
        const char bad[] = "\x80";
        const char *r = bad + 1;
        EXPECT(decode_utf8_prev(r, bad) == 0xFFFD);
        EXPECT(r == bad);
        // a truncated sequence (lead + 1 of 2 continuations) likewise
        const char trunc[] = "a\xE4\xB8";
        const char *s = trunc + 3;
        EXPECT(decode_utf8_prev(s, trunc) == 0xFFFD);
        EXPECT(s == trunc + 2);
        // a 3-continuation run has no lead within reach
        const char stray[] = "\x80\x80\x80\x80";
        const char *u = stray + 4;
        EXPECT(decode_utf8_prev(u, stray) == 0xFFFD);
        EXPECT(u == stray + 3);
    }

    // sanitize_utf8: valid text passes through byte-identical, an
    // invalid or truncated byte becomes U+FFFD, embedded NULs follow
    // the same bound rule as the decoders
    {
        EXPECT(sanitize_utf8("abc") == "abc");
        EXPECT(sanitize_utf8("a\xE4\xB8\xAD\xF0\x9F\x98\x80z") ==
               "a\xE4\xB8\xAD\xF0\x9F\x98\x80z");
        EXPECT(sanitize_utf8("a\xFF"
                             "b") == "a\xEF\xBF\xBD"
                                     "b");
        EXPECT(sanitize_utf8("a\xE4") == "a\xEF\xBF\xBD");  // truncated tail
        EXPECT(sanitize_utf8(nullptr).empty());
        // counted form keeps the embedded NUL
        const char nul[] = "a\0b";
        const std::string s = sanitize_utf8(nul, 3);
        EXPECT(s.size() == 3);
        EXPECT(s[0] == 'a' && s[1] == '\0' && s[2] == 'b');
        // NUL-terminated form stops at the first NUL
        EXPECT(sanitize_utf8(nul) == "a");
    }

    // bounded slice decoding (W-4): a sequence truncated by the slice end
    // is reported invalid on the lead byte, and no byte at or past `end`
    // is ever read -- the packed-bytes form (no NUL terminator)
    {
        const char buf[] = "\xE4\xB8";  // 2 bytes + the literal's NUL
        const char *end = buf + 2;
        const char *p = buf;
        EXPECT(decode_utf8_next(p, end) == 0xFFFD);
        EXPECT(p == buf + 1);  // consumed exactly one byte
        EXPECT(decode_utf8_next(p, end) == 0xFFFD);
        EXPECT(p == end);
        // exhausted: returns U+FFFD without advancing, so a `p < end`
        // loop terminates
        EXPECT(decode_utf8_next(p, end) == 0xFFFD);
        EXPECT(p == end);
        // end <= p is the same exhausted state
        const char *q = buf;
        EXPECT(decode_utf8_next(q, buf) == 0xFFFD);
        EXPECT(q == buf);
    }

    // bounded slice: embedded NULs are ordinary bytes, not terminators
    {
        const char buf[] = "A\x00\xC3\xA9";
        const auto u16 = utf8_to_utf16(buf, 4);
        EXPECT(u16.size() == 3);
        if (u16.size() == 3)
        {
            EXPECT(u16[0] == u'A');
            EXPECT(u16[1] == 0);
            EXPECT(u16[2] == 0xE9);
        }
    }

    // bounded slice: a 4-byte sequence cut short at the last byte
    {
        const char buf[] = "\xF0\x9F\x98";  // missing the 4th byte
        const char *end = buf + 3;
        const char *p = buf;
        EXPECT(decode_utf8_next(p, end) == 0xFFFD);
        EXPECT(p == buf + 1);
    }

    // mixed multibyte text round-trips to the right code units
    {
        const auto u16 = utf8_to_utf16("A\xC3\xA9\xE4\xB8\xAD");
        std::u16string expected;
        expected += u'A';
        expected += 0xE9;
        expected += 0x4E2D;
        EXPECT(u16 == expected);
        // the counted form of the same text agrees
        EXPECT(utf8_to_utf16("A\xC3\xA9\xE4\xB8\xAD", 6) == expected);
    }

    // BitmapProvider: coverage and metrics
    {
        BitmapProvider p;
        EXPECT(p.covers(u'A'));
        EXPECT(p.covers(u'a'));  // uppercase rendering
        EXPECT(p.covers(u' '));
#if defined(IMCORE_HAS_SUBSET)
        // the string literal below is in the generator's scan set, so
        // the generated subset covers its code unit (batch E)
        EXPECT(p.covers(u"é"[0]));
        EXPECT(!p.covers(u'\u4E2D'));  // CJK is never in the 5x7 subset
#else
        EXPECT(!p.covers(u"é"[0]));  // ASCII-only build
#endif
        const auto m = p.measure(u"AB", 2);
        EXPECT(m.width == 12);
        EXPECT(m.height == 7);
        EXPECT(m.ascent == 7);
        EXPECT(p.measure(nullptr, 0).width == 0);
        const auto m2 = p.measure(u"A\u4E2DB", 3);
        EXPECT(m2.width == 12);  // the uncovered code unit advances 0
        const auto lm = p.line_metrics();
        EXPECT(lm.width == 0);
        EXPECT(lm.height == 7);
        EXPECT(lm.ascent == 7);
    }

    // BitmapProvider: write draws the glyph above the baseline
    {
        auto g = core::Graphics::make_ptr(12, 10);
        g->fill(core::colors::Black);
        BitmapProvider p;
        p.write(*g, u"A", 1, 0, 7, core::colors::White);                // baseline at y=7
        EXPECT(test::pixel_at(*g, 1, 0) == core::colors::White.pixel);  // 'A' top row
        EXPECT(test::pixel_at(*g, 0, 6) == core::colors::White.pixel);  // 'A' bottom row left leg
        EXPECT(test::pixel_at(*g, 4, 6) == core::colors::White.pixel);  // 'A' bottom row right leg
        EXPECT(test::pixel_at(*g, 0, 0) != core::colors::White.pixel);  // left of the glyph
        EXPECT(test::pixel_at(*g, 1, 7) != core::colors::White.pixel);  // below the baseline
    }

    // BitmapProvider: subset glyphs (batch E) render with the same
    // advance; an uncovered code unit advances zero width
    {
        auto g = core::Graphics::make_ptr(20, 10);
        g->fill(core::colors::Black);
        BitmapProvider p;
        p.write(*g, u"\u4E2D", 1, 0, 7, core::colors::White);
        EXPECT(test::pixel_at(*g, 0, 0) != core::colors::White.pixel);  // CJK: nothing
#if defined(IMCORE_HAS_SUBSET)
        EXPECT(p.measure(u"é", 1).width == 6);
        p.write(*g, u"é", 1, 0, 7, core::colors::White);                // baseline at y=7
        EXPECT(test::pixel_at(*g, 2, 0) == core::colors::White.pixel);  // accent dot
        EXPECT(test::pixel_at(*g, 0, 6) == core::colors::White.pixel);  // 'e' bottom row
        // demo batch (htmldemo dials): the literals below keep these
        // units in the generator's scan set (code-contract §2.4)
        EXPECT(p.covers(u"°"[0]));
        EXPECT(p.covers(u"■"[0]));
        EXPECT(p.covers(u"◆"[0]));
        EXPECT(p.measure(u"°", 1).width == 6);
        p.write(*g, u"°", 1, 10, 7, core::colors::White);
        EXPECT(test::pixel_at(*g, 11, 0) == core::colors::White.pixel);  // ring top
        EXPECT(test::pixel_at(*g, 10, 0) != core::colors::White.pixel);  // ring left gap
        p.write(*g, u"■", 1, 0, 7, core::colors::White);
        EXPECT(test::pixel_at(*g, 4, 1) == core::colors::White.pixel);  // block face
        EXPECT(test::pixel_at(*g, 0, 0) != core::colors::White.pixel);  // block top margin
        p.write(*g, u"◆", 1, 10, 7, core::colors::White);
        EXPECT(test::pixel_at(*g, 12, 4) == core::colors::White.pixel);  // diamond bottom tip
        EXPECT(test::pixel_at(*g, 10, 2) == core::colors::White.pixel);  // diamond waist
#else
        EXPECT(p.measure(u"é", 1).width == 0);  // ASCII-only build
#endif
    }

    // Widget::set_text decodes UTF-8 into UTF-16 storage
    {
        Widget w;
        w.set_text(u8"A\u4E2D");
        const auto &t = w.get_text();
        EXPECT(t.size() == 2);
        if (t.size() == 2)
        {
            EXPECT(t[0] == u'A');
            EXPECT(t[1] == 0x4E2D);
        }
        w.set_text(nullptr);
        EXPECT(w.get_text().empty());
    }

    // without any font, widget text renders via the bitmap fallback
    {
        auto g = core::Graphics::make_ptr(12, 8);
        g->fill(core::colors::Black);
        Widget w;
        w.set_size(12, 8);
        w.set_text("O");
        w.set_text_color(core::colors::White);
        w.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 0) == core::colors::White.pixel);  // 'O' top row
        EXPECT(test::pixel_at(*g, 1, 6) == core::colors::White.pixel);  // 'O' bottom row
        EXPECT(test::pixel_at(*g, 0, 0) != core::colors::White.pixel);
    }

    // fallback chain: primary covers 'A', bitmap covers 'B', '中' is skipped
    {
        auto g = core::Graphics::make_ptr(20, 8);
        g->fill(core::colors::Black);
        Widget w;
        w.set_size(20, 8);
        w.set_text_color(core::colors::White);
        w.set_glyph_provider(zb::make_shared<StubProvider>());
        w.set_text(std::u16string(u"A\u4E2DB"));
        w.draw(*g);
        // 'A' drawn by the stub as a 4x4 square at x=0, rows 0..6
        EXPECT(test::pixel_at(*g, 1, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 3, 6) == core::colors::White.pixel);
        // '中' advances 0; 'B' falls back to the bitmap at x=4..9
        EXPECT(test::pixel_at(*g, 5, 0) == core::colors::White.pixel);  // 'B' top row
        EXPECT(test::pixel_at(*g, 5, 6) == core::colors::White.pixel);  // 'B' bottom row
        EXPECT(test::pixel_at(*g, 7, 4) != core::colors::White.pixel);  // 'B' right side is empty
        EXPECT(test::pixel_at(*g, 2, 7) != core::colors::White.pixel);  // below the baseline
    }

    // T-3: a run recolors only its units — the rest keeps the base color
    {
        auto g = core::Graphics::make_ptr(16, 8);
        g->fill(core::colors::Black);
        Widget w;
        w.set_size(16, 8);
        w.set_glyph_provider(zb::make_shared<StubProvider>());
        w.set_text(std::u16string(u"AAAA"));
        w.set_text_color(core::colors::White);
        w.set_text_runs({text_style_run{1, 2, core::colors::Red}});
        w.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 5, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 9, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 13, 3) == core::colors::White.pixel);
    }

    // T-3: a run's provider overrides the pick for its units in measure
    // (borrowed, stack-scoped); an empty vector clears the runs
    {
        WideAProvider wide;
        ProbeWidget w;
        w.set_glyph_provider(zb::make_shared<StubProvider>());
        w.set_text(std::u16string(u"AAAA"));
        EXPECT(w.text_advance() == 16);
        w.set_text_runs({text_style_run{0, 2, std::nullopt, &wide}});
        EXPECT(w.text_advance() == 24);  // 8+8 for the run units
        w.set_text_runs({});
        EXPECT(w.text_advance() == 16);
    }

    // T-3: run bold double-strikes only that part (P-2a, per part) —
    // the gapped face makes the +1px strike observable
    {
        auto g = core::Graphics::make_ptr(16, 8);
        g->fill(core::colors::Black);
        Widget w;
        w.set_size(16, 8);
        w.set_glyph_provider(zb::make_shared<GappedProvider>());
        w.set_text(std::u16string(u"AAA"));
        w.set_text_color(core::colors::White);
        w.set_text_runs({text_style_run{2, 1, std::nullopt, nullptr, true}});
        w.draw(*g);
        // unit 2's strike lands in its trailing gap column (x=11)
        EXPECT(test::pixel_at(*g, 11, 3) == core::colors::White.pixel);
        // the base units are not bold: their strike columns stay empty
        EXPECT(test::pixel_at(*g, 3, 3) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 3) != core::colors::White.pixel);
    }

    // T-3: set_text re-clamps runs to the new length — a run left past
    // the end drops, one crossing the end truncates
    {
        auto g = core::Graphics::make_ptr(16, 8);
        Widget w;
        w.set_size(16, 8);
        w.set_glyph_provider(zb::make_shared<StubProvider>());
        w.set_text(std::u16string(u"AAAA"));
        w.set_text_color(core::colors::White);
        w.set_text_runs({text_style_run{1, 3, core::colors::Red}});
        w.draw(*g);
        EXPECT(test::pixel_at(*g, 5, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 13, 3) == core::colors::Red.pixel);

        // two units left: the run truncates to {1,1}
        w.set_text(std::u16string(u"AA"));
        g->fill(core::colors::Black);
        w.draw(*g);
        EXPECT(test::pixel_at(*g, 5, 3) == core::colors::Red.pixel);

        // one unit left: the run sits past the end and drops
        w.set_text(std::u16string(u"A"));
        g->fill(core::colors::Black);
        w.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 3) == core::colors::White.pixel);
    }

    // T-3: overlapping runs — the first run by offset wins
    {
        auto g = core::Graphics::make_ptr(16, 8);
        g->fill(core::colors::Black);
        Widget w;
        w.set_size(16, 8);
        w.set_glyph_provider(zb::make_shared<StubProvider>());
        w.set_text(std::u16string(u"AAAA"));
        w.set_text_color(core::colors::White);
        w.set_text_runs({text_style_run{0, 4, core::colors::Red},
                         text_style_run{2, 2, core::colors::Blue}});
        w.draw(*g);
        EXPECT(test::pixel_at(*g, 1, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 9, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 13, 3) == core::colors::Red.pixel);
    }

    // T-3: a wrapped paragraph splits every line at the run boundaries —
    // a run straddling the wrap colors both of its parts, and a
    // mid-line run end splits its line
    {
        auto g = core::Graphics::make_ptr(8, 21);
        g->fill(core::colors::Black);
        Widget w;
        w.set_size(8, 21);
        w.set_glyph_provider(zb::make_shared<GappedProvider>());
        w.set_text(std::u16string(u"AA AA AA"));
        w.set_text_wrap(true);
        w.set_text_color(core::colors::White);
        // units 1..3: the tail of line 1 and the head of line 2
        w.set_text_runs({text_style_run{1, 3, core::colors::Red}});
        w.draw(*g);
        // line 1 = units 0-1, line 2 = units 3-4, line 3 = units 6-7
        EXPECT(test::pixel_at(*g, 1, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 5, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 1, 10) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 5, 10) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 1, 17) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 5, 17) == core::colors::White.pixel);
    }

    return test::report("text");
}
