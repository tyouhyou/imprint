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
        cover.set_a(255);  // collapses to the single alpha bit
        g->draw_pixel(1, 1, cover);
        core::Color clear = core::colors::White;
        clear.set_a(0);
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

    // fill_gradient (vertical): both ends exact, middle interpolated,
    // every column of the span covered
    {
        auto g = core::Graphics::make_ptr(4, 9);
        g->fill(core::colors::Black);
        g->fill_gradient(0, 0, 3, 8, core::colors::Black, core::colors::White, false);
        EXPECT(test::pixel_at(*g, 1, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 1, 8) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 1, 4) == core::Color::from(127, 127, 127).pixel);  // (0*4 + 255*4) / 8
        EXPECT(test::pixel_at(*g, 0, 4) == test::pixel_at(*g, 3, 4));  // constant per row
    }

    // fill_gradient (horizontal): the default direction interpolates
    // along columns, rows stay constant
    {
        auto g = core::Graphics::make_ptr(9, 4);
        g->fill(core::colors::Black);
        g->fill_gradient(0, 0, 8, 3, core::colors::Black, core::colors::White);
        EXPECT(test::pixel_at(*g, 0, 1) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 8, 1) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 1) == core::Color::from(127, 127, 127).pixel);
        EXPECT(test::pixel_at(*g, 2, 0) == test::pixel_at(*g, 2, 3));  // constant per column
    }

    // fill_gradient: a reversed corner order and a single-column span
    // (degenerate to the flat `from` color) both behave
    {
        auto g = core::Graphics::make_ptr(6, 6);
        g->fill(core::colors::Black);
        g->fill_gradient(5, 3, 2, 3, core::colors::White, core::colors::White, false);
        EXPECT(test::pixel_at(*g, 2, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 5, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 2) != core::colors::White.pixel);
        g->fill(core::colors::Black);
        g->fill_gradient(2, 0, 2, 5, core::colors::White, core::colors::Black, false);
        EXPECT(test::pixel_at(*g, 2, 0) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 1, 0) != core::colors::White.pixel);
    }

    // fill_gradient respects clip_safe like every other raster path
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        {
            auto guard = g->clip_safe(2, 2, 5, 5);
            EXPECT(static_cast<bool>(guard));
            g->fill_gradient(0, 0, 9, 9, core::colors::White, core::colors::White, false);
        }
        EXPECT(test::pixel_at(*g, 1, 4) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 1) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 6, 6) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 7) != core::colors::White.pixel);
    }

    // fill_round_rect: corner pixels are rounded away, the flat edges
    // and the interior are full; the arc chord matches the geometry
    {
        auto g = core::Graphics::make_ptr(16, 16);
        g->fill(core::colors::Black);
        g->fill_round_rect(2, 2, 13, 13, 4, core::colors::White);
        EXPECT(test::pixel_at(*g, 2, 2) != core::colors::White.pixel);   // corner cut
        EXPECT(test::pixel_at(*g, 13, 2) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 2, 13) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 13, 13) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 2) == core::colors::White.pixel);   // flat top edge
        EXPECT(test::pixel_at(*g, 7, 13) == core::colors::White.pixel);  // flat bottom edge
        EXPECT(test::pixel_at(*g, 2, 7) == core::colors::White.pixel);   // flat left edge
        EXPECT(test::pixel_at(*g, 13, 7) == core::colors::White.pixel);  // flat right edge
        EXPECT(test::pixel_at(*g, 7, 7) == core::colors::White.pixel);   // interior
        // row 4 sits dy=2 above the top-left arc center (6,6): the chord
        // half-width is floor(sqrt(16-4)) = 3, so columns 3..9 fill
        EXPECT(test::pixel_at(*g, 4, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 3, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 2, 4) != core::colors::White.pixel);
    }

    // fill_round_rect: radius 0 is the plain fill, an oversized radius
    // clamps to the shorter half-side
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->fill_round_rect(2, 2, 7, 7, 0, core::colors::White);
        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 7) == core::colors::White.pixel);
        g->fill(core::colors::Black);
        g->fill_round_rect(1, 1, 8, 6, 100, core::colors::White);  // clamps to r = 2
        EXPECT(test::pixel_at(*g, 1, 1) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 1) == core::colors::White.pixel);
    }

    // draw_round_rect: the outline traces the fill boundary -- straight
    // edges, arc points at the chord ends, hollow center and corners
    {
        auto g = core::Graphics::make_ptr(16, 16);
        g->fill(core::colors::Black);
        g->draw_round_rect(2, 2, 13, 13, 4, core::colors::White);
        EXPECT(test::pixel_at(*g, 7, 2) == core::colors::White.pixel);    // top edge
        EXPECT(test::pixel_at(*g, 7, 13) == core::colors::White.pixel);   // bottom edge
        EXPECT(test::pixel_at(*g, 2, 7) == core::colors::White.pixel);    // left edge
        EXPECT(test::pixel_at(*g, 13, 7) == core::colors::White.pixel);   // right edge
        EXPECT(test::pixel_at(*g, 3, 5) == core::colors::White.pixel);    // arc (dy=1)
        EXPECT(test::pixel_at(*g, 12, 5) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 3) == core::colors::White.pixel);    // arc (dy=3)
        EXPECT(test::pixel_at(*g, 11, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 7) != core::colors::White.pixel);    // hollow center
        EXPECT(test::pixel_at(*g, 2, 2) != core::colors::White.pixel);    // no corner
    }

    // tinted draw_image: an opaque-white tint is the exact plain
    // draw_image identity on every depth (the *255/255 modulate
    // collapses back through the same setters)
    {
        auto g = core::Graphics::make_ptr(6, 2);
        auto plain = core::Graphics::make_ptr(6, 2);
        g->fill(core::colors::Black);
        plain->fill(core::colors::Black);
        g->enable_alpha(false);
        plain->enable_alpha(false);
        const core::Color src[2] = {core::colors::White, core::Color::from(255, 128, 0, 200)};
        g->draw_image(src, 2, 1, 2, 0, 0, core::Color::from(255, 255, 255));
        plain->draw_image(src, 2, 1, 2, 0, 0);
        for (int x = 0; x < 2; ++x)
        {
            EXPECT(test::pixel_at(*g, x, 0) == test::pixel_at(*plain, x, 0));
        }
    }

    // tinted draw_image scales channels by the tint. Exact pixels are
    // asserted at 32bpp only: at 16bpp the modulated value requantizes
    // through the 5/6/5-bit setters (128 expands to 123, which collapses
    // to a different bit pattern than a direct 128), so the useful
    // 16bpp contract is the white-tint identity above
    if (core::ImColor_Depth == 32)
    {
        auto g = core::Graphics::make_ptr(6, 2);
        g->fill(core::colors::Black);
        g->enable_alpha(false);
        const core::Color src[2] = {core::colors::White, core::Color::from(255, 128, 0, 200)};
        g->draw_image(src, 2, 1, 2, 0, 0, core::Color::from(128, 255, 64));
        EXPECT(test::pixel_at(*g, 0, 0) == core::Color::from(128, 255, 64).pixel);
        EXPECT(test::pixel_at(*g, 1, 0) == core::Color::from(128, 128, 0, 200).pixel);
    }

    // tinted draw_image under alpha_enabled: the modulated source alpha
    // flows into the source-over blend (32bpp per-channel blend only --
    // at 16bpp the alpha bit is binary and the blend returns the front)
    if (core::ImColor_Depth == 32)
    {
        auto g = core::Graphics::make_ptr(6, 2);
        g->fill(core::colors::Red);
        g->enable_alpha(true);
        const core::Color white[1] = {core::colors::White};
        g->draw_image(white, 1, 1, 1, 4, 1, core::Color::from(255, 255, 255, 128));
        // white at tint-alpha 128 over red: source-over gives (255,128,128)
        EXPECT(test::pixel_at(*g, 4, 1) == core::Color::from(255, 128, 128).pixel);
    }

    return test::report("graphics");
}
