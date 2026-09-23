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

    // draw_line_aa: axis-aligned runs fall back to the exact plain line
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->draw_line_aa(2, 4, 7, 4, core::colors::White);
        for (int x = 2; x <= 7; ++x)
        {
            EXPECT(test::pixel_at(*g, x, 4) == core::colors::White.pixel);
        }
        EXPECT(test::pixel_at(*g, 4, 3) != core::colors::White.pixel);
    }

    // AA always blends regardless of the alpha_enabled switch
    if (core::ImColor_Depth == 32)
    {
        // shallow line: endpoints solid, each body column splits its
        // coverage between the two nearest pixels (Wu pair); column 4 of
        // (0,0)-(7,3): ideal y = 12/7, remainder 5 -> frac8 = 182
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->draw_line_aa(0, 0, 7, 3, core::colors::White);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 1) == core::Color::from(73, 73, 73).pixel);
        EXPECT(test::pixel_at(*g, 4, 2) == core::Color::from(182, 182, 182).pixel);
        EXPECT(test::pixel_at(*g, 4, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 4, 3) == core::colors::Black.pixel);

        // steep line: the transposed walk mirrors the coverage exactly
        auto gs = core::Graphics::make_ptr(10, 10);
        gs->fill(core::colors::Black);
        gs->draw_line_aa(0, 0, 3, 7, core::colors::White);
        EXPECT(test::pixel_at(*gs, 0, 0) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*gs, 3, 7) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*gs, 1, 4) == core::Color::from(73, 73, 73).pixel);
        EXPECT(test::pixel_at(*gs, 2, 4) == core::Color::from(182, 182, 182).pixel);

        // circle: exact axis extremes (integer chords), coverage pair on
        // the non-integer chords (px=4 of r=7: py=5, rem=8 -> frac8=185),
        // interior and one-past-the-boundary untouched
        auto gc = core::Graphics::make_ptr(21, 21);
        gc->fill(core::colors::Black);
        gc->draw_circle_aa(10, 10, 7, core::colors::White);
        EXPECT(test::pixel_at(*gc, 17, 10) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*gc, 10, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*gc, 18, 10) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*gc, 10, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*gc, 10, 10) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*gc, 14, 15) == core::Color::from(70, 70, 70).pixel);
        EXPECT(test::pixel_at(*gc, 14, 16) == core::Color::from(185, 185, 185).pixel);

        // the AA write path respects clip_safe like every raster entry
        {
            auto gk = core::Graphics::make_ptr(10, 10);
            gk->fill(core::colors::Black);
            {
                auto guard = gk->clip_safe(0, 0, 4, 10);
                EXPECT(static_cast<bool>(guard));
                gk->draw_line_aa(0, 0, 7, 3, core::colors::White);
            }
            EXPECT(test::pixel_at(*gk, 0, 0) == core::colors::White.pixel);
            EXPECT(test::pixel_at(*gk, 7, 3) != core::colors::White.pixel);  // endpoint clipped
            EXPECT(test::pixel_at(*gk, 2, 1) == core::Color::from(218, 218, 218).pixel);
            EXPECT(test::pixel_at(*gk, 5, 2) == core::colors::Black.pixel);
        }
    }

    // 16bpp: binary coverage -- exactly one pixel of each Wu pair plots
    // (the half-coverage threshold), endpoints stay solid
    if (core::ImColor_Depth == 16)
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        g->draw_line_aa(0, 0, 7, 3, core::colors::White);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 3) == core::colors::White.pixel);
        const bool lower = test::pixel_at(*g, 4, 1) == core::colors::White.pixel;
        const bool upper = test::pixel_at(*g, 4, 2) == core::colors::White.pixel;
        EXPECT(lower != upper);
        EXPECT(test::pixel_at(*g, 4, 0) != core::colors::White.pixel);
    }

    // fill_circle_aa: solid interior and axis extremes, fractional edge
    // pixels on the other rows (r=7: row +4 has half=5, rem=8, frac=185,
    // so (4,14)/(16,14) blend to 185 while 5..15 stay solid)
    {
        auto g = core::Graphics::make_ptr(21, 21);
        g->fill(core::colors::Black);
        g->fill_circle_aa(10, 10, 7, core::colors::White);
        EXPECT(test::pixel_at(*g, 10, 10) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 17, 10) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 3, 10) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 10, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 10, 17) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 10, 14) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 18, 10) != core::colors::White.pixel);
        if (core::ImColor_Depth == 32)
        {
            EXPECT(test::pixel_at(*g, 4, 14) == core::Color::from(185, 185, 185).pixel);
            EXPECT(test::pixel_at(*g, 16, 14) == core::Color::from(185, 185, 185).pixel);
            EXPECT(test::pixel_at(*g, 5, 14) == core::colors::White.pixel);
        }
        // quadrants mirror exactly (no double-plotted blend stacking)
        bool symmetric = true;
        for (int y = 0; y < 21 && symmetric; ++y)
        {
            for (int x = 0; x < 21; ++x)
            {
                symmetric = symmetric &&
                    (test::pixel_at(*g, x, y) == test::pixel_at(*g, 20 - x, y)) &&
                    (test::pixel_at(*g, x, y) == test::pixel_at(*g, x, 20 - y));
            }
        }
        EXPECT(symmetric);
        // two separate draws are identical (deterministic, no stacking:
        // each pixel plots once per call)
        auto g2 = core::Graphics::make_ptr(21, 21);
        g2->fill(core::colors::Black);
        g2->fill_circle_aa(10, 10, 7, core::colors::White);
        bool same = true;
        for (int y = 0; y < 21 && same; ++y)
        {
            for (int x = 0; x < 21; ++x)
            {
                same = same && (test::pixel_at(*g, x, y) == test::pixel_at(*g2, x, y));
            }
        }
        EXPECT(same);
    }

    // fill_circle_aa at 16bpp: the 78-coverage fringe stays out while
    // the 145-coverage one plots (row +3 vs +6)
    if (core::ImColor_Depth == 16)
    {
        auto g = core::Graphics::make_ptr(21, 21);
        g->fill(core::colors::Black);
        g->fill_circle_aa(10, 10, 7, core::colors::White);
        EXPECT(test::pixel_at(*g, 10, 10) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 4, 13) == core::colors::White.pixel);   // solid span
        EXPECT(test::pixel_at(*g, 3, 13) != core::colors::White.pixel);   // fringe skipped
        EXPECT(test::pixel_at(*g, 6, 16) == core::colors::White.pixel);   // fringe plotted
    }

    // fill_round_rect_aa: solid middle spans and straight edges, blended
    // corner fringes (r=4 rect 1,1,20,10: row 2 has dy=3, frac=153)
    {
        auto g = core::Graphics::make_ptr(22, 12);
        g->fill(core::colors::Black);
        g->fill_round_rect_aa(1, 1, 20, 10, 4, core::colors::White);
        EXPECT(test::pixel_at(*g, 11, 5) == core::colors::White.pixel);   // middle span
        EXPECT(test::pixel_at(*g, 11, 1) == core::colors::White.pixel);   // top edge
        EXPECT(test::pixel_at(*g, 5, 1) == core::colors::White.pixel);    // edge start
        EXPECT(test::pixel_at(*g, 1, 1) != core::colors::White.pixel);    // corner cut
        EXPECT(test::pixel_at(*g, 3, 2) == core::colors::White.pixel);    // corner span
        if (core::ImColor_Depth == 32)
        {
            // the corner boundary lands exactly on the pixel-center
            // grid here (continuous chord at 1/4 px), so the first cut
            // pixels are fully covered — no faint fringe anymore
            EXPECT(test::pixel_at(*g, 2, 2) == core::colors::White.pixel);
            EXPECT(test::pixel_at(*g, 19, 2) == core::colors::White.pixel);
            EXPECT(test::pixel_at(*g, 1, 2) != core::colors::White.pixel);
        }
        // zero radius is the plain fill_rect
        auto gz = core::Graphics::make_ptr(22, 12);
        gz->fill(core::colors::Black);
        gz->fill_round_rect_aa(1, 1, 20, 10, 0, core::colors::White);
        auto gr = core::Graphics::make_ptr(22, 12);
        gr->fill(core::colors::Black);
        gr->fill_rect(1, 1, 20, 10, core::colors::White);
        bool same = true;
        for (int y = 0; y < 12 && same; ++y)
        {
            for (int x = 0; x < 22; ++x)
            {
                same = same && (test::pixel_at(*gz, x, y) == test::pixel_at(*gr, x, y));
            }
        }
        EXPECT(same);
    }

    // fill_round_rect_aa at 16bpp: the continuous quarter-px chord
    // pulls row 3's span to x=1 (its center lands on the boundary) and
    // row 4's with it; the zero-coverage outside pixel stays dark
    if (core::ImColor_Depth == 16)
    {
        auto g = core::Graphics::make_ptr(22, 12);
        g->fill(core::colors::Black);
        g->fill_round_rect_aa(1, 1, 20, 10, 4, core::colors::White);
        EXPECT(test::pixel_at(*g, 2, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 1, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 1, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 0, 3) != core::colors::White.pixel);
    }

    // draw_round_rect_aa: solid edge extremes and tangents (arc sample
    // points, path-neutral), a hollow middle, a populated corner band,
    // and zero radius falling back to draw_rect. Corner blend values
    // are trig-path dependent, so the band is locked by count range
    // (11 nominal at r=8), not by value.
    {
        auto g = core::Graphics::make_ptr(34, 24);
        g->fill(core::colors::Black);
        g->draw_round_rect_aa(1, 1, 32, 22, 8, core::colors::White);
        EXPECT(test::pixel_at(*g, 16, 1) == core::colors::White.pixel);   // top edge
        EXPECT(test::pixel_at(*g, 9, 1) == core::colors::White.pixel);    // corner tangent
        EXPECT(test::pixel_at(*g, 1, 9) == core::colors::White.pixel);    // corner tangent
        EXPECT(test::pixel_at(*g, 16, 11) != core::colors::White.pixel);  // hollow
        if (core::ImColor_Depth == 32)
        {
            int band = 0;
            for (int y = 0; y <= 8; ++y)
            {
                for (int x = 0; x <= 8; ++x)
                {
                    if (test::pixel_at(*g, x, y) != core::colors::Black.pixel)
                    {
                        ++band;
                    }
                }
            }
            EXPECT(band >= 7 && band <= 15);
        }
        // redrawing on a fresh surface matches exactly (deterministic)
        auto g2 = core::Graphics::make_ptr(34, 24);
        g2->fill(core::colors::Black);
        g2->draw_round_rect_aa(1, 1, 32, 22, 8, core::colors::White);
        bool same2 = true;
        for (int y = 0; y < 24 && same2; ++y)
        {
            for (int x = 0; x < 34; ++x)
            {
                same2 = same2 && (test::pixel_at(*g, x, y) == test::pixel_at(*g2, x, y));
            }
        }
        EXPECT(same2);
        auto gz = core::Graphics::make_ptr(34, 24);
        gz->fill(core::colors::Black);
        gz->draw_round_rect_aa(1, 1, 32, 22, 0, core::colors::White);
        auto gr = core::Graphics::make_ptr(34, 24);
        gr->fill(core::colors::Black);
        gr->draw_rect(1, 1, 32, 22, core::colors::White);
        bool same = true;
        for (int y = 0; y < 24 && same; ++y)
        {
            for (int x = 0; x < 34; ++x)
            {
                same = same && (test::pixel_at(*gz, x, y) == test::pixel_at(*gr, x, y));
            }
        }
        EXPECT(same);
    }

    // wireframe: the AA fills degrade to their AA outlines
    {
        auto gf = core::Graphics::make_ptr(22, 12);
        auto go = core::Graphics::make_ptr(22, 12);
        gf->fill(core::colors::Black);
        go->fill(core::colors::Black);
        gf->set_render_mode(core::Graphics::render_mode::wireframe);
        gf->fill_circle_aa(11, 6, 5, core::colors::White);
        go->draw_circle_aa(11, 6, 5, core::colors::White);
        bool same = true;
        for (int y = 0; y < 12 && same; ++y)
        {
            for (int x = 0; x < 22; ++x)
            {
                same = same && (test::pixel_at(*gf, x, y) == test::pixel_at(*go, x, y));
            }
        }
        EXPECT(same);
        auto rf = core::Graphics::make_ptr(22, 12);
        auto ro = core::Graphics::make_ptr(22, 12);
        rf->fill(core::colors::Black);
        ro->fill(core::colors::Black);
        rf->set_render_mode(core::Graphics::render_mode::wireframe);
        rf->fill_round_rect_aa(1, 1, 20, 10, 4, core::colors::White);
        ro->draw_round_rect_aa(1, 1, 20, 10, 4, core::colors::White);
        same = true;
        for (int y = 0; y < 12 && same; ++y)
        {
            for (int x = 0; x < 22; ++x)
            {
                same = same && (test::pixel_at(*rf, x, y) == test::pixel_at(*ro, x, y));
            }
        }
        EXPECT(same);
    }

    // draw_arc_aa: shape, endpoints and sample points are path-neutral
    // (both trig paths agree there); the interior and the unswept half
    // stay untouched. Angles are the math convention measured from +x;
    // on the raster's screen coordinates (y down) a positive sweep runs
    // visually clockwise, so 0..180 sweeps the lower half.
    {
        auto g = core::Graphics::make_ptr(21, 21);
        g->fill(core::colors::Black);
        g->draw_arc_aa(10, 10, 7, 0, 180, core::colors::White);
        EXPECT(test::pixel_at(*g, 17, 10) == core::colors::White.pixel);  // 0deg endpoint
        EXPECT(test::pixel_at(*g, 3, 10) == core::colors::White.pixel);   // 180deg endpoint
        EXPECT(test::pixel_at(*g, 10, 17) == core::colors::White.pixel);  // 90deg sample (lower)
        EXPECT(test::pixel_at(*g, 15, 15) == core::colors::White.pixel);  // 45deg sample
        EXPECT(test::pixel_at(*g, 10, 10) != core::colors::White.pixel);  // hollow center
        EXPECT(test::pixel_at(*g, 10, 3) != core::colors::White.pixel);   // 270deg not swept
        EXPECT(test::pixel_at(*g, 18, 10) != core::colors::White.pixel);  // outside
    }

    // negative sweep walks the same pixels as its positive mirror
    {
        auto gcw = core::Graphics::make_ptr(21, 21);
        auto gccw = core::Graphics::make_ptr(21, 21);
        gcw->fill(core::colors::Black);
        gccw->fill(core::colors::Black);
        gcw->draw_arc_aa(10, 10, 7, 0, 180, core::colors::White);
        gccw->draw_arc_aa(10, 10, 7, 180, -180, core::colors::White);
        bool same = true;
        for (int y = 0; y < 21; ++y)
        {
            for (int x = 0; x < 21; ++x)
            {
                same = same && (test::pixel_at(*gcw, x, y) == test::pixel_at(*gccw, x, y));
            }
        }
        EXPECT(same);
    }

    // full circle relocates to draw_circle_aa: multi-turn and negative
    // sweeps converge to the exact-chord ring
    {
        auto ga = core::Graphics::make_ptr(21, 21);
        auto gc = core::Graphics::make_ptr(21, 21);
        ga->fill(core::colors::Black);
        gc->fill(core::colors::Black);
        ga->draw_arc_aa(10, 10, 7, 0, 360, core::colors::White);
        gc->draw_circle_aa(10, 10, 7, core::colors::White);
        bool same = true;
        for (int y = 0; y < 21; ++y)
        {
            for (int x = 0; x < 21; ++x)
            {
                same = same && (test::pixel_at(*ga, x, y) == test::pixel_at(*gc, x, y));
            }
        }
        EXPECT(same);
        ga->draw_arc_aa(10, 10, 7, 100, -720, core::colors::White);  // still the ring, no seam
        EXPECT(test::pixel_at(*ga, 10, 3) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*ga, 10, 17) == core::colors::White.pixel);
    }

    // degenerate: zero sweep draws nothing, radius 0 plots the center
    {
        auto g = core::Graphics::make_ptr(21, 21);
        g->fill(core::colors::Black);
        g->draw_arc_aa(10, 10, 7, 0, 0, core::colors::White);
        EXPECT(test::pixel_at(*g, 17, 10) != core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 10, 10) != core::colors::White.pixel);
        g->draw_arc_aa(10, 10, 0, 30, 90, core::colors::White);
        EXPECT(test::pixel_at(*g, 10, 10) == core::colors::White.pixel);
    }

    // the arc's write path respects clip_safe like every raster entry
    {
        auto gk = core::Graphics::make_ptr(21, 21);
        gk->fill(core::colors::Black);
        {
            auto guard = gk->clip_safe(0, 0, 11, 21);
            EXPECT(static_cast<bool>(guard));
            gk->draw_arc_aa(10, 10, 7, 0, 180, core::colors::White);
        }
        EXPECT(test::pixel_at(*gk, 17, 10) != core::colors::White.pixel);  // 0deg endpoint clipped
        EXPECT(test::pixel_at(*gk, 10, 17) == core::colors::White.pixel);  // 90deg endpoint visible
        EXPECT(test::pixel_at(*gk, 3, 10) == core::colors::White.pixel);   // 180deg endpoint visible
    }

    // fill_radial (P-1): center pixel is `from`, the farthest corner is
    // `to`, midpoints interpolate; square corners stay painted
    {
        auto g = core::Graphics::make_ptr(21, 21);
        g->fill(core::colors::Black);
        // 20x20 box, center (10,10): farthest corner dist = sqrt(200)
        g->fill_radial(0, 0, 20, 20, 10, 10, core::colors::White, 0,
                       core::colors::Black, 100);
        EXPECT(test::pixel_at(*g, 10, 10) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 20, 20) == core::colors::Black.pixel);
        // halfway to the corner (dist sqrt(50), pct 50): mid gray
        EXPECT(test::pixel_at(*g, 5, 5) == core::Color::from(127, 127, 127).pixel);
        EXPECT(test::pixel_at(*g, 15, 15) == core::Color::from(127, 127, 127).pixel);
    }

    // fill_radial: stop offsets rescale the ramp (outside clamps), the
    // center follows cx/cy
    {
        auto g = core::Graphics::make_ptr(21, 11);
        g->fill(core::colors::Black);
        // center (5,5), 0%..50%: the 50% ring is already `to`
        g->fill_radial(0, 0, 20, 10, 5, 5, core::colors::White, 0,
                       core::colors::Black, 50);
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::White.pixel);
        // farthest corner from (5,5) is (20,10): dist sqrt(250); a pixel
        // at pct >= 50 clamps to `to`
        EXPECT(test::pixel_at(*g, 20, 10) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 15, 5) == core::colors::Black.pixel);
    }

    // fill_radial with corner radius: corners cut, interior gradient kept
    {
        auto g = core::Graphics::make_ptr(22, 22);
        g->fill(core::colors::Black);
        g->fill_radial(1, 1, 20, 20, 10, 10, core::colors::White, 0,
                       core::colors::Black, 100, 5);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Black.pixel);  // cut
        EXPECT(test::pixel_at(*g, 10, 10) != core::colors::Black.pixel);  // face kept
        EXPECT(test::pixel_at(*g, 10, 1) != core::colors::Black.pixel);  // flat edge kept
    }

    // fill_conic (P-2b): 41x41 box, center (20,20), stops 0 black /
    // 180 white / 360 black. Axis pixels read exact angles in both
    // trig paths (off-boundary interiors may differ by the 1-degree
    // integer-path tolerance, so only axes assert here)
    {
        auto g = core::Graphics::make_ptr(41, 41);
        const int degs[3] = {0, 180, 360};
        const core::Color cols[3] = {core::colors::Black,
                                     core::colors::White,
                                     core::colors::Black};
        g->fill(core::colors::Black);
        g->fill_conic(0, 0, 40, 40, 0, degs, cols, 3);
        EXPECT(test::pixel_at(*g, 20, 0) == core::colors::Black.pixel);  // up: 0
        EXPECT(test::pixel_at(*g, 20, 40) == core::colors::White.pixel);  // down: 180
        EXPECT(test::pixel_at(*g, 20, 20) == core::colors::Black.pixel);  // center: 0
        // left/right sit halfway down their segments: (0*90+255*90)/180
        EXPECT(test::pixel_at(*g, 40, 20) == core::Color::from(127, 127, 127).pixel);
        EXPECT(test::pixel_at(*g, 0, 20) == core::Color::from(127, 127, 127).pixel);
        // the `from` origin rotates the sweep: from 90 puts gray up top
        g->fill(core::colors::Black);
        g->fill_conic(0, 0, 40, 40, 90, degs, cols, 3);
        EXPECT(test::pixel_at(*g, 20, 0) == core::Color::from(127, 127, 127).pixel);
        EXPECT(test::pixel_at(*g, 20, 40) == core::Color::from(127, 127, 127).pixel);
    }

    // fill_conic with corner radius: corners cut, sweep face kept
    {
        auto g = core::Graphics::make_ptr(42, 42);
        const int degs[2] = {0, 360};
        const core::Color cols[2] = {core::colors::White,
                                     core::colors::White};
        g->fill(core::colors::Black);
        g->fill_conic(1, 1, 40, 40, 0, degs, cols, 2, 10);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Black.pixel);  // cut
        EXPECT(test::pixel_at(*g, 20, 20) == core::colors::White.pixel);  // face
    }

    // fill_gradient3 (P-2c): ends exact, the mid column/row reads mid
    // exactly, quarters lerp their half
    {
        auto g = core::Graphics::make_ptr(41, 41);
        g->fill(core::colors::Black);
        g->fill_gradient3(0, 0, 40, 40, core::colors::Black,
                          core::colors::Red, 50, core::colors::White, true);
        EXPECT(test::pixel_at(*g, 0, 20) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 40, 20) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 20, 20) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 10, 20) == core::Color::from(127, 0, 0).pixel);
        EXPECT(test::pixel_at(*g, 30, 20) == core::Color::from(255, 127, 127).pixel);
        // vertical: the mid row is constant across the span
        g->fill(core::colors::Black);
        g->fill_gradient3(0, 0, 40, 40, core::colors::Black,
                          core::colors::Red, 50, core::colors::White, false);
        EXPECT(test::pixel_at(*g, 20, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 20, 40) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 20, 20) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 0, 20) == test::pixel_at(*g, 40, 20));
    }

    // fill_linear_stops (N-stop): ends exact, every stop reads
    // exactly at its percent row, spans lerp straight
    {
        auto g = core::Graphics::make_ptr(5, 101);
        const int pos[5] = {0, 25, 50, 75, 100};
        const core::Color cols[5] = {core::colors::Black, core::colors::Red,
                                     core::colors::Green, core::colors::Blue,
                                     core::colors::White};
        g->fill(core::colors::Black);
        g->fill_linear_stops(0, 0, 4, 100, pos, cols, 5, false);
        EXPECT(test::pixel_at(*g, 2, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 2, 100) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 2, 25) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 2, 50) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g, 2, 75) == core::colors::Blue.pixel);
#if COLOR_DEPTH == 32
        // tp=12 in Black->Red (span 25): (0*13 + 255*12) / 25 = 122
        EXPECT(test::pixel_at(*g, 2, 12) == core::Color::from(122, 0, 0).pixel);
        // tp=37 in Red->Green: r = 255*13/25 = 132, g = 255*12/25 = 122
        EXPECT(test::pixel_at(*g, 2, 37) == core::Color::from(132, 122, 0).pixel);
#else
        // 16bpp lerps through the quantized endpoints (248), shifting
        // interior buckets down one: r = 248*12/25 = 119, g = 248*12/25
        EXPECT(test::pixel_at(*g, 2, 12) == core::Color::from(112, 0, 0).pixel);
        EXPECT(test::pixel_at(*g, 2, 37) == core::Color::from(128, 112, 0).pixel);
#endif
        EXPECT(test::pixel_at(*g, 0, 37) == test::pixel_at(*g, 4, 37));
        // horizontal with corner radius: mid rows span, corners cut
        auto g2 = core::Graphics::make_ptr(41, 41);
        g2->fill(core::colors::Black);
        g2->fill_linear_stops(0, 0, 40, 40, pos, cols, 5, true, 10);
        EXPECT(test::pixel_at(*g2, 0, 20) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g2, 40, 20) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g2, 0, 0) == core::colors::Black.pixel);  // cut
    }

    // fill_repeating (P-2c): period-6 white[0,2]/black[2,6] stripes,
    // hard stops flat, period wraps
    {
        auto g = core::Graphics::make_ptr(30, 6);
        const int pos[4] = {0, 2, 2, 6};
        const core::Color cols[4] = {core::colors::White,
                                     core::colors::White,
                                     core::colors::Black,
                                     core::colors::Black};
        g->fill(core::colors::Black);
        g->fill_repeating(0, 0, 29, 5, true, 6, pos, cols, 4);
        EXPECT(test::pixel_at(*g, 0, 2) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 1, 2) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 5, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 6, 2) == core::colors::White.pixel);  // wrap
        EXPECT(test::pixel_at(*g, 8, 2) == core::colors::Black.pixel);
        // vertical: stripes run across rows instead
        auto g2 = core::Graphics::make_ptr(6, 30);
        g2->fill(core::colors::Black);
        g2->fill_repeating(0, 0, 5, 29, false, 6, pos, cols, 4);
        EXPECT(test::pixel_at(*g2, 2, 0) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g2, 2, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g2, 2, 6) == core::colors::White.pixel);
    }

    // fill_repeating: faint stops follow the depth's alpha rule —
    // true blend on 32bpp, the binary any-bit-set rule on 16bpp
    {
        auto g = core::Graphics::make_ptr(12, 2);
        const int pos[2] = {0, 4};
        const core::Color cols[2] = {core::Color::from(255, 255, 255, 10),
                                     core::Color::from(255, 255, 255, 10)};
        g->fill(core::colors::Black);
        g->enable_alpha(true);
        g->fill_repeating(0, 0, 11, 1, true, 4, pos, cols, 2);
        g->enable_alpha(false);
#if COLOR_DEPTH == 32
        EXPECT(test::pixel_at(*g, 0, 0) == core::Color::from(10, 10, 10).pixel);
#else
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::White.pixel);
#endif
    }

    // fill_repeating with first stop > 0: the wrap segment (seg < 0)
    // must lerp from the previous period's tail without shifting m
    // (a second m += period made the weight pass 1 into garbage colors)
    {
        auto g = core::Graphics::make_ptr(8, 1);
        // period 8, stops at 4 (red) and 6 (blue): m in [0,4) wraps
        // from the previous period's last stop (blue at 6-8=-2) to red at 4
        const int pos[4] = {4, 4, 6, 6};
        const core::Color cols[4] = {core::colors::Red, core::colors::Red,
                                     core::colors::Blue, core::colors::Blue};
        g->fill(core::colors::Black);
        g->fill_repeating(0, 0, 7, 0, true, 8, pos, cols, 4);
        // m=0..3: seg < 0, lerp(blue_tail, red, m-(-2), 4-(-2))
        // m=0 → weight 2/6; m=4..5 solid red; m=6..7 solid blue
        EXPECT(test::pixel_at(*g, 4, 0) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 6, 0) == core::colors::Blue.pixel);
        // wrap head is a blend of blue and red, not pure black / pure red
        const uint32_t wrap = test::pixel_at(*g, 0, 0);
        EXPECT(wrap != core::colors::Black.pixel);
        EXPECT(wrap != core::colors::Red.pixel);
        EXPECT(wrap != core::colors::Blue.pixel);
#if COLOR_DEPTH == 32
        // weight 2/6 from blue toward red: r = 255*2/6 ≈ 85, b = 255*4/6 ≈ 170
        EXPECT(wrap == core::Color::from(85, 0, 170).pixel);
#endif
    }

    // fill_gradient with corner radius: middle rows interpolate
    // full-width, corner rows shrink by the chord
    {
        auto g = core::Graphics::make_ptr(12, 12);
        g->fill(core::colors::Black);
        g->fill_gradient(1, 1, 10, 10, core::colors::Black, core::colors::White,
                         false, 3);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Black.pixel);  // cut
        EXPECT(test::pixel_at(*g, 5, 5) != core::colors::Black.pixel);  // face
        EXPECT(test::pixel_at(*g, 5, 2) != core::colors::Black.pixel);  // chord span
        EXPECT(test::pixel_at(*g, 0, 2) == core::colors::Black.pixel);  // chord cut
        EXPECT(test::pixel_at(*g, 0, 9) == core::colors::Black.pixel);  // cut
#if COLOR_DEPTH == 32
        // the AA fringe kisses the first cut column (same formula as
        // fill_round_rect_aa); at 16bpp coverage quantizes and the
        // fringe pixel stays clear
        EXPECT(test::pixel_at(*g, 1, 2) != core::colors::Black.pixel);
#endif
    }

    return test::report("graphics");
}
