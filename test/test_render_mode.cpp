#include "test.hpp"

#include <cstdio>
#include <cstring>

#include "imui.hpp"

using namespace zb::ui;
using core::Graphics;

namespace
{
    bool same_buffer(const Graphics &a, const Graphics &b, int w, int h)
    {
        return std::memcmp(a.data(), b.data(),
                           static_cast<size_t>(w) * static_cast<size_t>(h) *
                               sizeof(core::Color)) == 0;
    }

    int count_color(const Graphics &g, uint32_t pixel, int w, int h)
    {
        int n = 0;
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                if (test::pixel_at(g, x, y) == pixel)
                {
                    ++n;
                }
            }
        }
        return n;
    }

    std::string item_text(const void *, const size_t i)
    {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%zu", i);
        return buf;
    }
}  // namespace

// S-1 wireframe render mode: shape fills degrade to 1px outlines,
// everything else is untouched
int test_render_mode()
{
    using core::colors::Black;
    using core::colors::Red;
    using core::colors::White;
    using mode = Graphics::render_mode;

    // default FULL + set/get roundtrip
    {
        Graphics g(20, 20, nullptr);
        EXPECT(g.get_render_mode() == mode::full);
        g.set_render_mode(mode::wireframe);
        EXPECT(g.get_render_mode() == mode::wireframe);
        g.set_render_mode(mode::full);
        EXPECT(g.get_render_mode() == mode::full);
    }

    // FULL control: fills paint interiors
    {
        Graphics g(20, 20, nullptr);
        g.fill(White);
        g.fill_rect(2, 2, 17, 12, Red);
        EXPECT(test::pixel_at(g, 9, 7) == Red.pixel);
    }

    // wireframe rect: corners set, interior untouched
    {
        Graphics g(20, 20, nullptr);
        g.fill(White);
        g.set_render_mode(mode::wireframe);
        g.fill_rect(2, 2, 17, 12, Red);
        EXPECT(test::pixel_at(g, 2, 2) == Red.pixel);
        EXPECT(test::pixel_at(g, 17, 12) == Red.pixel);
        EXPECT(test::pixel_at(g, 9, 7) == White.pixel);
    }

    // wireframe circle/ellipse: interior empty, bones painted
    {
        Graphics g(40, 30, nullptr);
        g.fill(White);
        g.set_render_mode(mode::wireframe);
        g.fill_circle(15, 10, 6, Red);
        EXPECT(test::pixel_at(g, 15, 10) == White.pixel);
        EXPECT(count_color(g, Red.pixel, 40, 30) > 0);

        Graphics e(40, 30, nullptr);
        e.fill(White);
        e.set_render_mode(mode::wireframe);
        e.fill_ellipse(15, 10, 8, 5, Red);
        EXPECT(test::pixel_at(e, 15, 10) == White.pixel);
        EXPECT(count_color(e, Red.pixel, 40, 30) > 0);
    }

    // wireframe triangle: vertices plotted, interior empty (FULL paints it)
    {
        Graphics g(30, 26, nullptr);
        g.fill(White);
        g.set_render_mode(mode::wireframe);
        g.fill_triangle(2, 2, 27, 2, 14, 22, Red);
        EXPECT(test::pixel_at(g, 2, 2) == Red.pixel);
        EXPECT(test::pixel_at(g, 14, 8) == White.pixel);

        Graphics f(30, 26, nullptr);
        f.fill(White);
        f.fill_triangle(2, 2, 27, 2, 14, 22, Red);
        EXPECT(test::pixel_at(f, 14, 8) == Red.pixel);
    }

    // wireframe gradient/round_rect: bounds outlined, interior empty
    {
        Graphics g(20, 20, nullptr);
        g.fill(White);
        g.set_render_mode(mode::wireframe);
        g.fill_gradient(0, 0, 19, 9, Red, Black, true);
        EXPECT(test::pixel_at(g, 0, 0) == Red.pixel);  // from-outline
        EXPECT(test::pixel_at(g, 9, 4) == White.pixel);

        Graphics r(20, 20, nullptr);
        r.fill(White);
        r.set_render_mode(mode::wireframe);
        r.fill_round_rect(0, 0, 19, 9, 2, Red);
        EXPECT(test::pixel_at(r, 9, 4) == White.pixel);
        EXPECT(count_color(r, Red.pixel, 20, 20) > 0);

        Graphics f(20, 20, nullptr);
        f.fill(White);
        f.fill_round_rect(0, 0, 19, 9, 2, Red);
        EXPECT(test::pixel_at(f, 9, 4) == Red.pixel);
    }

    // strokes, images, and the fill() clear primitive are identical in
    // both modes (wireframe changes fills only)
    {
        Graphics full(30, 20, nullptr);
        Graphics wire(30, 20, nullptr);
        core::Color img[4] = {Red, Red, Red, Red};
        const auto ops = [&](Graphics &g)
        {
            g.fill(White);
            g.draw_rect(1, 1, 10, 8, Red);
            g.draw_line(0, 0, 29, 19, Black);
            g.draw_circle(20, 10, 5, Red);
            g.draw_pixel(15, 15, Black);
            g.draw_image(img, 2, 2, 2, 12, 12);
        };
        ops(full);
        wire.set_render_mode(mode::wireframe);
        ops(wire);
        EXPECT(same_buffer(full, wire, 30, 20));
    }

    // opt-in grid: dots on the spacing lattice, nothing else
    {
        Graphics g(20, 20, nullptr);
        g.fill(White);
        g.draw_wireframe_grid(4, Red);
        EXPECT(test::pixel_at(g, 0, 0) == Red.pixel);
        EXPECT(test::pixel_at(g, 4, 0) == Red.pixel);
        EXPECT(test::pixel_at(g, 0, 4) == Red.pixel);
        EXPECT(test::pixel_at(g, 8, 12) == Red.pixel);
        EXPECT(test::pixel_at(g, 1, 0) == White.pixel);
        EXPECT(test::pixel_at(g, 0, 1) == White.pixel);
        EXPECT(test::pixel_at(g, 3, 3) == White.pixel);

        Graphics z(20, 20, nullptr);
        z.fill(White);
        z.draw_wireframe_grid(0, Red);  // non-positive: no-op
        EXPECT(test::pixel_at(z, 0, 0) == White.pixel);
    }

    // sketch is reserved (S-2) and renders as FULL for now
    {
        Graphics g(20, 20, nullptr);
        g.fill(White);
        g.set_render_mode(mode::sketch);
        g.fill_rect(2, 2, 17, 12, Red);
        EXPECT(test::pixel_at(g, 9, 7) == Red.pixel);
    }

    // ListBox across a mode flip: wireframe rows bypass the image
    // cache (no cache activity, counter flat) and paint bones (white
    // face, live text); flipping back reuses the warm FULL cache
    {
        Panel root;
        root.set_size(140, 80);
        auto l = std::make_unique<ListBox>();
        l->set_row_height(16);
        l->set_size(100, 1);
        l->set_visible_rows(3);
        l->set_item_text(item_text, nullptr);
        l->set_item_count(6);
        l->set_position(10, 10);
        ListBox *list = l.get();
        root.add_child(std::move(l));

        Graphics gf(140, 80, nullptr);
        gf.fill(White);
        root.draw(gf);
        const long long full_misses = list->rasterization_count();
        EXPECT(full_misses == 3);
        // row face is solid in FULL (theme field_bg)
        EXPECT(test::pixel_at(gf, 12, 18) ==
               core::Color::from(240, 240, 240).pixel);

        Graphics gw(140, 80, nullptr);
        gw.fill(White);
        gw.set_render_mode(mode::wireframe);
        root.draw(gw);
        EXPECT(list->rasterization_count() == full_misses);  // no cache use
        EXPECT(test::pixel_at(gw, 12, 18) == White.pixel);  // face outlined
        bool saw_text = false;  // row-0 glyphs still drawn, live
        for (int y = 10; y < 26 && !saw_text; ++y)
        {
            for (int x = 10; x < 110; ++x)
            {
                if (test::pixel_at(gw, x, y) != White.pixel)
                {
                    saw_text = true;
                    break;
                }
            }
        }
        EXPECT(saw_text);

        Graphics gw2(140, 80, nullptr);
        gw2.fill(White);
        root.draw(gw2);  // back to FULL: warm cache, no new misses
        EXPECT(list->rasterization_count() == full_misses);

        EXPECT(!same_buffer(gf, gw, 140, 80));  // the mode changes output
    }

    // S-2 sketch: deterministic hand-drawn jitter. The wobble is a pure
    // function of the coordinates — two renders agree byte-for-byte,
    // a line's endpoints wobble by at most 1 px, and fills keep their
    // interior while their boundary turns uneven.
    {
        // determinism: identical calls, identical bytes
        Graphics a(60, 40, nullptr);
        Graphics b(60, 40, nullptr);
        a.fill(White);
        b.fill(White);
        a.set_render_mode(mode::sketch);
        b.set_render_mode(mode::sketch);
        a.draw_rect(5, 5, 40, 30, Black);
        b.draw_rect(5, 5, 40, 30, Black);
        a.fill_rect(10, 15, 30, 25, Black);
        b.fill_rect(10, 15, 30, 25, Black);
        EXPECT(same_buffer(a, b, 60, 40));

        // sketch differs from full for the same geometry
        Graphics f(60, 40, nullptr);
        f.fill(White);
        f.draw_rect(5, 5, 40, 30, Black);
        f.fill_rect(10, 15, 30, 25, Black);
        EXPECT(!same_buffer(a, f, 60, 40));

        // endpoint wobble: a vertical line's pixels stay within one
        // column of the true x, and the line still reaches both ends
        Graphics l(60, 40, nullptr);
        l.fill(White);
        l.set_render_mode(mode::sketch);
        l.draw_line(10, 2, 10, 30, Black);
        int min_x = 60, max_x = 0;
        for (int y = 0; y < 40; ++y)
        {
            for (int x = 0; x < 60; ++x)
            {
                if (test::pixel_at(l, x, y) == Black.pixel)
                {
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                }
            }
        }
        EXPECT(min_x >= 9 && max_x <= 11);
        // the whole line may shift by up to one pixel (both endpoints
        // may wobble the same way) but stays connected: every row of
        // the drawn span carries a pixel
        int rows_drawn = 0;
        for (int y = 0; y < 40; ++y)
        {
            bool row_has = false;
            for (int x = 0; x < 60; ++x)
            {
                if (test::pixel_at(l, x, y) == Black.pixel)
                {
                    row_has = true;
                    break;
                }
            }
            if (row_has)
            {
                ++rows_drawn;
            }
        }
        EXPECT(rows_drawn >= 28);  // 30-row line, endpoints wobble +-1

        // fill interior survives, boundary wobbles: the row extents of
        // a sketched fill_rect deviate from the exact rectangle by at
        // most one pixel on each side, and the center is solid
        Graphics r(60, 40, nullptr);
        r.fill(White);
        r.set_render_mode(mode::sketch);
        r.fill_rect(15, 10, 45, 30, Black);
        EXPECT(test::pixel_at(r, 30, 20) == Black.pixel);
        int left_most = 60, right_most = 0;
        for (int y = 10; y <= 30; ++y)
        {
            for (int x = 0; x < 60; ++x)
            {
                if (test::pixel_at(r, x, y) == Black.pixel)
                {
                    if (x < left_most) left_most = x;
                    if (x > right_most) right_most = x;
                }
            }
        }
        EXPECT(left_most >= 14 && left_most <= 15);
        EXPECT(right_most >= 45 && right_most <= 46);

        // sketch draws with the wireframe-style entry too: the mask
        // helpers (ListBox row images and the like) keep FULL behavior —
        // text and images are untouched, verified via the fill()
        // immune primitive
        Graphics m(60, 40, nullptr);
        m.fill(White);
        m.set_render_mode(mode::sketch);
        m.fill(Black);  // mode-immune clear
        EXPECT(test::pixel_at(m, 0, 0) == Black.pixel);
        EXPECT(test::pixel_at(m, 59, 39) == Black.pixel);
    }

    return test::report("render_mode");
}
