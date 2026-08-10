#include "test.hpp"

#include "text/text_image.hpp"

using namespace zb::ui;

namespace
{
    // counts the pixels equal to the given color in a surface
    int count_pixels(const core::Graphics &g, const core::Color &c)
    {
        const auto s = g.size();
        int n = 0;
        for (int y = 0; y < s.height; ++y)
        {
            for (int x = 0; x < s.width; ++x)
            {
                if (test::pixel_at(g, x, y) == c.pixel)
                {
                    ++n;
                }
            }
        }
        return n;
    }
}

int test_blit_font()
{
    const auto fg = core::colors::White;
    const auto bg = core::colors::Black;

    // a single 'A' renders its 18 set pixels
    {
        auto g = make_text_image("A", 5, 7, fg, bg);
        EXPECT(count_pixels(*g, fg) == 18);
        EXPECT(count_pixels(*g, bg) == 35 - 18);
    }

    // lowercase maps to uppercase
    {
        auto g1 = make_text_image("a", 5, 7, fg, bg);
        auto g2 = make_text_image("A", 5, 7, fg, bg);
        EXPECT(count_pixels(*g1, fg) == count_pixels(*g2, fg));
    }

    // unsupported characters are skipped: 'A' + '~' + 'B' == 'A' + 'B'
    {
        auto g = make_text_image("A~B", 17, 7, fg, bg);
        EXPECT(count_pixels(*g, fg) == 18 + 20);  // 'B' has 20 set pixels
    }

    // a blank glyph (space) renders nothing
    {
        auto g = make_text_image("  ", 11, 7, fg, bg);
        EXPECT(count_pixels(*g, fg) == 0);
        EXPECT(count_pixels(*g, bg) == 77);
    }

    // empty text renders only the background
    {
        auto g = make_text_image("", 5, 7, fg, bg);
        EXPECT(count_pixels(*g, fg) == 0);
    }

    // a larger surface centers the glyph and keeps the rest as background
    {
        auto g = make_text_image("A", 20, 20, fg, bg);
        EXPECT(count_pixels(*g, fg) == 18);
        EXPECT(count_pixels(*g, fg) + count_pixels(*g, bg) == 400);
        // 'A' top row (3 pixels) sits at y = (20-7)/2 = 6, starting x = (20-5)/2 = 7
        EXPECT(test::pixel_at(*g, 8, 6) == fg.pixel);
        EXPECT(test::pixel_at(*g, 6, 6) == bg.pixel);
    }

    // digits and punctuation have glyphs too
    {
        auto g = make_text_image("0!?", 17, 7, fg, bg);
        EXPECT(count_pixels(*g, fg) > 0);
    }

    return test::report("blit_font");
}
