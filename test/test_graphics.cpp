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

    // fill_rect: a plain rect fills the interior and leaves the outside
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->fill_rect(3, 3, 6, 5, core::colors::White);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 6, 5) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 2, 4) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 4) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 2) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 6) != core::colors::White.pixel);
    }

    // fill_rect: a single-row rect (y1 == y2) fills exactly that row and
    // returns -- the old descending row loop iterated forever here
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->fill_rect(2, 4, 7, 4, core::colors::White);
        EXPECT(test::pixel_at(*g, 2, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 1, 4) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 8, 4) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 3) != core::colors::White.pixel);  // row above
        EXPECT(test::pixel_at(*g, 4, 5) != core::colors::White.pixel);  // row below
    }

    // fill_rect: corners in reverse row order fill the same span
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->fill_rect(2, 6, 7, 2, core::colors::White);
        EXPECT(test::pixel_at(*g, 4, 2) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 6) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 1) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 7) != core::colors::White.pixel);
    }

    // 16bpp: the single alpha bit is binary opacity (Z2 core half) -- a
    // set bit paints the foreground, a clear bit leaves the backdrop
    // (the 8-bit blend math would treat the bit as 1/255 and make every
    // covered pixel nearly transparent)
    if (core::ImColor_Depth == 16)
    {
        auto g = core::Graphics::make_ptr(4, 4);
        g->fill(core::colors::Black);
        g->enable_alpha(true);
        core::Color cover = core::colors::White;
        cover.rgb.a = 255;  // collapses to the single alpha bit
        g->draw_pixel(1, 1, cover);
        core::Color clear = core::colors::White;
        clear.rgb.a = 0;
        g->draw_pixel(2, 2, clear);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Black.pixel);
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
        // outside both guards: untouched by the clipped fills, still the
        // background Black (a literal 0 here broke when Black gained a
        // real alpha -- the fill writes the full pixel)
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 16, 16) == core::colors::Black.pixel);
    }

    // clip_safe: an off-screen area yields an invalid guard (no-op)
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        auto guard = g->clip_safe(20, 20, 5, 5);
        EXPECT(!guard);
    }

    // the constructor rejects empty and overflow-sized surfaces (F3):
    // the pixel count is validated in 64 bits -- 65536x65536 used to
    // wrap to zero, allocate nothing and still report a full-size draw
    // area, overflowing the heap on the first fill/draw
    {
        bool threw = false;
        try
        {
            auto g = core::Graphics::make_ptr(0, 10);
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);
        threw = false;
        try
        {
            auto g = core::Graphics::make_ptr(10, 0);
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);
        threw = false;
        try
        {
            auto g = core::Graphics::make_ptr(65536, 65536);
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);
        auto ok = core::Graphics::make_ptr(1920, 1080);
        EXPECT(ok->size().width == 1920 && ok->size().height == 1080);
    }

    // clone rejects an area whose bounds check used to wrap: 2^30 + 2^30
    // went negative and passed (out-of-bounds read)
    {
        auto g = core::Graphics::make_ptr(10, 10);
        bool threw = false;
        try
        {
            g->clone(1 << 30, 0, 1 << 30, 5);
        }
        catch (const std::exception &)
        {
            threw = true;
        }
        EXPECT(threw);
    }

    // clip_safe: a child hanging off the left/top of its parent's clip
    // has that part cut away, not translated into view (F4) -- the clip
    // bounds are the intersection, the local-coordinate origin stays
    // the requested one
    {
        auto g = core::Graphics::make_ptr(100, 100);
        g->fill(core::colors::Black);
        {
            auto parent = g->clip_safe(30, 10, 70, 90);  // abs [30..99]x[10..99]
            EXPECT(static_cast<bool>(parent));
            {
                // child at relative (-20, 0): requested abs x [10..59]
                auto child = g->clip_safe(-20, 0, 50, 50);
                EXPECT(static_cast<bool>(child));
                g->draw_pixel(0, 0, core::colors::White);   // abs (10,10): clipped away
                g->draw_pixel(30, 0, core::colors::White);  // abs (40,10): in place
                g->draw_pixel(30, 10, core::colors::White); // abs (40,20): in place
            }
            g->draw_pixel(0, 0, core::colors::Red);  // parent restored: abs (30,10)
        }
        EXPECT(test::pixel_at(*g, 10, 10) != core::colors::White.pixel);  // cut away
        EXPECT(test::pixel_at(*g, 40, 10) == core::colors::White.pixel);  // not shifted
        EXPECT(test::pixel_at(*g, 40, 20) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 30, 10) == core::colors::Red.pixel);
    }

    return test::report("graphics");
}
