#include "test.hpp"

#include <climits>

#include <vector>

#include "imcore.hpp"

using namespace zb::ui;

// U-8: the one-call surface blit. A consumer-owned pixel block moves in
// one call (row-wise copy) while the lib keeps the accounting: draw-area
// clamp, hard damage clip, widget-local offset. The damage assertion is
// the fps F9 failure mode: a full-screen scene blit inside a partial
// repaint must not smear undamaged pixels.
int test_draw_surface()
{
    // plain blit: an exact opaque overwrite, no blending
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);

        std::vector<core::Color> src(4, core::colors::Red);
        g->draw_surface(src.data(), 2, 2, 2, 3, 3);

        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 4, 4) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 2, 3) == core::colors::Black.pixel);  // left of it
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::Black.pixel);  // below it
    }

    // stride wider than the surface: only the first `width` pixels
    // of each source row are read
    {
        auto g = core::Graphics::make_ptr(4, 4);
        g->fill(core::colors::Black);

        std::vector<core::Color> src = {
            core::colors::Red,
            core::colors::White,
            core::colors::Green,
            core::colors::Blue,
            core::colors::White,
            core::colors::Black,
        };
        g->draw_surface(src.data(), 3, 2, 2, 0, 0);

        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 1, 0) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 0, 1) == core::colors::Blue.pixel);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 2, 0) == core::colors::Black.pixel);
    }

    // malformed views draw nothing (null, stride < width, empty)
    {
        auto g = core::Graphics::make_ptr(4, 4);
        g->fill(core::colors::Black);

        std::vector<core::Color> src(4, core::colors::Red);
        g->draw_surface(nullptr, 2, 2, 2, 0, 0);
        g->draw_surface(src.data(), 1, 2, 2, 0, 0);  // stride < width
        g->draw_surface(src.data(), 2, 0, 2, 0, 0);  // empty width
        g->draw_surface(src.data(), 2, 2, 0, 0, 0);  // empty height
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Black.pixel);
    }

    // off-surface placement clips to the draw area
    {
        auto g = core::Graphics::make_ptr(4, 4);
        g->fill(core::colors::Black);

        std::vector<core::Color> src(9, core::colors::Red);  // 3x3
        g->draw_surface(src.data(), 3, 3, 3, 2, 2);          // overruns right/bottom

        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 2, 1) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 1, 2) == core::colors::Black.pixel);
    }

    // extreme origins draw nothing (review-2 pin): the clamp must stay in
    // int64 until the intersection is proven non-empty -- a premature
    // narrow would wrap INT_MAX back negative, get rescued by the
    // draw-area clamp, and wild-pointer the row walk
    {
        auto g = core::Graphics::make_ptr(4, 4);
        g->fill(core::colors::Black);

        std::vector<core::Color> src(4, core::colors::Red);  // 2x2
        g->draw_surface(src.data(), 2, 2, 2, INT_MAX, 0);
        g->draw_surface(src.data(), 2, 2, 2, 0, INT_MAX);
        g->draw_surface(src.data(), 2, 2, 2, INT_MAX, INT_MAX);
        g->draw_surface(src.data(), 2, 2, 2, INT_MIN, INT_MIN);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Black.pixel);
    }

    // the F9 lock: damage mode hard-clips the blit — a full-surface
    // blit inside a partial repaint keeps undamaged pixels intact
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);
        g->set_damage(3, 3, 6, 6);  // half-open: rows/cols 3..5 repaint

        std::vector<core::Color> src(64, core::colors::Red);  // full 8x8
        g->draw_surface(src.data(), 8, 8, 8, 0, 0);

        // inside the damage region: overwritten
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 5, 4) == core::colors::Red.pixel);
        // outside it: previous content survives
        EXPECT(test::pixel_at(*g, 2, 4) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 4, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 6, 4) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 4, 6) == core::colors::Black.pixel);
    }

    // opaque by contract: alpha_enabled does not turn it into a blend
    {
        auto g = core::Graphics::make_ptr(4, 4);
        g->fill(core::colors::Black);
        g->enable_alpha(true);

        // 50% red over black would blend to ~127; the blit overwrites
        const core::Color translucent = core::Color::from(255, 0, 0, 128);
        std::vector<core::Color> src(16, translucent);
        g->draw_surface(src.data(), 4, 4, 4, 0, 0);

        EXPECT(test::pixel_at(*g, 2, 2) == translucent.pixel);
    }

    // widget-local coordinates resolve through the draw-area offset
    // (the public clip_surface_safe path, like Widget::draw)
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        {
            auto clip = g->clip_surface_safe(4, 4, 2, 2);
            EXPECT(static_cast<bool>(clip));
            std::vector<core::Color> src(4, core::colors::Green);
            g->draw_surface(src.data(), 2, 2, 2, 0, 0);  // widget-local (0,0)
        }

        EXPECT(test::pixel_at(*g, 4, 4) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g, 3, 4) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 4, 3) == core::colors::Black.pixel);
    }

    // ---- draw_image_scaled (FI-09): the nearest-neighbor scaled blit ----

    // pixel-exact mapping: every destination pixel equals
    // src[dx*sw/dw, dy*sh/dh], the index taken from the rectangle origin
    {
        auto palette = [](int idx)
        {
            // 4 levels per channel: distinct after 16bpp quantization too
            return core::Color::from(static_cast<uint8_t>((idx % 4) * 80),
                                     static_cast<uint8_t>(((idx / 4) % 4) * 80),
                                     static_cast<uint8_t>(((idx / 16) % 4) * 80), 255);
        };
        auto check_mapping = [&](core::Graphics &g,
                                 int sw, int sh, int dx0, int dy0, int dw, int dh)
        {
            for (int y = dy0; y < dy0 + dh; y++)
            {
                for (int x = dx0; x < dx0 + dw; x++)
                {
                    const int sxp = (x - dx0) * sw / dw;
                    const int syp = (y - dy0) * sh / dh;
                    const core::Color expected = palette(syp * sw + sxp);
                    EXPECT(test::pixel_at(g, x, y) == expected.pixel);
                }
            }
        };

        // upscale 2x3 -> 6x9 at an offset; margins stay untouched
        {
            auto g = core::Graphics::make_ptr(12, 12);
            g->fill(core::colors::Black);
            std::vector<core::Color> src;
            for (int i = 0; i < 2 * 3; i++)
                src.push_back(palette(i));
            g->draw_image_scaled(core::image_t{src.data(), 2, 3, 0}, 3, 2, 6, 9);
            check_mapping(*g, 2, 3, 3, 2, 6, 9);
            EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Black.pixel);
            EXPECT(test::pixel_at(*g, 9, 10) == core::colors::Black.pixel);
        }

        // downscale 8x6 -> 3x2
        {
            auto g = core::Graphics::make_ptr(8, 6);
            g->fill(core::colors::Black);
            std::vector<core::Color> src;
            for (int i = 0; i < 8 * 6; i++)
                src.push_back(palette(i));
            g->draw_image_scaled(core::image_t{src.data(), 8, 6, 0}, 0, 0, 3, 2);
            check_mapping(*g, 8, 6, 0, 0, 3, 2);
        }

        // 1:1 scale maps every pixel onto itself
        {
            auto g = core::Graphics::make_ptr(4, 4);
            g->fill(core::colors::Black);
            std::vector<core::Color> src;
            for (int i = 0; i < 4 * 4; i++)
                src.push_back(palette(i));
            g->draw_image_scaled(core::image_t{src.data(), 4, 4, 0}, 0, 0, 4, 4);
            check_mapping(*g, 4, 4, 0, 0, 4, 4);
        }
    }

    // malformed views and degenerate rectangles draw nothing
    {
        auto g = core::Graphics::make_ptr(4, 4);
        g->fill(core::colors::Black);

        std::vector<core::Color> src(4, core::colors::Red);
        g->draw_image_scaled(core::image_t{nullptr, 2, 2, 0}, 0, 0, 4, 4);      // null pixels
        g->draw_image_scaled(core::image_t{src.data(), 2, 2, 1}, 0, 0, 4, 4);   // stride < width
        g->draw_image_scaled(core::image_t{src.data(), 0, 2, 0}, 0, 0, 4, 4);   // zero width
        g->draw_image_scaled(core::image_t{src.data(), 2, 0, 0}, 0, 0, 4, 4);   // zero height
        g->draw_image_scaled(core::image_t{src.data(), 2, 2, 0}, 0, 0, 0, 4);   // degenerate rect w
        g->draw_image_scaled(core::image_t{src.data(), 2, 2, 0}, 0, 0, 4, 0);   // degenerate rect h
        g->draw_image_scaled(core::image_t{src.data(), 2, 2, 0}, 0, 0, -2, 4);  // negative rect w

        // extreme origins draw nothing (the draw_surface INT_MAX pin)
        g->draw_image_scaled(core::image_t{src.data(), 2, 2, 0}, INT_MAX, 0, 4, 4);
        g->draw_image_scaled(core::image_t{src.data(), 2, 2, 0}, 0, INT_MIN, 4, 4);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Black.pixel);
    }

    // two identical paints produce byte-identical pixels (determinism)
    {
        auto g = core::Graphics::make_ptr(6, 6);
        g->fill(core::colors::Black);
        std::vector<core::Color> src = {
            core::colors::Red, core::colors::Green,
            core::colors::Blue, core::colors::White};
        g->draw_image_scaled(core::image_t{src.data(), 2, 2, 0}, 1, 1, 4, 4);
        std::vector<uint32_t> snapshot;
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 6; x++)
                snapshot.push_back(test::pixel_at(*g, x, y));

        g->fill(core::colors::Black);
        g->draw_image_scaled(core::image_t{src.data(), 2, 2, 0}, 1, 1, 4, 4);
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 6; x++)
                EXPECT(test::pixel_at(*g, x, y) == snapshot[static_cast<size_t>(y) * 6 + static_cast<size_t>(x)]);
    }

    // the F9 lock for the scaled blit: a damage-clipped partial repaint
    // samples from the unclipped rectangle origin, so the damaged region
    // matches the full repaint pixel for pixel (contract §9 determinism)
    {
        auto g_full = core::Graphics::make_ptr(8, 8);
        g_full->fill(core::colors::Black);
        std::vector<core::Color> src;
        for (int i = 0; i < 2 * 4; i++)
            src.push_back(i % 2 ? core::colors::Red : core::colors::Green);
        g_full->draw_image_scaled(core::image_t{src.data(), 2, 4, 0}, 2, 1, 6, 7);

        auto g_part = core::Graphics::make_ptr(8, 8);
        g_part->fill(core::colors::Black);
        g_part->set_damage(4, 3, 7, 6);  // half-open: cols 4..6, rows 3..5
        g_part->draw_image_scaled(core::image_t{src.data(), 2, 4, 0}, 2, 1, 6, 7);

        for (int y = 0; y < 8; y++)
        {
            for (int x = 0; x < 8; x++)
            {
                const bool inside = x >= 4 && x < 7 && y >= 3 && y < 6;
                if (inside)
                    EXPECT(test::pixel_at(*g_part, x, y) == test::pixel_at(*g_full, x, y));
                else
                    EXPECT(test::pixel_at(*g_part, x, y) == core::colors::Black.pixel);
            }
        }
    }

    // blending follows draw_image: alpha_enabled with an opaque source
    // still overwrites (contract §9: an opaque source overwrites)
    {
        auto g = core::Graphics::make_ptr(4, 4);
        g->fill(core::colors::Black);
        g->enable_alpha(true);

        std::vector<core::Color> src(9, core::colors::Red);  // 3x3
        g->draw_image_scaled(core::image_t{src.data(), 3, 3, 0}, 0, 0, 4, 4);
        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);
    }

    // off-surface placement clips to the draw area
    {
        auto g = core::Graphics::make_ptr(4, 4);
        g->fill(core::colors::Black);

        std::vector<core::Color> src(9, core::colors::Red);                    // 3x3
        g->draw_image_scaled(core::image_t{src.data(), 3, 3, 0}, 2, 2, 6, 6);  // overruns right/bottom
        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 2, 1) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 1, 2) == core::colors::Black.pixel);
    }

    // widget-local coordinates resolve through the draw-area offset
    {
        auto g = core::Graphics::make_ptr(10, 10);
        g->fill(core::colors::Black);
        {
            auto clip = g->clip_surface_safe(4, 4, 2, 2);
            EXPECT(static_cast<bool>(clip));
            std::vector<core::Color> src(4, core::colors::Green);
            g->draw_image_scaled(core::image_t{src.data(), 2, 2, 0}, 0, 0, 2, 2);  // widget-local (0,0)
        }

        EXPECT(test::pixel_at(*g, 4, 4) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g, 3, 4) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 4, 3) == core::colors::Black.pixel);
    }

    // ---- TJ-NEW: Graphics::draw_polyline_aa (code-contract §9) ----

    // width-3 capsule band, horizontal: the band spans y 2.5..5.5, so the
    // fully covered rows are 3..4 (row 2/5 carry a half-coverage fringe
    // the exact assertions stay off), rows 1/6/7 are clear; round caps
    // reach x 1.5..11.5, so columns 2..10 are solid on the band
    {
        const core::impoint_t h[] = {{3, 4}, {10, 4}};
        auto g = core::Graphics::make_ptr(14, 9);
        g->fill(core::colors::Black);
        g->draw_polyline_aa(h, 2, 3, core::colors::Red);

        for (int x = 2; x <= 10; ++x)
        {
            EXPECT(test::pixel_at(*g, x, 3) == core::colors::Red.pixel);
            EXPECT(test::pixel_at(*g, x, 4) == core::colors::Red.pixel);
            EXPECT(test::pixel_at(*g, x, 1) == core::colors::Black.pixel);
            EXPECT(test::pixel_at(*g, x, 6) == core::colors::Black.pixel);
            EXPECT(test::pixel_at(*g, x, 7) == core::colors::Black.pixel);
        }
        EXPECT(test::pixel_at(*g, 0, 4) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 13, 4) == core::colors::Black.pixel);
    }

    // width-3 capsule band, vertical: columns 3..4 solid, 1/6/7 clear
    {
        const core::impoint_t v[] = {{4, 3}, {4, 10}};
        auto g = core::Graphics::make_ptr(9, 14);
        g->fill(core::colors::Black);
        g->draw_polyline_aa(v, 2, 3, core::colors::Red);

        for (int y = 2; y <= 10; ++y)
        {
            EXPECT(test::pixel_at(*g, 3, y) == core::colors::Red.pixel);
            EXPECT(test::pixel_at(*g, 4, y) == core::colors::Red.pixel);
            EXPECT(test::pixel_at(*g, 1, y) == core::colors::Black.pixel);
            EXPECT(test::pixel_at(*g, 6, y) == core::colors::Black.pixel);
            EXPECT(test::pixel_at(*g, 7, y) == core::colors::Black.pixel);
        }
        EXPECT(test::pixel_at(*g, 4, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 4, 13) == core::colors::Black.pixel);
    }

    // 45-degree segment: on-line pixels are solid, pixels a full
    // radius-and-a-half away are untouched
    {
        const core::impoint_t d[] = {{2, 2}, {9, 9}};
        auto g = core::Graphics::make_ptr(12, 12);
        g->fill(core::colors::Black);
        g->draw_polyline_aa(d, 2, 3, core::colors::Red);

        EXPECT(test::pixel_at(*g, 4, 4) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 6, 6) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 5, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 2, 5) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 6, 3) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 6) == core::colors::Black.pixel);
    }

    // joints single-write: the capsule union (min distance over the
    // segments) plots a fully covered joint pixel exactly once, so a
    // translucent L's corner equals the single-blend value of a straight
    // interior pixel (a double blend would stack the alpha)
    {
        const core::Color ink = core::Color::from(0xFF, 0xFF, 0xFF, 0x80);
        const core::impoint_t el[] = {{3, 3}, {10, 3}, {10, 10}};
        auto g = core::Graphics::make_ptr(14, 14);
        g->fill(core::colors::Black);
        g->draw_polyline_aa(el, 3, 4, ink);

        const core::impoint_t ref[] = {{3, 3}, {10, 3}};
        auto g_ref = core::Graphics::make_ptr(14, 14);
        g_ref->fill(core::colors::Black);
        g_ref->draw_polyline_aa(ref, 2, 4, ink);

        const uint32_t single = test::pixel_at(*g_ref, 6, 3);
        EXPECT(test::pixel_at(*g, 10, 3) == single);
        EXPECT(test::pixel_at(*g, 9, 3) == single);
        EXPECT(test::pixel_at(*g, 10, 4) == single);
        EXPECT(test::pixel_at(*g, 9, 4) == single);
    }

    // width 1 is the draw_line_aa chain shape, byte for byte: the first
    // segment whole, every later one skip_first (solid endpoints, joints
    // not double-blended)
    {
        const core::impoint_t path[] = {
            {2, 8}, {8, 8}, {8, 2}, {12, 6}, {12, 6}, {5, 6}};
        auto g_chain = core::Graphics::make_ptr(16, 12);
        g_chain->fill(core::colors::Black);
        g_chain->draw_line_aa(path[0].x, path[0].y, path[1].x, path[1].y,
                              core::colors::Red);
        for (int i = 1; i + 1 < 6; ++i)
        {
            g_chain->draw_line_aa(path[i].x, path[i].y, path[i + 1].x,
                                  path[i + 1].y, core::colors::Red, true);
        }

        auto g_poly = core::Graphics::make_ptr(16, 12);
        g_poly->fill(core::colors::Black);
        g_poly->draw_polyline_aa(path, 6, 1, core::colors::Red);

        for (int y = 0; y < 12; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                EXPECT(test::pixel_at(*g_poly, x, y) ==
                       test::pixel_at(*g_chain, x, y));
            }
        }
    }

    // the F9 lock for the stroke: a damage-clipped partial repaint
    // matches the full repaint inside the region and keeps the previous
    // content outside it, byte for byte over the whole frame
    {
        const core::impoint_t path[] = {{1, 1}, {12, 6}};
        auto g_full = core::Graphics::make_ptr(14, 8);
        g_full->fill(core::colors::Black);
        g_full->draw_polyline_aa(path, 2, 2, core::colors::Red);

        auto g_part = core::Graphics::make_ptr(14, 8);
        g_part->fill(core::colors::Black);
        g_part->set_damage(6, 0, 14, 8);  // half-open: cols 6..13
        g_part->draw_polyline_aa(path, 2, 2, core::colors::Red);

        for (int y = 0; y < 8; ++y)
        {
            for (int x = 0; x < 14; ++x)
            {
                if (x >= 6)
                {
                    EXPECT(test::pixel_at(*g_part, x, y) ==
                           test::pixel_at(*g_full, x, y));
                }
                else
                {
                    EXPECT(test::pixel_at(*g_part, x, y) ==
                           core::colors::Black.pixel);
                }
            }
        }
    }

    // widget-local stroke coordinates resolve through the draw-area offset
    {
        auto g = core::Graphics::make_ptr(12, 12);
        g->fill(core::colors::Black);
        {
            auto clip = g->clip_surface_safe(4, 4, 4, 4);
            EXPECT(static_cast<bool>(clip));
            const core::impoint_t path[] = {{0, 1}, {3, 1}};
            g->draw_polyline_aa(path, 2, 1, core::colors::Green);
        }

        EXPECT(test::pixel_at(*g, 4, 5) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g, 7, 5) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g, 4, 4) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 5) == core::colors::Black.pixel);
    }

    // degenerate inputs draw nothing
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);
        const core::impoint_t p[] = {{2, 2}, {5, 5}};
        g->draw_polyline_aa(nullptr, 2, 3, core::colors::Red);  // null points
        g->draw_polyline_aa(p, 1, 3, core::colors::Red);        // fewer than two
        g->draw_polyline_aa(p, 0, 3, core::colors::Red);        // empty
        g->draw_polyline_aa(p, -2, 3, core::colors::Red);       // negative count
        g->draw_polyline_aa(p, 2, 0, core::colors::Red);        // zero width
        g->draw_polyline_aa(p, 2, -3, core::colors::Red);       // negative width
        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 7, 7) == core::colors::Black.pixel);
    }

    // two identical paints produce byte-identical pixels (determinism)
    {
        const core::impoint_t path[] = {{2, 2}, {6, 7}, {11, 3}};
        auto g = core::Graphics::make_ptr(14, 10);
        g->fill(core::colors::Black);
        g->draw_polyline_aa(path, 3, 2, core::colors::Green);
        std::vector<uint32_t> snapshot;
        for (int y = 0; y < 10; ++y)
        {
            for (int x = 0; x < 14; ++x)
            {
                snapshot.push_back(test::pixel_at(*g, x, y));
            }
        }

        g->fill(core::colors::Black);
        g->draw_polyline_aa(path, 3, 2, core::colors::Green);
        for (int y = 0; y < 10; ++y)
        {
            for (int x = 0; x < 14; ++x)
            {
                EXPECT(test::pixel_at(*g, x, y) ==
                       snapshot[static_cast<size_t>(y) * 14 +
                                static_cast<size_t>(x)]);
            }
        }
    }

    return test::report("draw_surface");
}
