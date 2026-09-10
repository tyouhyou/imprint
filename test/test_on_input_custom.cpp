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

    zb::input::input_event touch_press_at(const int x, const int y, const int id)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::touch_down;
        ev.x = x;
        ev.y = y;
        ev.touch_id = id;
        return ev;
    }

    zb::input::input_event touch_release_at(const int x, const int y, const int id)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::touch_up;
        ev.x = x;
        ev.y = y;
        ev.touch_id = id;
        return ev;
    }

    zb::input::input_event touch_move_to(const int x, const int y, const int id)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::touch_move;
        ev.x = x;
        ev.y = y;
        ev.touch_id = id;
        return ev;
    }

    /*
     * A widget whose on_input() override consumes pointer events to track
     * a drag (the Knob/TrendLine value-drag pattern). It accumulates the
     * horizontal delta so a test can assert the drag actually happened,
     * exposes `capture` to toggle between drag semantics, and reports the
     * return-value convention (true = consumed / changed).
     */
    struct DragProbe : public Widget
    {
        bool capture = false;
        int presses = 0;
        int releases = 0;
        int moves = 0;
        int cancels = 0;
        int drag = 0;  // accumulated |dx| across moves
        int last_x = 0;

        bool captures_pointer() const override { return capture; }

        void on_cancel() override { ++cancels; }

        bool on_input(const zb::input::input_event &ev) override
        {
            if (ev.type == zb::input::input_type::mouse_left_down ||
                ev.type == zb::input::input_type::touch_down)
            {
                ++presses;
                last_x = ev.x;
                return true;  // claimed
            }
            if (ev.type == zb::input::input_type::mouse_left_up ||
                ev.type == zb::input::input_type::touch_up)
            {
                ++releases;
                return true;
            }
            if (ev.type == zb::input::input_type::mouse_move ||
                ev.type == zb::input::input_type::touch_move)
            {
                ++moves;
                drag += ev.x >= last_x ? ev.x - last_x : last_x - ev.x;
                last_x = ev.x;
                return true;  // consumed: a drag changed the widget
            }
            return false;
        }
    };

    struct Tree
    {
        Panel root;
        DragProbe *probe = nullptr;

        Tree()
        {
            root.set_size(100, 100);
            auto p = std::make_unique<DragProbe>();
            p->set_size(20, 20);
            p->set_position(10, 10);
            probe = p.get();
            root.add_child(std::move(p));
        }
    };
}  // namespace

int test_on_input_custom()
{
    // a custom on_input that returns true on press claims the pressed
    // target; the release anywhere still delivers to it
    {
        Tree t;
        InputDispatcher d;
        EXPECT(d.dispatch(t.root, press_at(15, 15)));
        EXPECT(t.probe->presses == 1);
        EXPECT(d.dispatch(t.root, release_at(80, 80)));
        EXPECT(t.probe->releases == 1);
        EXPECT(t.probe->cancels == 0);
    }

    // the pressed-target lock works with the custom override: without
    // capture, an off-area move beyond slop cancels the press (drag needs
    // captures_pointer())
    {
        Tree t;
        InputDispatcher d;
        EXPECT(d.dispatch(t.root, press_at(15, 15)));
        EXPECT(t.probe->cancels == 0);
        EXPECT(d.dispatch(t.root, move_to(80, 80)));  // changed: cancelled
        EXPECT(t.probe->cancels == 1);
        EXPECT(t.probe->moves == 0);  // the cancel did not deliver the move
        // the press is gone: a later release is not delivered
        EXPECT(!d.dispatch(t.root, release_at(80, 80)));
        EXPECT(t.probe->releases == 0);
    }

    // captures_pointer(): every move is delivered while the press is held
    // (Knob/Slider drag), far outside the area, and never cancelled
    {
        Tree t;
        InputDispatcher d;
        t.probe->capture = true;
        EXPECT(d.dispatch(t.root, press_at(15, 15)));
        EXPECT(d.dispatch(t.root, move_to(90, 90)));  // far outside
        EXPECT(t.probe->moves == 1);
        EXPECT(t.probe->drag == 75);
        EXPECT(d.dispatch(t.root, move_to(95, 5)));
        EXPECT(t.probe->moves == 2);
        EXPECT(t.probe->drag == 80);
        EXPECT(t.probe->cancels == 0);
        EXPECT(d.dispatch(t.root, release_at(95, 5)));
        EXPECT(t.probe->releases == 1);
        EXPECT(t.probe->cancels == 0);
    }

    // dispatch return value reports the widget's own true/false: a move
    // the widget consumes reports a change, one it declines reports none
    {
        struct DecliningProbe final : public DragProbe
        {
            bool declining = false;
            bool on_input(const zb::input::input_event &ev) override
            {
                DragProbe::on_input(ev);
                return !declining;  // decline -> false (no repaint owed)
            }
        };
        auto p = std::make_unique<DecliningProbe>();
        p->set_size(20, 20);
        p->set_position(10, 10);
        p->capture = true;  // moves reach it only while it captures
        auto *pdecl = p.get();
        Panel root;
        root.set_size(100, 100);
        root.add_child(std::move(p));
        InputDispatcher d;

        EXPECT(d.dispatch(root, press_at(15, 15)));
        pdecl->declining = true;
        EXPECT(!d.dispatch(root, move_to(25, 15)));  // declined: no-op
        EXPECT(d.dispatch(root, release_at(30, 30)));
        pdecl->declining = false;
        EXPECT(d.dispatch(root, press_at(15, 15)));
        EXPECT(d.dispatch(root, move_to(25, 15)));   // consumed: change
    }

    // the touch pointer lock applies to custom on_input overrides: moves
    // and releases from another touch_id never interfere with the press
    {
        Tree t;
        InputDispatcher d;
        EXPECT(d.dispatch(t.root, touch_press_at(15, 15, 1)));
        EXPECT(t.probe->presses == 1);
        // finger 2 drags away and lifts: ignored
        EXPECT(!d.dispatch(t.root, touch_move_to(80, 80, 2)));
        EXPECT(!d.dispatch(t.root, touch_release_at(80, 80, 2)));
        EXPECT(t.probe->moves == 0);
        EXPECT(t.probe->releases == 0);
        EXPECT(t.probe->cancels == 0);
        // finger 1 lifts: the press completes
        EXPECT(d.dispatch(t.root, touch_release_at(15, 15, 1)));
        EXPECT(t.probe->releases == 1);
    }

    // a custom on_input override without captures_pointer still honors the
    // hold: a release outside the widget after an in-area press completes
    {
        Tree t;
        InputDispatcher d;
        EXPECT(d.dispatch(t.root, press_at(15, 15)));
        EXPECT(t.probe->cancels == 0);
        // a single touch glitch spike is tolerated (the dispatcher's slop
        // debounce, independent of any widget override)
        EXPECT(!d.dispatch(t.root, touch_move_to(0, 0, 0)));
        EXPECT(t.probe->cancels == 0);
        EXPECT(d.dispatch(t.root, release_at(0, 0)));
        EXPECT(t.probe->releases == 1);
    }

    return test::report("on_input_custom");
}