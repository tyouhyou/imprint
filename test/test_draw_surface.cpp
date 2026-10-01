#include "test.hpp"

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
            core::colors::Red, core::colors::White, core::colors::Green,
            core::colors::Blue, core::colors::White, core::colors::Black,
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

    return test::report("draw_surface");
}
