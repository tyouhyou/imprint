#include "test.hpp"

#include "imapp.hpp"
#include "imui.hpp"

using namespace zb::app;
using namespace zb::ui;

namespace
{
    // a fresh window whose tree contains one widget at (10,10) sized
    // 60x20, parked (layout done, damage consumed by one paint call)
    struct rig
    {
        zb::app::CanvasWindow w;
        Widget *widget = nullptr;

        rig(const bool use_slider = false)
        {
            w.create(200, 80);
            auto b = use_slider ? std::unique_ptr<Widget>(new Slider())
                                : std::unique_ptr<Widget>(new Button());
            b->set_size(60, 20);
            b->set_position(10, 10);
            widget = b.get();
            w.root().add_child(std::move(b));
            w.paint();
        }
    };

    void expect_region(zb::app::CanvasWindow &w,
                       const int x, const int y, const int ww, const int hh)
    {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        EXPECT(w.dirty_region(rx, ry, rw, rh));
        EXPECT(rx == x && ry == y && rw == ww && rh == hh);
    }

    // counts how many times the rasterizer touched it
    struct DrawProbe : public Widget
    {
        mutable int draws = 0;
        void draw_at(zb::ui::core::Graphics &) const override { ++draws; }
    };
}

// damage reporting: setters mark their widget, paint() aggregates the
// union into one rect, and the region is consumed afterwards (batch C)
int test_dirty()
{
    // initial frame and idle frames
    {
        zb::app::CanvasWindow w;
        w.create(200, 80);
        w.paint();
        // the creation requested a frame: the whole buffer is new
        expect_region(w, 0, 0, 200, 80);

        // an idle repaint (never owed) reports an empty region
        w.paint();
        expect_region(w, 0, 0, 0, 0);
    }

    // geometry setters report old UNION new bounds
    {
        zb::app::CanvasWindow w;
        w.create(200, 80);
        auto b = std::make_unique<Button>();
        b->set_size(60, 20);
        b->set_position(10, 10);
        auto *btn = b.get();
        w.root().add_child(std::move(b));
        // the first paint requested a frame: the whole buffer (the new
        // child also parked damage at the origin, it is subsumed)
        w.paint();
        expect_region(w, 0, 0, 200, 80);

        btn->set_position(50, 5);
        w.paint();
        // old (10,10,60,20) union new (50,5,60,20)
        expect_region(w, 10, 5, 100, 25);
    }

    // base setters report the widget bounds
    {
        rig r;
        r.widget->set_text("changed");
        r.w.paint();
        expect_region(r.w, 10, 10, 60, 20);

        r.widget->set_background_color(core::colors::Red);
        r.w.paint();
        expect_region(r.w, 10, 10, 60, 20);

        r.widget->set_visible(false);
        r.w.paint();
        expect_region(r.w, 10, 10, 60, 20);

        r.widget->set_visible(true);
        r.w.paint();
        expect_region(r.w, 10, 10, 60, 20);
    }

    // a click press/release pair on a button
    {
        rig r;
        zb::ui::InputDispatcher d;
        zb::input::input_event down;
        down.type = zb::input::input_type::mouse_left_down;
        down.x = 20;
        down.y = 20;
        EXPECT(d.dispatch(r.w.root(), down));
        r.w.paint();
        expect_region(r.w, 10, 10, 60, 20);

        zb::input::input_event up;
        up.type = zb::input::input_type::mouse_left_up;
        up.x = 20;
        up.y = 20;
        EXPECT(d.dispatch(r.w.root(), up));
        r.w.paint();
        expect_region(r.w, 10, 10, 60, 20);
    }

    // slider: value change via setter
    {
        rig r(true);
        auto *sld = static_cast<Slider *>(r.widget);
        sld->set_value(80);
        r.w.paint();
        expect_region(r.w, 10, 10, 60, 20);
    }

    // radio group: selecting one unchecks the siblings, and every
    // involved widget reports damage (the union covers both)
    {
        zb::app::CanvasWindow w;
        w.create(200, 80);
        auto r1 = std::make_unique<RadioButton>();
        auto r2 = std::make_unique<RadioButton>();
        r1->set_group(1);
        r2->set_group(1);
        r1->set_size(40, 20);
        r1->set_position(10, 10);
        r2->set_size(40, 20);
        r2->set_position(60, 10);
        auto *a = r1.get();
        auto *b2 = r2.get();
        w.root().add_child(std::move(r1));
        w.root().add_child(std::move(r2));
        w.paint();

        static_cast<RadioButton *>(a)->set_checked(true);
        w.paint();
        // only the chosen radio changed (the sibling was already off)
        expect_region(w, 10, 10, 40, 20);

        static_cast<RadioButton *>(b2)->set_checked(true);
        w.paint();
        // the new selection damages itself, the sibling its uncheck
        expect_region(w, 10, 10, 90, 20);
    }

    // list box: selection and scroll
    {
        zb::app::CanvasWindow w;
        w.create(200, 80);
        auto lb = std::make_unique<ListBox>();
        lb->set_visible_rows(3);
        lb->set_item_count(10);
        lb->set_size(100, 48);
        lb->set_position(10, 10);
        auto *box = lb.get();
        w.root().add_child(std::move(lb));
        w.paint();

        box->set_value(5);  // beyond the window: scrolls
        w.paint();
        expect_region(w, 10, 10, 100, 48);

        box->set_value(9);
        w.paint();
        expect_region(w, 10, 10, 100, 48);
    }

    // text input edits through the dispatcher
    {
        zb::app::CanvasWindow w;
        w.create(200, 80);
        auto ti = std::make_unique<TextInput>();
        ti->set_size(100, 8);
        ti->set_position(10, 10);
        auto *input = ti.get();
        w.root().add_child(std::move(ti));
        w.paint();

        zb::ui::InputDispatcher d;
        zb::input::input_event down;
        down.type = zb::input::input_type::mouse_left_down;
        down.x = 20;
        down.y = 12;
        EXPECT(d.dispatch(w.root(), down));
        w.paint();
        EXPECT(input->is_focused());

        zb::input::input_event ch;
        ch.type = zb::input::input_type::key_down;
        ch.ch = 'a';
        EXPECT(d.dispatch(w.root(), ch));
        w.paint();
        EXPECT(input->get_text() == u"a");
        int rx = 0, ry = 0, rw = 0, rh = 0;
        EXPECT(w.dirty_region(rx, ry, rw, rh));
        EXPECT(rx <= 10 && ry <= 10 && rx + rw >= 10 + 100 && ry + rh >= 10 + 7);
    }

    // layout marks the container (children cannot leave its bounds)
    {
        zb::app::CanvasWindow w;
        w.create(200, 80);
        auto host = std::make_unique<FlexPanel>();
        host->set_size(100, 60);
        host->set_position(20, 10);
        auto c1 = std::make_unique<Button>();
        c1->set_size(30, 20);
        auto c2 = std::make_unique<Button>();
        c2->set_size(20, 20);
        host->add_child(std::move(c1));
        host->add_child(std::move(c2));
        auto *f = host.get();
        w.root().add_child(std::move(host));
        w.paint();

        f->layout();
        w.paint();
        // the union of the container and its children stays inside it
        int rx = 0, ry = 0, rw = 0, rh = 0;
        EXPECT(w.dirty_region(rx, ry, rw, rh));
        EXPECT(rx >= 20 && ry >= 10 && rx + rw <= 120 && ry + rh <= 70);
        // both children are inside the reported region
        EXPECT(rx <= 21 && ry <= 11 && rx + rw >= 21 + 30 && ry + rh >= 11 + 20);
    }

    // damage is consumed: no setter, no region
    {
        rig r;
        r.w.paint();
        expect_region(r.w, 0, 0, 0, 0);
    }

    // damage culling: subtrees outside the reported region are not drawn
    // at all (the rasterizer never runs for them)
    {
        zb::app::CanvasWindow w;
        w.create(200, 80);
        auto near = std::make_unique<DrawProbe>();
        near->set_position(10, 10);
        near->set_size(40, 20);
        auto far = std::make_unique<DrawProbe>();
        far->set_position(150, 10);
        far->set_size(40, 20);
        auto *n = near.get();
        auto *f = far.get();
        w.root().add_child(std::move(near));
        w.root().add_child(std::move(far));
        w.paint();
        // the initial frame covers the whole buffer: both drew
        EXPECT(n->draws > 0 && f->draws > 0);

        // damage one probe only: the other subtree must not render
        n->draws = 0;
        f->draws = 0;
        n->set_background_color(core::colors::Red);
        w.paint();
        EXPECT(n->draws > 0 && f->draws == 0);
        int x = 0, y = 0, ww = 0, hh = 0;
        EXPECT(w.dirty_region(x, y, ww, hh));
        EXPECT(x == 10 && y == 10 && ww == 40 && hh == 20);
    }

    // overflow children and the subtree prune (A-10): every widget's
    // drawing is clipped to its own rect before the descent (the
    // clip_safe chain), so a widget's visible contribution is a subset
    // of its own bounds -- the prune by own bounds can never hide a
    // pixel that a full frame would show. Both directions are pinned:
    // a negative-position child repaints its in-bounds sliver on a
    // partial repaint, and a fully-outside child stays invisible even
    // when damaged.
    {
        zb::app::CanvasWindow w;
        w.create(60, 60);

        auto parent = std::make_unique<Panel>();
        parent->set_size(30, 30);
        parent->set_position(10, 10);

        // child hanging off the top-left: absolute (5,5)-(24,24); only
        // the sliver inside the parent, (10,10)-(24,24), can ever show
        auto child = std::make_unique<Widget>();
        child->set_size(20, 20);
        child->set_position(-5, -5);
        child->set_background_color(core::colors::Red);
        auto *pc = child.get();
        parent->add_child(std::move(child));

        // child fully outside the parent: never visible, even damaged
        auto far = std::make_unique<Widget>();
        far->set_size(10, 10);
        far->set_position(50, 50);  // absolute (60,60): off-window too
        far->set_background_color(core::colors::Blue);
        auto *pf = far.get();
        parent->add_child(std::move(far));

        w.root().add_child(std::move(parent));
        w.paint();

        auto px = [&w](const int x, const int y)
        {
            return static_cast<const core::Color *>(w.data())[y * w.width() + x].pixel;
        };

        // full frame: the sliver is red, nothing outside the parent is
        EXPECT(px(10, 10) == core::colors::Red.pixel);
        EXPECT(px(24, 24) == core::colors::Red.pixel);
        EXPECT(px(9, 9) != core::colors::Red.pixel);
        EXPECT(px(25, 10) != core::colors::Red.pixel);
        EXPECT(px(5, 5) != core::colors::Blue.pixel);

        // partial repaint of the overflow child: the sliver updates, so
        // the prune did not over-cull the part visible inside the parent
        pc->set_background_color(core::colors::Green);
        w.paint();
        EXPECT(px(10, 10) == core::colors::Green.pixel);
        EXPECT(px(24, 24) == core::colors::Green.pixel);
        EXPECT(px(9, 9) != core::colors::Green.pixel);

        // damage the fully-outside child: the tree prunes at the parent
        // and nothing on screen changes
        pf->set_background_color(core::colors::Red);
        w.paint();
        EXPECT(px(10, 10) == core::colors::Green.pixel);  // sliver kept
        EXPECT(px(5, 5) != core::colors::Red.pixel);
    }

    return test::report("dirty");
}