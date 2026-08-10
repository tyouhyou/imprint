#include "test.hpp"

#include "imcore.hpp"

using namespace zb::ui;

int test_graphics()
{
    // draw_circle: the octant scan still covers the axis extremes
    {
        auto g = core::Graphics::make_ptr(12, 12);
        g->fill(core::colors::Black);
        g->draw_circle(5, 5, 3, core::colors::White);
        EXPECT(test::pixel_at(*g, 5, 2) == core::colors::White.pixel);  // top
        EXPECT(test::pixel_at(*g, 8, 5) == core::colors::White.pixel);  // right
        EXPECT(test::pixel_at(*g, 5, 5) != core::colors::White.pixel);  // hollow center
    }

    // fill_circle: the same axis extremes are filled
    {
        auto g = core::Graphics::make_ptr(12, 12);
        g->fill(core::colors::Black);
        g->fill_circle(5, 5, 3, core::colors::White);
        EXPECT(test::pixel_at(*g, 5, 2) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::White.pixel);  // filled center
    }

    // fill_ellipse: the scanline fill covers the interior and the boundary
    {
        auto g = core::Graphics::make_ptr(21, 21);
        g->fill(core::colors::Black);
        g->fill_ellipse(10, 10, 5, 3, core::colors::White);
        EXPECT(test::pixel_at(*g, 10, 10) == core::colors::White.pixel);  // center
        EXPECT(test::pixel_at(*g, 15, 10) == core::colors::White.pixel);  // right extreme
        EXPECT(test::pixel_at(*g, 13, 12) == core::colors::White.pixel);  // inside
        EXPECT(test::pixel_at(*g, 10, 13) == core::colors::White.pixel);  // bottom extreme
        EXPECT(test::pixel_at(*g, 14, 12) != core::colors::White.pixel);  // outside
        EXPECT(test::pixel_at(*g, 16, 10) != core::colors::White.pixel);  // outside
    }

    // a degenerate ellipse draws nothing
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->fill_ellipse(5, 5, 0, 3, core::colors::White);
        EXPECT(test::pixel_at(*g, 5, 5) != core::colors::White.pixel);
    }

    // fill_triangle: the scanline fill covers the interior, the middle
    // vertex row and the flat-bottom case
    {
        auto g = core::Graphics::make_ptr(21, 21);
        g->fill(core::colors::Black);
        g->fill_triangle(0, 0, 10, 5, 5, 10, core::colors::White);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::White.pixel);  // top vertex
        EXPECT(test::pixel_at(*g, 3, 4) == core::colors::White.pixel);  // interior (left of the a-c edge)
        EXPECT(test::pixel_at(*g, 7, 4) == core::colors::White.pixel);  // interior (right of the a-b edge)
        EXPECT(test::pixel_at(*g, 2, 5) == core::colors::White.pixel);  // middle vertex row, long edge side
        EXPECT(test::pixel_at(*g, 10, 5) == core::colors::White.pixel); // middle vertex row, short edge side
        EXPECT(test::pixel_at(*g, 5, 10) == core::colors::White.pixel); // bottom vertex
        EXPECT(test::pixel_at(*g, 11, 5) != core::colors::White.pixel); // outside
        EXPECT(test::pixel_at(*g, 5, 2) != core::colors::White.pixel);  // outside (right of the a-b edge)
    }
    {
        auto g = core::Graphics::make_ptr(16, 11);
        g->fill(core::colors::Black);
        g->fill_triangle(0, 10, 5, 0, 10, 10, core::colors::White); // flat bottom, base 0..10
        EXPECT(test::pixel_at(*g, 5, 0) == core::colors::White.pixel);  // apex
        EXPECT(test::pixel_at(*g, 0, 10) == core::colors::White.pixel); // base row filled
        EXPECT(test::pixel_at(*g, 5, 10) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 10, 10) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 9) == core::colors::White.pixel);  // interior near the base
        EXPECT(test::pixel_at(*g, 9, 9) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 5) == core::colors::White.pixel);  // interior mid-height
        EXPECT(test::pixel_at(*g, 2, 2) != core::colors::White.pixel);  // outside
        EXPECT(test::pixel_at(*g, 8, 2) != core::colors::White.pixel);  // outside
    }
    // a degenerate triangle draws nothing
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->fill_triangle(3, 3, 4, 3, 5, 3, core::colors::White); // one row
        EXPECT(test::pixel_at(*g, 3, 3) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 3) != core::colors::White.pixel);
    }

    // clip_safe: restricts drawing, restores the state on scope exit
    {
        auto g = core::Graphics::make_ptr(20, 20);
        g->fill(core::colors::Black);
        {
            auto guard = g->clip_safe(5, 5, 10, 10);
            EXPECT(static_cast<bool>(guard));
            g->fill(core::colors::White);  // fills only 5..15
        }
        EXPECT(test::pixel_at(*g, 4, 4) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 14, 14) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 15, 15) != core::colors::White.pixel);
        g->fill(core::colors::Red);  // state restored: full surface again
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 19, 19) == core::colors::Red.pixel);
    }

    // clip_safe: nested guards restore in the right order. A point that
    // was clipped by the inner guard draws again once it is restored
    {
        auto g = core::Graphics::make_ptr(20, 20);
        g->fill(core::colors::Black);
        {
            auto outer = g->clip_safe(5, 5, 10, 10);  // absolute 5..15
            g->fill(core::colors::Blue);
            {
                auto inner = g->clip_safe(0, 0, 2, 2);  // absolute 5..7
                g->fill(core::colors::White);
                g->draw_pixel(3, 3, core::colors::Red);  // clipped: outside 2x2
            }
            g->draw_pixel(3, 3, core::colors::Red);  // restored: inside the outer area
        }
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::White.pixel);  // inner fill kept
        EXPECT(test::pixel_at(*g, 6, 6) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 8, 8) == core::colors::Red.pixel);    // inner restored
        EXPECT(test::pixel_at(*g, 5, 14) == core::colors::Blue.pixel);  // outer area (5..14)
        EXPECT(test::pixel_at(*g, 0, 0) == 0);                          // outside both
        EXPECT(test::pixel_at(*g, 16, 16) == 0);
    }

    // clip_safe: an off-screen area yields an invalid guard (no-op)
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        auto guard = g->clip_safe(20, 20, 5, 5);
        EXPECT(!guard);
    }

    return test::report("graphics");
}
