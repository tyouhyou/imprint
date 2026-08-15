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

    zb::input::input_event key_down(const int key)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.key = key;
        return ev;
    }

    zb::input::input_event wheel_at(const int x, const int y, const int delta)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_wheel;
        ev.x = x;
        ev.y = y;
        ev.delta = delta;
        return ev;
    }

    // 100x20 slider at (10,10): thumb travels x 5..95 for value 0..100;
    // a button at (10,30) gives the focus navigation a second stop
    struct Tree
    {
        Panel root;
        Slider *slider = nullptr;

        Tree()
        {
            root.set_size(120, 60);
            auto s = std::make_unique<Slider>();
            s->set_size(100, 20);
            s->set_position(10, 10);
            s->set_range(0, 100);
            s->set_step(5);
            slider = s.get();
            root.add_child(std::move(s));

            auto b = std::make_unique<Button>();
            b->set_size(20, 20);
            b->set_position(10, 30);
            root.add_child(std::move(b));
        }
    };
}

int test_slider()
{
    // a press jumps the value to the press position (and fires changed)
    {
        Tree t;
        InputDispatcher d;
        int fires = 0;
        int last = -1;
        t.slider->changed += [&](const int v) {
            ++fires;
            last = v;
        };

        // press at local x=60 -> thumb center 5 + v*90/100 = 60 -> v=61
        d.dispatch(t.root, press_at(70, 20));
        d.dispatch(t.root, release_at(70, 20));
        EXPECT(t.slider->get_value() == 61);
        EXPECT(fires == 1);
        EXPECT(last == 61);
    }

    // dragging follows the pointer; ends clamp the value
    {
        Tree t;
        InputDispatcher d;
        int last = -1;
        t.slider->changed += [&](const int v) { last = v; };

        EXPECT(d.dispatch(t.root, press_at(15, 20)));  // local x=5 -> value 0
        // drag right to local x=70
        EXPECT(d.dispatch(t.root, move_to(80, 25)));
        EXPECT(t.slider->get_value() == 72);  // (70-5)*100/90=72
        // drag far past the right end: clamps to max, still delivered
        EXPECT(d.dispatch(t.root, move_to(400, 30)));
        EXPECT(t.slider->get_value() == 100);
        // drag far past the left end
        EXPECT(d.dispatch(t.root, move_to(-50, 15)));
        EXPECT(t.slider->get_value() == 0);
        EXPECT(d.dispatch(t.root, release_at(-50, 15)));
    }

    // the press survives moves far outside the widget (pointer capture)
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, press_at(15, 20));
        d.dispatch(t.root, move_to(500, 80));  // way outside
        EXPECT(t.slider->get_value() == 100);  // not cancelled, clamped
        d.dispatch(t.root, release_at(500, 80));
    }

    // a move that does not change the value reports no repaint
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, press_at(15, 20));  // value 0
        EXPECT(!d.dispatch(t.root, move_to(13, 20)));  // still 0
        EXPECT(d.dispatch(t.root, move_to(60, 20)));   // moved
        d.dispatch(t.root, release_at(60, 20));
    }

    // mouse wheel steps the value (clamped at both ends)
    {
        Tree t;
        InputDispatcher d;
        int last = -1;
        t.slider->changed += [&](const int v) { last = v; };

        EXPECT(d.dispatch(t.root, wheel_at(50, 20, 3)));  // +5
        EXPECT(t.slider->get_value() == 5);
        EXPECT(last == 5);
        EXPECT(d.dispatch(t.root, wheel_at(50, 20, -1)));  // -5
        EXPECT(t.slider->get_value() == 0);
        // below the floor: no change, no event
        EXPECT(!d.dispatch(t.root, wheel_at(50, 20, -3)));
        EXPECT(t.slider->get_value() == 0);
    }

    // wheel outside the slider does nothing
    {
        Tree t;
        InputDispatcher d;
        EXPECT(!d.dispatch(t.root, wheel_at(5, 5, 1)));  // outside the slider
    }

    // keyboard: focused slider consumes left/right and steps
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, press_at(15, 20));  // value 0 + focus
        d.dispatch(t.root, release_at(15, 20));
        EXPECT(d.get_focus_target() == t.slider);

        EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::right))));
        EXPECT(t.slider->get_value() == 5);
        EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::right))));
        EXPECT(t.slider->get_value() == 10);
        // left steps back; at the floor the key is not consumed, so the
        // focus navigation takes over (to the button)
        EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::left))));
        EXPECT(t.slider->get_value() == 5);
        EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::left))));
        EXPECT(t.slider->get_value() == 0);
        EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::left))));
        EXPECT(d.get_focus_target() != t.slider);  // unconsumed: navigation
    }

    // drawing: track across the widget, thumb at the value position
    {
        Tree t;
        InputDispatcher d;
        core::Graphics g(120, 60, nullptr);
        t.root.draw(g);

        // track pixel right of the thumb (local x=60 -> global 70)
        EXPECT(core::colors::Black.pixel == test::pixel_at(g, 70, 20));
        // thumb at value 0: center vx=5 -> pixels 0..9 local -> (10..19,20)
        // thumb color is Blue: (12,20) inside the thumb
        EXPECT(core::colors::Blue.pixel == test::pixel_at(g, 12, 20));
        // right half of the track (x=60 local -> 70 global): no thumb
        EXPECT(core::colors::Blue.pixel != test::pixel_at(g, 70, 20));
        // the track is still there
        EXPECT(core::colors::Black.pixel == test::pixel_at(g, 70, 20));
    }

    return test::report("slider");
}