#include "test.hpp"

#include <vector>

#include "iapp.hpp"
#include "shell/input_source.hpp"
#include "shell/presentation.hpp"
#include "shell/presenter.hpp"

/*
 * A-2 shell seams, dummy-driven: the shared "what do I blit" decision
 * (region_to_present), the painted-callback dirty coalescer, the shared
 * feed step (feed_input), and the I-2a presentation-scaling math
 * (presentation_fit / to_buffer / presentation_region / the shared
 * resample loop) -- all pure, no platform needed.
 */

using zb::shell::dirty_coalescer;
using zb::shell::present_rect;
using zb::shell::region_to_present;

namespace
{
    struct ShellAppStub final : zb::app::IApp
    {
        int inputs = 0;
        int paints = 0;
        bool dirty = false;

        void create_window() override {}
        void create_window(uint32_t, uint32_t) override {}
        void create_window(uint32_t, uint32_t, void *) override {}
        zb::SharedPtr<zb::app::IWindow> window() noexcept override { return {}; }
        void input(const zb::input::input_event &ev) noexcept override
        {
            ++inputs;
            (void)ev;
        }
        void paint() override
        {
            ++paints;
            dirty = false;
        }
        [[nodiscard]] bool is_dirty() const noexcept override { return dirty; }
        void on_painting(zb::event::PAINT_EVENT::EventHandler) noexcept override {}
        void on_painted(zb::event::PAINT_EVENT::EventHandler) noexcept override {}
        void on_closing(zb::event::CLOSE_EVENT::EventHandler) noexcept override {}
        void on_closed(zb::event::CLOSE_EVENT::EventHandler) noexcept override {}
    };
}

int test_shell_presenter()
{
    // region decision: dirty region / empty dirty region / no tracking
    {
        EXPECT(region_to_present(false, 0, 0, 0, 0, 320, 240).w == 320);
        EXPECT(region_to_present(false, 0, 0, 0, 0, 320, 240).h == 240);
        const present_rect r = region_to_present(true, 5, 6, 30, 40, 320, 240);
        EXPECT(r.x == 5 && r.y == 6 && r.w == 30 && r.h == 40);
        const present_rect none = region_to_present(true, 0, 0, 0, 0, 320, 240);
        EXPECT(none.w == 0 && none.h == 0);  // a frame that drew nothing
    }

    // coalescer: regions union, empty adds are no-ops, clear resets
    {
        dirty_coalescer c;
        EXPECT(!c.valid());
        EXPECT(c.get().w == 0);  // nothing pending
        c.add(10, 10, 20, 20);
        EXPECT(c.valid());
        c.add(25, 25, 15, 15);
        present_rect u = c.get();
        EXPECT(u.x == 10 && u.y == 10 && u.w == 30 && u.h == 30);  // union
        c.add(0, 0, 0, 0);  // an empty frame must not drop pending regions
        u = c.get();
        EXPECT(u.w == 30 && u.h == 30);
        c.clear();
        EXPECT(!c.valid());
        EXPECT(c.get().w == 0);
    }

    // coalescer: a full-buffer present (no dirty tracking) covers any
    // previously accumulated region
    {
        dirty_coalescer c;
        c.add(10, 10, 20, 20);
        c.add(0, 0, 320, 240);
        const present_rect u = c.get();
        EXPECT(u.x == 0 && u.y == 0 && u.w == 320 && u.h == 240);
    }

    // feed_input: feeds the event and repaints only when a frame is owed
    {
        ShellAppStub app;
        const zb::input::input_event ev{};
        app.dirty = true;
        zb::shell::feed_input(app, ev);
        EXPECT(app.inputs == 1 && app.paints == 1);

        app.dirty = false;
        zb::shell::feed_input(app, ev);
        EXPECT(app.inputs == 2 && app.paints == 1);  // no owed frame, no paint
    }

    // --- I-2a presentation scaling ----------------------------------

    // fit: the default window size is the identity (1:1, captures
    // unchanged)
    {
        const zb::shell::presentation p = zb::shell::presentation_fit(320, 240, 320, 240);
        EXPECT(p.x == 0 && p.y == 0 && p.w == 320 && p.h == 240);
        EXPECT(p.buf_w == 320 && p.buf_h == 240);
    }

    // fit: width-constrained (wide window) -- the full width is used,
    // the height is fitted and centered vertically
    {
        const zb::shell::presentation p = zb::shell::presentation_fit(640, 640, 320, 240);
        EXPECT(p.w == 640);
        EXPECT(p.h == 480);  // 240 * 640 / 320
        EXPECT(p.x == 0 && p.y == 80);
    }

    // fit: height-constrained (narrow window) -- floor division on the
    // fitted width, centered horizontally
    {
        const zb::shell::presentation p = zb::shell::presentation_fit(400, 200, 320, 240);
        EXPECT(p.h == 200);
        EXPECT(p.w == 266);  // floor(320 * 200 / 240) = floor(266.67)
        EXPECT(p.x == 67 && p.y == 0);  // floor((400 - 266) / 2)
    }

    // fit: degenerate sizes give a zero rect (nothing is presented)
    {
        const zb::shell::presentation p = zb::shell::presentation_fit(0, 200, 320, 240);
        EXPECT(p.w == 0 && p.h == 0 && p.buf_w == 320);
    }

    // inverse map: exact at integer scale, floor elsewhere
    {
        const zb::shell::presentation p = zb::shell::presentation_fit(640, 480, 320, 240);
        int bx = -1;
        int by = -1;
        EXPECT(p.to_buffer(0, 0, bx, by));
        EXPECT(bx == 0 && by == 0);
        // dest pixel d shows buffer pixel d * buf_w / w (the stretch's
        // own sampling formula) -- the map is its exact inverse
        EXPECT(p.to_buffer(127, 63, bx, by));
        EXPECT(bx == 63 && by == 31);  // floor(127 / 2), floor(63 / 2)
        EXPECT(p.to_buffer(639, 479, bx, by));
        EXPECT(bx == 319 && by == 239);
    }

    // inverse map: the letterbox is not the app
    {
        const zb::shell::presentation p = zb::shell::presentation_fit(640, 200, 320, 240);
        // height-constrained: h = 200, w = 266, x = 187
        EXPECT(p.w == 266 && p.x == 187 && p.y == 0);
        int bx = 0;
        int by = 0;
        EXPECT(!p.to_buffer(100, 100, bx, by));   // left band
        EXPECT(!p.to_buffer(500, 100, bx, by));   // right band
        EXPECT(p.to_buffer(300, 100, bx, by));    // inside
        EXPECT(!p.to_buffer(300, -1, bx, by));    // outside the client
    }

    // inverse map: fractional scale still lands on the shown pixel
    {
        const zb::shell::presentation p = zb::shell::presentation_fit(400, 200, 320, 240);
        int bx = -1;
        int by = -1;
        EXPECT(p.to_buffer(p.x, 100, bx, by));
        EXPECT(bx == 0);
        EXPECT(p.to_buffer(p.x + p.w - 1, 100, bx, by));
        EXPECT(bx == (p.w - 1) * 320 / p.w);  // the stretch's own sampling
        EXPECT(by == (100) * 240 / 200);
    }

    // region map: integer scale is exact, fractional ceils conservatively
    {
        const zb::shell::presentation two = zb::shell::presentation_fit(640, 480, 320, 240);
        const present_rect d = zb::shell::presentation_region(two, present_rect{5, 6, 30, 40});
        EXPECT(d.x == 10 && d.y == 12 && d.w == 60 && d.h == 80);

        // dest pixel 1 shows buffer pixel 3 (3 * 100 / 33), so the
        // buffer region [3,4) maps to dest [1,2)
        const zb::shell::presentation frac = zb::shell::presentation_fit(33, 33, 100, 100);
        const present_rect f = zb::shell::presentation_region(frac, present_rect{3, 3, 1, 1});
        EXPECT(f.x == 1 && f.y == 1 && f.w == 1 && f.h == 1);

        // an empty region stays empty
        EXPECT(zb::shell::presentation_region(two, present_rect{0, 0, 0, 0}).w == 0);
    }

    // resample: the shared loop picks the same pixel the inverse map
    // returns to an input event at the same dest point (the exactness
    // the hit-testing contract rests on). Compared as Color words: the
    // resampler copies verbatim, and words are the bpp-agnostic form
    // (channel accessors expand at 16bpp, contract 3)
    {
        const zb::shell::presentation p = zb::shell::presentation_fit(64, 48, 32, 24);
        std::vector<zb::ui::core::Color> src(32 * 24);
        std::vector<zb::ui::core::Color> dst(64 * 48);
        for (int y = 0; y < 24; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                // each buffer pixel carries its coordinates in the word
                src[static_cast<size_t>(y) * 32 + x] =
                    zb::ui::core::Color::from(x, y, 0, 255);
            }
        }
        zb::shell::resample_presentation(p, present_rect{0, 0, 64, 48},
                                         src.data(), dst.data());
        int bx = -1;
        int by = -1;
        for (int dy = 0; dy < 48; ++dy)
        {
            for (int dx = 0; dx < 64; ++dx)
            {
                EXPECT(p.to_buffer(dx, dy, bx, by));
                EXPECT(dst[static_cast<size_t>(dy) * 64 + dx].pixel ==
                       src[static_cast<size_t>(by) * 32 + bx].pixel);
            }
        }
    }

    // maps_pointer: pointer types carry a position, keys never do
    {
        EXPECT(zb::shell::maps_pointer(zb::input::input_type::mouse_move));
        EXPECT(zb::shell::maps_pointer(zb::input::input_type::mouse_left_down));
        EXPECT(zb::shell::maps_pointer(zb::input::input_type::mouse_wheel));
        EXPECT(!zb::shell::maps_pointer(zb::input::input_type::key_down));
        EXPECT(!zb::shell::maps_pointer(zb::input::input_type::none));
    }

    return test::report("shell_presenter");
}
