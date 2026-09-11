#include "test.hpp"
#include "theme.hpp"
#include "trend_line.hpp"
#include <vector>

int test_trend_line()
{
    using namespace zb::ui;

    set_theme(light_theme());
    const core::Color field = theme().field_bg;
    const core::Color accent = theme().accent;

    TrendLine t;
    EXPECT(t.get_count() == 0);
    EXPECT(t.measure().width == 120);
    EXPECT(t.measure().height == 40);
    EXPECT(t.get_max_points() == 64);
    EXPECT(!t.is_focusable());

    // set_series copies
    const int a[]{1, 3, 2};
    t.set_series(a, 3);
    EXPECT(t.get_count() == 3);

    // append scrolls past max_points
    t.set_max_points(4);
    EXPECT(t.get_max_points() == 4);
    for (int v = 10; v <= 14; ++v)
    {
        t.append(v);
    }
    EXPECT(t.get_count() == 4);

    // set_series over the cap also trims
    const int b[]{100, 99, 98, 97, 96, 95};
    t.set_series(b, 6);
    EXPECT(t.get_count() == 4);

    // clear
    t.clear();
    EXPECT(t.get_count() == 0);
    t.clear();
    EXPECT(t.get_count() == 0);

    // fixed y range flags
    EXPECT(!t.is_y_fixed());
    t.set_y_range(0, 10);
    EXPECT(t.is_y_fixed());
    t.set_y_auto();
    EXPECT(!t.is_y_fixed());

    // draw empty: no crash, no scribble
    std::vector<uint32_t> px(120 * 40);
    core::Graphics g(120, 40, px.data());
    t.draw(g);

    // draw a flat run: a horizontal accent line on the card
    const int flat[]{7, 7, 7, 7};
    t.set_y_range(0, 10);
    t.set_series(flat, 4);
    t.draw(g);
    int line_pixels = 0;
    for (int x = 0; x < 120; ++x)
    {
        for (int y = 12; y <= 14; ++y)
        {
            if (test::pixel_at(g, x, y) == accent.pixel)
            {
                ++line_pixels;
                break;
            }
        }
    }
    EXPECT(line_pixels > 60);  // a clear continuous run

    // the card interior is field background; the rounded corner (0,0) stays
    // the untouched buffer (zeros) because the corner is cut away
    EXPECT(test::pixel_at(g, 10, 20) == field.pixel);
    EXPECT(test::pixel_at(g, 110, 8) == field.pixel);
    EXPECT(test::pixel_at(g, 0, 0) != field.pixel);

    return test::report("trend_line");
}