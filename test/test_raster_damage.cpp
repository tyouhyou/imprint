#include "test.hpp"

#include <vector>

#include "imcore.hpp"

using namespace zb::ui;

// A-12 / A-13: direct Graphics probes under damage mode. The normal
// paint path (CanvasWindow::paint -> clip_safe) intersects the draw
// area with the damage rect, masking the rasterizer's own clip; these
// calls bypass clip_safe so the hard clip is tested on its own.
// Convention: the damage rect is half-open, draw_area is inclusive.
int test_raster_damage()
{
    // draw_pixel: the exclusive damage edge rejects; the inclusive
    // edge and the interior write
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->set_damage(2, 2, 6, 6);
        g->draw_pixel(2, 3, core::colors::White);  // inclusive left edge
        g->draw_pixel(3, 2, core::colors::White);  // inclusive top edge
        g->draw_pixel(6, 3, core::colors::White);  // x == r: exclusive, rejected
        g->draw_pixel(3, 6, core::colors::White);  // y == b: exclusive, rejected
        g->draw_pixel(1, 3, core::colors::White);  // outside left
        g->draw_pixel(3, 1, core::colors::White);  // outside top
        EXPECT(test::pixel_at(*g, 2, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 3, 2) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 6, 3) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 6) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 1, 3) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 1) == core::colors::Black.pixel);
    }

    // fill: clipped to the intersection of draw area and damage;
    // nothing written outside (the old clamp leaked the exclusive edge)
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->set_damage(2, 2, 6, 6);
        g->fill(core::colors::White);
        for (int y = 2; y < 6; ++y)
        {
            for (int x = 2; x < 6; ++x)
            {
                EXPECT(test::pixel_at(*g, x, y) == core::colors::White.pixel);
            }
        }
        EXPECT(test::pixel_at(*g, 6, 3) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 6) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 1, 3) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 1) == core::colors::Black.pixel);
    }

    // fill: a draw area that does not intersect the damage rect is a
    // no-op. The old one-sided clamp produced start > end and fed a
    // negative width to fill_n (A-12 underflow / OOB write)
    {
        std::vector<core::Color> buf(8 * 8, core::colors::Black);
        core::Graphics g(8, 8, buf.data());
        g.set_damage(4, 4, 6, 6);
        {
            auto guard = g.clip_safe(0, 0, 2, 2);
            EXPECT(static_cast<bool>(guard));
            g.fill(core::colors::White);
        }
        bool untouched = true;
        for (const auto &px : buf)
        {
            if (px.pixel != core::colors::Black.pixel)
            {
                untouched = false;
            }
        }
        EXPECT(untouched);
    }

    // an empty (degenerate) damage rect rejects every write
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);
        g->set_damage(4, 4, 4, 4);
        g->fill(core::colors::White);
        g->draw_pixel(4, 4, core::colors::White);
        EXPECT(test::pixel_at(*g, 4, 4) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Black.pixel);
    }

    // draw_image delegates to draw_pixel, so the same clip applies
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->set_damage(2, 2, 6, 6);
        core::Color row[4] = {core::colors::White, core::colors::White,
                              core::colors::White, core::colors::White};
        g->draw_image(row, 4, 1, 4, 3, 3);  // spans x = 3..6 at y = 3
        EXPECT(test::pixel_at(*g, 5, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 6, 3) == core::colors::Black.pixel);  // x == r
    }

    return test::report("raster_damage");
}
