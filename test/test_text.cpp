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

    // utf8_to_utf16 on null / empty
    {
        EXPECT(utf8_to_utf16(nullptr).empty());
        EXPECT(utf8_to_utf16("").empty());
    }

    // mixed multibyte text round-trips to the right code units
    {
        const auto u16 = utf8_to_utf16("A\xC3\xA9\xE4\xB8\xAD");
        std::u16string expected;
        expected += u'A';
        expected += 0xE9;
        expected += 0x4E2D;
        EXPECT(u16 == expected);
    }

    // BitmapProvider: coverage and metrics
    {
        BitmapProvider p;
        EXPECT(p.covers(u'A'));
        EXPECT(p.covers(u'a'));  // uppercase rendering
        EXPECT(p.covers(u' '));
        EXPECT(!p.covers(u'\u4E2D'));
        EXPECT(!p.covers(0xE9));
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
        p.write(*g, u"A", 1, 0, 7, core::colors::White);  // baseline at y=7
        EXPECT(test::pixel_at(*g, 1, 0) == core::colors::White.pixel);  // 'A' top row
        EXPECT(test::pixel_at(*g, 0, 6) == core::colors::White.pixel);  // 'A' bottom row left leg
        EXPECT(test::pixel_at(*g, 4, 6) == core::colors::White.pixel);  // 'A' bottom row right leg
        EXPECT(test::pixel_at(*g, 0, 0) != core::colors::White.pixel);  // left of the glyph
        EXPECT(test::pixel_at(*g, 1, 7) != core::colors::White.pixel);  // below the baseline
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

    return test::report("text");
}
