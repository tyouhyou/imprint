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

    // 100x100 panel with a 72x20 checkbox at (10,10); box 14px square
    struct Tree
    {
        Panel root;
        Checkbox *box = nullptr;

        Tree()
        {
            root.set_size(100, 100);
            auto b = std::make_unique<Checkbox>();
            b->set_size(72, 20);
            b->set_position(10, 10);
            b->set_text("opt");
            box = b.get();
            root.add_child(std::move(b));
        }
    };
}

int test_checkbox()
{
    // a click toggles and emits changed with the new state
    {
        Tree t;
        InputDispatcher d;
        bool emitted = false;
        bool emitted_state = false;
        t.box->changed += [&](const bool c) {
            emitted = true;
            emitted_state = c;
        };

        d.dispatch(t.root, press_at(15, 15));
        EXPECT(t.box->is_pressed());
        d.dispatch(t.root, release_at(15, 15));
        EXPECT(t.box->is_checked());
        EXPECT(emitted);
        EXPECT(emitted_state);

        // second click switches back to false
        emitted_state = false;
        d.dispatch(t.root, press_at(15, 15));
        d.dispatch(t.root, release_at(15, 15));
        EXPECT(!t.box->is_checked());
        EXPECT(!emitted_state);
        // programmatic set is silent
        emitted = false;
        t.box->set_checked(true);
        EXPECT(!emitted);
    }

    // clicking the label area toggles too (whole widget is hittable)
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, press_at(60, 20));
        d.dispatch(t.root, release_at(60, 20));
        EXPECT(t.box->is_checked());
    }

    // dragging a press away cancels: no toggle
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, press_at(15, 15));
        d.dispatch(t.root, move_to(80, 80));
        EXPECT(!t.box->is_checked());
        EXPECT(!t.box->is_pressed());
        d.dispatch(t.root, release_at(80, 80));
        EXPECT(!t.box->is_checked());
    }

    // keyboard: focus + Enter/Space toggle immediately
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::tab)));
        EXPECT(d.get_focus_target() == t.box);
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::enter)));
        EXPECT(t.box->is_checked());
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::space)));
        EXPECT(!t.box->is_checked());
    }

    // drawing: the box border is drawn; the check appears only when
    // checked (bitmap text of the label is covered by test_text)
    {
        Tree t;
        InputDispatcher d;
        core::Graphics g(100, 100, nullptr);
        t.root.draw(g);

        // box border pixels: (10,10) should be the border color
        EXPECT(core::colors::Black.pixel == test::pixel_at(g, 10, 10));
        // inside the box: nothing when unchecked (interior stays black)
        EXPECT(core::colors::Blue.pixel != test::pixel_at(g, 14, 19));

        // toggle via keyboard and redraw
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::tab)));
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::space)));
        core::Graphics g2(100, 100, nullptr);
        t.root.draw(g2);
        // the first check stroke crosses local (4,9) -> global (14,19)
        EXPECT(core::colors::Blue.pixel == test::pixel_at(g2, 14, 19));
    }

    return test::report("checkbox");
}