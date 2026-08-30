#include "test.hpp"

#include "test_alloc_count.hpp"

#include "imapp.hpp"
#include "imui.hpp"

using namespace zb::app;
using namespace zb::ui;

namespace
{
    // 100x12 bar at (10,10) inside a 120x60 panel: the outline owns the
    // border pixels, the interior spans global x 11..108 / y 11..20
    struct Tree
    {
        Panel root;
        ProgressBar *bar = nullptr;

        Tree()
        {
            root.set_size(120, 60);
            auto b = std::make_unique<ProgressBar>();
            b->set_size(100, 12);
            b->set_position(10, 10);
            b->set_range(0, 100);
            bar = b.get();
            root.add_child(std::move(b));
        }
    };

    void expect_region(CanvasWindow &w,
                       const int x, const int y, const int ww, const int hh)
    {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        EXPECT(w.dirty_region(rx, ry, rw, rh));
        EXPECT(rx == x && ry == y && rw == ww && rh == hh);
    }
}

int test_progress_bar()
{
    // defaults and intrinsic size (contract: fixed 100x12)
    {
        ProgressBar b;
        EXPECT(b.get_min() == 0 && b.get_max() == 100 && b.get_value() == 0);
        EXPECT(b.measure().width == 100 && b.measure().height == 12);
    }

    // set_value clamps at both ends
    {
        ProgressBar b;
        b.set_value(150);
        EXPECT(b.get_value() == 100);
        b.set_value(-5);
        EXPECT(b.get_value() == 0);
    }

    // set_range re-clamps the existing value; a reversed range clamps
    // to a point (Slider semantics)
    {
        ProgressBar b;
        b.set_value(80);
        b.set_range(0, 50);
        EXPECT(b.get_value() == 50);
        b.set_range(30, 40);
        EXPECT(b.get_value() == 40);
        b.set_value(35);
        b.set_range(60, 10);  // reversed: max clamps to min
        EXPECT(b.get_max() == 60 && b.get_value() == 60);
    }

    // display-only: pressing on the bar neither claims the event nor
    // moves the value
    {
        Tree t;
        InputDispatcher d;
        zb::input::input_event press = {};
        press.type = zb::input::input_type::mouse_left_down;
        press.x = 60;
        press.y = 15;
        EXPECT(!d.dispatch(t.root, press));
        EXPECT(t.bar->get_value() == 0);
    }

    // repaint gate: only a real value change reports damage
    {
        CanvasWindow w;
        w.create(140, 40);
        auto b = std::make_unique<ProgressBar>();
        b->set_size(100, 12);
        b->set_position(10, 10);
        auto *bar = b.get();
        w.root().add_child(std::move(b));
        w.paint();

        bar->set_value(25);
        w.paint();
        expect_region(w, 10, 10, 100, 12);

        // feeding the current value back produces no damage
        bar->set_value(25);
        w.paint();
        expect_region(w, 0, 0, 0, 0);
        bar->set_value(200);  // clamps to 100: a real change again
        w.paint();
        expect_region(w, 10, 10, 100, 12);
    }

    // drawing: outline + proportional fill, all probes against theme
    // tokens (depth-independent)
    {
        Tree t;
        t.bar->set_value(50);
        core::Graphics g(120, 60, nullptr);
        t.root.draw(g);

        // fill_w = 50 * 98 / 100 = 49: interior x 11..59 filled
        EXPECT(theme().border.pixel == test::pixel_at(g, 10, 15));
        EXPECT(theme().border.pixel == test::pixel_at(g, 109, 15));
        EXPECT(theme().accent.pixel == test::pixel_at(g, 11, 15));
        EXPECT(theme().accent.pixel == test::pixel_at(g, 59, 15));
        EXPECT(theme().field_bg.pixel == test::pixel_at(g, 60, 15));
        EXPECT(theme().field_bg.pixel == test::pixel_at(g, 108, 15));
        // top/bottom outline rows
        EXPECT(theme().border.pixel == test::pixel_at(g, 60, 10));
        EXPECT(theme().border.pixel == test::pixel_at(g, 60, 21));
    }

    // empty and full bars
    {
        Tree t;
        core::Graphics g(120, 60, nullptr);
        t.root.draw(g);
        EXPECT(theme().field_bg.pixel == test::pixel_at(g, 11, 15));
        EXPECT(theme().field_bg.pixel == test::pixel_at(g, 108, 15));

        t.bar->set_value(100);
        core::Graphics g2(120, 60, nullptr);
        t.root.draw(g2);
        EXPECT(theme().accent.pixel == test::pixel_at(g2, 11, 15));
        EXPECT(theme().accent.pixel == test::pixel_at(g2, 108, 15));
    }

    // per-widget color overrides
    {
        Tree t;
        t.bar->set_value(100);
        t.bar->set_fill_color(core::colors::Red);
        t.bar->set_track_color(core::colors::Green);
        core::Graphics g(120, 60, nullptr);
        t.root.draw(g);
        EXPECT(core::colors::Red.pixel == test::pixel_at(g, 60, 15));
        // a degenerate fill exposes the track override: shrink the range
        // so the value maps to zero fill width
        t.bar->set_range(0, 1000);
        t.bar->set_value(5);  // fill_w = 5 * 98 / 1000 = 0
        core::Graphics g2(120, 60, nullptr);
        t.root.draw(g2);
        EXPECT(core::colors::Green.pixel == test::pixel_at(g2, 11, 15));
    }

    // materialization: fluent builder and .ui text land in the same
    // property table (no-RTTI table sync, contract 4)
    {
        FlexPanel host;
        host.set_size(200, 100);
        const auto doc = column({progress_bar(0, 200).value(42).named("pb")});
        build(host, doc);
        auto *pb = static_cast<ProgressBar *>(host.find_by_id("pb"));
        EXPECT(pb != nullptr);
        EXPECT(pb->get_min() == 0 && pb->get_max() == 200 && pb->get_value() == 42);
    }
    {
        FlexPanel host;
        host.set_size(200, 100);
        bool ok = false;
        const auto doc = parse_ui_text("progress_bar id=\"pb\" min=10 max=20 value=15", &ok);
        EXPECT(ok);
        build(host, doc);
        auto *pb = static_cast<ProgressBar *>(host.find_by_id("pb"));
        EXPECT(pb != nullptr);
        EXPECT(pb->get_min() == 10 && pb->get_max() == 20 && pb->get_value() == 15);
    }

    // zero-allocation steady state: value updates + repaints (contract 8)
    {
        CanvasWindow w;
        w.create(140, 40);
        auto b = std::make_unique<ProgressBar>();
        b->set_size(100, 12);
        b->set_position(10, 10);
        auto *bar = b.get();
        w.root().add_child(std::move(b));
        w.paint();  // consume the initial frame

        test::scoped_alloc_count c;
        for (int i = 0; i < 10; ++i)
        {
            bar->set_value(i * 10);
            w.paint();
        }
        EXPECT(c.delta() == 0);
    }

    return test::report("progress_bar");
}
