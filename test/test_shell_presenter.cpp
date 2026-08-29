#include "test.hpp"

#include "iapp.hpp"
#include "shell/input_source.hpp"
#include "shell/presenter.hpp"

/*
 * A-2 shell seams, dummy-driven: the shared "what do I blit" decision
 * (region_to_present), the painted-callback dirty coalescer, and the
 * shared feed step (feed_input) -- all pure, no platform needed.
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

    return test::report("shell_presenter");
}
