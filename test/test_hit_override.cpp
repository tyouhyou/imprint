#include "test.hpp"

#include "imui.hpp"

using namespace zb::ui;

namespace
{
    zb::input::input_event press_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    zb::input::input_event release_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_up;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    zb::input::input_event move_to(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_move;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    /*
     * A 40x40 widget whose hit test is a circle of radius 15 centered at
     * (20,20) in widget-local coordinates -- the GaugeDial / Knob shape.
     * The shape round-trips the whole dispatch path: the parent's pick()
     * hands over the point already translated to widget-local coords.
     */
    struct CircleProbe : public Widget
    {
        int presses = 0;
        int releases = 0;
        int moves = 0;
        int cancels = 0;

        // an override replaces the whole base body, so it reproduces the
        // visibility gate the base checked (A-24)
        bool hit(const int x, const int y) const override
        {
            if (!is_visible())
            {
                return false;
            }
            const int dx = x - 20;
            const int dy = y - 20;
            return dx * dx + dy * dy <= 15 * 15;
        }

        void on_cancel() override { ++cancels; }

        // claims the press and the release; also records in-shape moves
        bool on_input(const zb::input::input_event &ev) override
        {
            if (ev.type == zb::input::input_type::mouse_left_down)
            {
                ++presses;
                return true;
            }
            if (ev.type == zb::input::input_type::mouse_left_up)
            {
                ++releases;
                return true;
            }
            if (ev.type == zb::input::input_type::mouse_move)
            {
                ++moves;
                return true;
            }
            return false;
        }
    };

    // root + a CircleProbe at (10,10): the circle center is at absolute
    // (30,30), its bounding box spans (10,10)..(49,49)
    struct Tree
    {
        Panel root;
        CircleProbe *probe = nullptr;

        Tree()
        {
            root.set_size(100, 100);
            auto p = std::make_unique<CircleProbe>();
            p->set_size(40, 40);
            p->set_position(10, 10);
            probe = p.get();
            root.add_child(std::move(p));
        }
    };
}  // namespace

int test_hit_override()
{
    // press + release inside the circle fires (custom shape pick)
    {
        Tree t;
        InputDispatcher d;
        EXPECT(d.dispatch(t.root, press_at(30, 30)));   // circle center
        EXPECT(t.probe->presses == 1);
        EXPECT(d.dispatch(t.root, release_at(30, 30)));
        EXPECT(t.probe->releases == 1);
    }

    // a point inside the bounding box but outside the circle is rejected:
    // the widget is not picked and the press is not claimed
    {
        Tree t;
        InputDispatcher d;
        // absolute (12,12) = local (2,2): 18px from the center, > 15 radius
        EXPECT(!d.dispatch(t.root, press_at(12, 12)));
        EXPECT(t.probe->presses == 0);
        EXPECT(!d.dispatch(t.root, release_at(12, 12)));
        EXPECT(t.probe->releases == 0);
    }

    // release is delivered to the claimed (pressed) widget even when the
    // release point lies outside the shape / off its area
    {
        Tree t;
        InputDispatcher d;
        EXPECT(d.dispatch(t.root, press_at(30, 30)));
        EXPECT(d.dispatch(t.root, release_at(90, 90)));  // far outside
        EXPECT(t.probe->releases == 1);
        EXPECT(t.probe->cancels == 0);
    }

    // a move crossing the circle boundary cancels the press (the slop
    // check goes through pick(), which routes through the custom hit())
    {
        Tree t;
        InputDispatcher d;
        EXPECT(d.dispatch(t.root, press_at(30, 30)));
        EXPECT(d.dispatch(t.root, move_to(12, 12)));  // outside the circle
        EXPECT(t.probe->cancels == 1);
        EXPECT(t.probe->moves == 0);  // the cancel did not deliver a move
        // the cancelled press is gone: the release claims nothing
        EXPECT(!d.dispatch(t.root, release_at(12, 12)));
        EXPECT(t.probe->releases == 0);
    }

    // a move that stays inside the custom shape does NOT cancel even past
    // the default 8px slop -- the shape, not the rectangle, is the hit area
    {
        Tree t;
        InputDispatcher d;
        EXPECT(d.dispatch(t.root, press_at(30, 30)));
        // absolute (40,30) = local (30,20): 10px right, 10px from the
        // center (inside the radius 15) but > 8px slop from the press
        EXPECT(!d.dispatch(t.root, move_to(40, 30)));
        EXPECT(t.probe->cancels == 0);
        EXPECT(d.dispatch(t.root, release_at(40, 30)));
        EXPECT(t.probe->releases == 1);
    }

    // a hidden custom-hit widget is not picked (the override reproduces
    // the base visibility gate)
    {
        Tree t;
        InputDispatcher d;
        t.probe->set_visible(false);
        EXPECT(!d.dispatch(t.root, press_at(30, 30)));
        EXPECT(t.probe->presses == 0);
    }

    // nested containers translate the point to widget-local coordinates
    // before hit(): the custom shape works through a container chain
    {
        Panel root;
        root.set_size(120, 120);

        auto inner = std::make_unique<Panel>();
        inner->set_size(60, 60);
        inner->set_position(20, 20);
        auto *pinner = inner.get();

        auto p = std::make_unique<CircleProbe>();
        p->set_size(40, 40);
        p->set_position(10, 10);  // circle center at absolute (50,50)
        auto *pprobe = p.get();
        inner->add_child(std::move(p));
        root.add_child(std::move(inner));

        InputDispatcher d;
        // the box-but-not-circle point first (no press in flight)
        EXPECT(!d.dispatch(root, press_at(32, 32)));  // local (2,2)
        EXPECT(pprobe->presses == 0);
        EXPECT(d.dispatch(root, press_at(50, 50)));
        EXPECT(pprobe->presses == 1);
        EXPECT(pprobe->is_descendant_of(&root));
        EXPECT(pprobe->is_descendant_of(pinner));
    }

    return test::report("hit_override");
}