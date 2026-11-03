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

    return test::report("draw_surface");
}
