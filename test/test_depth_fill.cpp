#include "test.hpp"

#include <vector>

#include "imcore.hpp"

using namespace zb::ui;

// U-6: the optional host-owned depth plane. GL-style LESS (smaller z
// closer), per-pixel z interpolation, depth test additive to the
// offset/draw-area/damage gates.
int test_depth_fill()
{
    // without a plane the depth primitives degenerate to their siblings
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);
        g->draw_line_depth(1, 4, 0.5f, 6, 4, 0.5f, core::colors::White);
        EXPECT(test::pixel_at(*g, 1, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 6, 4) == core::colors::White.pixel);

        auto g2 = core::Graphics::make_ptr(8, 8);
        g2->fill(core::colors::Black);
        g2->fill_triangle_depth(1, 1, 0.5f, 6, 1, 0.5f, 3, 6, 0.5f, core::colors::White);
        EXPECT(test::pixel_at(*g2, 3, 3) == core::colors::White.pixel);
    }

    // attach rejects a malformed plane (init path throws)
    {
        auto g = core::Graphics::make_ptr(8, 8);
        std::vector<float> plane(16, 1.0f);
        bool threw = false;
        try
        {
            g->attach_depth_buffer(plane.data(), 4);  // stride < width
        }
        catch (const zb::ui::error &)
        {
            threw = true;
        }
        EXPECT(threw);
    }

    // occlusion: a nearer span wins first, a farther one is rejected
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);
        std::vector<float> plane(64, 1.0f);
        g->attach_depth_buffer(plane.data(), 8);

        // the wall: near (z 0.5), paints white
        g->draw_line_depth(0, 4, 0.5f, 7, 4, 0.5f, core::colors::White);
        EXPECT(test::pixel_at(*g, 3, 4) == core::colors::White.pixel);
        EXPECT(plane[4 * 8 + 3] == 0.5f);

        // behind the wall (z 0.9): rejected everywhere, color and depth
        g->draw_line_depth(0, 4, 0.9f, 7, 4, 0.9f, core::colors::Red);
        EXPECT(test::pixel_at(*g, 3, 4) == core::colors::White.pixel);
        EXPECT(plane[4 * 8 + 3] == 0.5f);

        // in front of the wall (z 0.2): overwrites color and depth
        g->draw_line_depth(2, 4, 0.2f, 5, 4, 0.2f, core::colors::Green);
        EXPECT(test::pixel_at(*g, 3, 4) == core::colors::Green.pixel);
        EXPECT(plane[4 * 8 + 3] == 0.2f);
        // the front span only reached x 2..5
        EXPECT(test::pixel_at(*g, 0, 4) == core::colors::White.pixel);
        EXPECT(plane[4 * 8 + 0] == 0.5f);
    }

    // z interpolation along a line: a linear ramp crossing a flat plane
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);
        std::vector<float> plane(64, 1.0f);
        g->attach_depth_buffer(plane.data(), 8);

        // z ramps 0.0 (near, x=0) .. 1.0 (far, x=7): the far half is
        // beyond 0.5 and loses to nothing (plane starts at 1.0) —
        // everything passes against 1.0, so instead ramp against a
        // flat 0.5 wall painted first
        g->draw_line_depth(0, 4, 0.5f, 7, 4, 0.5f, core::colors::White);
        g->draw_line_depth(0, 6, 0.0f, 7, 6, 1.0f, core::colors::Red);
        // on row 6 the plane holds 1.0: everything below passes, and
        // the far endpoint's z == 1.0 exactly — the tie is rejected
        // (contract: the plane keeps the first writer)
        EXPECT(test::pixel_at(*g, 6, 6) == core::colors::Red.pixel);
        EXPECT(plane[6 * 8 + 7] == 1.0f);

        // now paint the ramp against a fresh 0.5 plane: only the near
        // half (z < 0.5) passes
        g->detach_depth_buffer();
        auto g2 = core::Graphics::make_ptr(8, 8);
        g2->fill(core::colors::Black);
        std::vector<float> plane2(64, 0.5f);
        g2->attach_depth_buffer(plane2.data(), 8);
        g2->draw_line_depth(0, 4, 0.0f, 7, 4, 1.0f, core::colors::Red);
        EXPECT(test::pixel_at(*g2, 0, 4) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g2, 3, 4) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g2, 4, 4) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g2, 7, 4) == core::colors::Black.pixel);
        // the recorded depth shows the ramp's near values
        EXPECT(plane2[4 * 8 + 0] == 0.0f);
    }

    // triangle: per-vertex z interpolation occludes a sloped plane
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);
        std::vector<float> plane(64, 1.0f);
        g->attach_depth_buffer(plane.data(), 8);

        // a wall triangle at z 0.5
        g->fill_triangle_depth(0, 0, 0.5f, 7, 0, 0.5f, 0, 7, 0.5f, core::colors::White);
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 5, 1) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 7, 7) == core::colors::Black.pixel);  // outside the tri

        // an object triangle closer at its top, farther at its bottom:
        // the top edge (z 0.2) wins, the bottom half (z > 0.5) loses
        g->fill_triangle_depth(0, 0, 0.2f, 7, 0, 0.2f, 7, 7, 0.9f, core::colors::Green);
        EXPECT(test::pixel_at(*g, 5, 1) == core::colors::Green.pixel);
        // row y=1 sits one seventh down the 0.2->0.9 edge: z = 0.3
        EXPECT(plane[1 * 8 + 5] == 0.3f);
        // deep in the far corner nothing was overwritten
        EXPECT(test::pixel_at(*g, 1, 5) == core::colors::White.pixel);
        EXPECT(plane[5 * 8 + 1] == 0.5f);
    }

    // the depth test is additive to the damage gate: writes outside the
    // damaged region touch neither color nor depth
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);
        std::vector<float> plane(64, 1.0f);
        g->attach_depth_buffer(plane.data(), 8);
        g->set_damage(2, 2, 6, 6);  // half-open: cols/rows 2..5

        g->draw_line_depth(0, 4, 0.1f, 7, 4, 0.1f, core::colors::White);
        EXPECT(test::pixel_at(*g, 2, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 5, 4) == core::colors::White.pixel);
        EXPECT(test::pixel_at(*g, 0, 4) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 7, 4) == core::colors::Black.pixel);
        EXPECT(plane[4 * 8 + 0] == 1.0f);
        EXPECT(plane[4 * 8 + 7] == 1.0f);
        EXPECT(plane[4 * 8 + 2] == 0.1f);
    }

    // WIREFRAME: the triangle degrades to the untested outline
    {
        auto g = core::Graphics::make_ptr(8, 8);
        g->fill(core::colors::Black);
        std::vector<float> plane(64, 1.0f);
        g->attach_depth_buffer(plane.data(), 8);
        g->set_render_mode(core::Graphics::render_mode::wireframe);
        g->fill_triangle_depth(0, 0, 0.9f, 7, 0, 0.9f, 3, 6, 0.9f, core::colors::White);
        // bones draw despite being behind the 1.0 plane? no: LESS means
        // 0.9 < 1.0 passes — use a clearly-farther z to prove no depth
        // test applies in wireframe
        g->fill(core::colors::Black);
        std::fill(plane.begin(), plane.end(), 0.5f);
        g->fill_triangle_depth(0, 0, 0.9f, 7, 0, 0.9f, 3, 6, 0.9f, core::colors::White);
        // outline rows still drew: a depth-tested fill would drop them
        EXPECT(test::pixel_at(*g, 3, 0) == core::colors::White.pixel);
    }

    return test::report("depth_fill");
}
