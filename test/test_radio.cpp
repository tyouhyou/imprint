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

    // 100x120 panel with three radios stacked at x=10, group 7
    struct Tree
    {
        Panel root;
        RadioButton *r1 = nullptr;
        RadioButton *r2 = nullptr;
        RadioButton *r3 = nullptr;

        Tree()
        {
            root.set_size(100, 120);
            for (int i = 0; i < 3; ++i)
            {
                auto r = std::make_unique<RadioButton>();
                r->set_group(7);
                r->set_size(72, 20);
                r->set_position(10, 10 + i * 30);
                r->set_text("opt");
                if (i == 0)
                    r1 = r.get();
                else if (i == 1)
                    r2 = r.get();
                else
                    r3 = r.get();
                root.add_child(std::move(r));
            }
        }
    };
}

int test_radio()
{
    // selecting one radio unselects the same-group siblings
    {
        Tree t;
        InputDispatcher d;
        int fires = 0;
        t.r1->changed += [&fires]() { ++fires; };
        t.r2->changed += [&fires]() { ++fires; };
        t.r3->changed += [&fires]() { ++fires; };

        d.dispatch(t.root, press_at(15, 45));
        d.dispatch(t.root, release_at(15, 45));
        EXPECT(t.r2->is_checked());
        EXPECT(!t.r1->is_checked());
        EXPECT(!t.r3->is_checked());
        EXPECT(fires == 1);  // only the newly selected radio reports

        // moving the selection elsewhere clears the previous one
        d.dispatch(t.root, press_at(15, 75));
        d.dispatch(t.root, release_at(15, 75));
        EXPECT(t.r3->is_checked());
        EXPECT(!t.r2->is_checked());
        EXPECT(fires == 2);

        // re-selecting the same radio is a no-op
        d.dispatch(t.root, press_at(15, 75));
        d.dispatch(t.root, release_at(15, 75));
        EXPECT(fires == 2);
    }

    // different groups do not interfere
    {
        Tree t;
        InputDispatcher d;
        t.r1->set_group(1);
        t.r2->set_group(2);
        t.r3->set_group(2);

        d.dispatch(t.root, press_at(15, 45));  // group 2
        d.dispatch(t.root, release_at(15, 45));
        d.dispatch(t.root, press_at(15, 10));  // group 1
        d.dispatch(t.root, release_at(15, 10));
        EXPECT(t.r1->is_checked());
        EXPECT(t.r2->is_checked());  // untouched by group 1
        EXPECT(!t.r3->is_checked());
    }

    // programmatic set_checked unselects siblings but stays silent
    {
        Tree t;
        int fires = 0;
        t.r1->changed += [&fires]() { ++fires; };
        t.r3->set_checked(true);
        EXPECT(t.r3->is_checked());
        EXPECT(!t.r1->is_checked());
        EXPECT(fires == 0);
        t.r1->set_checked(false);  // explicit deselect
        EXPECT(!t.r1->is_checked());
    }

    // dragging a press away cancels: no selection
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, press_at(15, 45));
        d.dispatch(t.root, move_to(90, 90));
        EXPECT(!t.r2->is_checked());
        d.dispatch(t.root, release_at(90, 90));
        EXPECT(!t.r2->is_checked());
    }

    // keyboard: tab through the radios, Enter/Space selects
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::tab)));
        EXPECT(d.get_focus_target() == t.r1);
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::tab)));
        EXPECT(d.get_focus_target() == t.r2);
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::space)));
        EXPECT(t.r2->is_checked());
        EXPECT(!t.r1->is_checked());
    }

    // drawing: ring border, dot only when selected
    {
        Tree t;
        InputDispatcher d;
        core::Graphics g(100, 120, nullptr);
        t.root.draw(g);
        // ring pixel on the r1 circle edge (center (16,16), radius 5)
        EXPECT(core::colors::Black.pixel == test::pixel_at(g, 21, 16));
        // no dot in the selected-less state: center is background
        EXPECT(core::colors::Blue.pixel != test::pixel_at(g, 16, 16));

        // select r2 (center (16,46)) via keyboard
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::tab)));
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::tab)));
        d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::space)));
        core::Graphics g2(100, 120, nullptr);
        t.root.draw(g2);
        // the dot is filled at the circle center
        EXPECT(core::colors::Blue.pixel == test::pixel_at(g2, 16, 46));
        // the others stay dot-less (r1 center (16,16), r3 (16,76))
        EXPECT(core::colors::Blue.pixel != test::pixel_at(g2, 16, 16));
        EXPECT(core::colors::Blue.pixel != test::pixel_at(g2, 16, 76));
    }

    return test::report("radio");
}