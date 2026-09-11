#include "test.hpp"
#include "dispatcher.hpp"
#include "panel.hpp"
#include "theme.hpp"
#include "toggle_switch.hpp"
#include <vector>

int test_toggle_switch()
{
    using namespace zb::ui;
    using namespace zb::input;

    // the pixel assertions depend on the light theme
    set_theme(light_theme());
    const core::Color blue  = theme().accent;
    const core::Color field = theme().field_bg;
    const core::Color white = theme().text_inverted;

    auto press_at = [](const int x, const int y)
    {
        input_event ev;
        ev.type = input_type::mouse_left_down;
        ev.x = x;
        ev.y = y;
        return ev;
    };
    auto release_at = [](const int x, const int y)
    {
        input_event ev;
        ev.type = input_type::mouse_left_up;
        ev.x = x;
        ev.y = y;
        return ev;
    };
    auto move_to = [](const int x, const int y)
    {
        input_event ev;
        ev.type = input_type::mouse_move;
        ev.x = x;
        ev.y = y;
        return ev;
    };
    auto key_down = [](const int key)
    {
        input_event ev;
        ev.type = input_type::key_down;
        ev.key = key;
        return ev;
    };

    struct Tree
    {
        Panel root;
        ToggleSwitch *sw = nullptr;
        Tree()
        {
            root.set_size({200, 100});
            auto ptr = std::make_unique<ToggleSwitch>();
            sw = ptr.get();
            sw->set_position({50, 30});
            sw->set_size(sw->measure());
            root.add_child(std::move(ptr));
        }
    };

    ToggleSwitch plain;
    EXPECT(!plain.is_checked());
    EXPECT(!plain.is_pressed());
    EXPECT(plain.measure().width == 40);
    EXPECT(plain.measure().height == 20);

    // public press/release/cancel state machine
    plain.set_checked(true);
    EXPECT(plain.is_checked());
    plain.press();
    EXPECT(plain.is_pressed());
    plain.release();
    EXPECT(!plain.is_checked());  // release toggles
    plain.press();
    plain.cancel();
    EXPECT(!plain.is_pressed());
    EXPECT(!plain.is_checked());  // cancel does not toggle

    // programmatic set_checked is silent
    int changed_count = 0;
    bool last_state = false;
    plain.changed.sub([&](const bool v)
    {
        ++changed_count;
        last_state = v;
    });
    plain.set_checked(true);
    EXPECT(plain.is_checked());
    EXPECT(changed_count == 0);

    // dispatcher: click toggles and fires changed exactly once
    {
        Tree t;
        InputDispatcher d;
        t.sw->changed.sub([&](const bool v)
        {
            ++changed_count;
            last_state = v;
        });
        d.dispatch(t.root, press_at(70, 40));
        EXPECT(t.sw->is_pressed());
        d.dispatch(t.root, release_at(70, 40));
        EXPECT(t.sw->is_checked());
        EXPECT(changed_count == 1);
        EXPECT(last_state);
    }

    // a press dragged away cancels: no toggle, no event
    {
        Tree t;
        InputDispatcher d;
        changed_count = 0;
        last_state = false;
        t.sw->changed.sub([&](const bool v)
        {
            ++changed_count;
            last_state = v;
        });
        d.dispatch(t.root, press_at(70, 40));
        d.dispatch(t.root, move_to(150, 90));
        EXPECT(!t.sw->is_pressed());
        d.dispatch(t.root, release_at(150, 90));
        EXPECT(!t.sw->is_checked());
        EXPECT(changed_count == 0);
    }

    // keyboard: tab focuses (focusable), Enter/Space toggle immediately
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, key_down(static_cast<int>(key_code::tab)));
        EXPECT(d.get_focus_target() == t.sw);
        d.dispatch(t.root, key_down(static_cast<int>(key_code::enter)));
        EXPECT(t.sw->is_checked());
        d.dispatch(t.root, key_down(static_cast<int>(key_code::space)));
        EXPECT(!t.sw->is_checked());
    }

    // pixel geometry: checked = accent track, knob rides right (white)
    {
        ToggleSwitch sw;
        sw.set_size({40, 20});
        sw.set_checked(true);
        std::vector<uint32_t> px(64 * 64);
        core::Graphics g(64, 64, px.data());
        sw.draw(g);
        EXPECT(test::pixel_at(g, 20, 10) == blue.pixel);   // track body
        EXPECT(test::pixel_at(g, 30, 10) == white.pixel);  // knob (right)
        EXPECT(test::pixel_at(g, 9, 10) != white.pixel);   // no knob on the left

        // unchecked = field track, knob rides left
        sw.set_checked(false);
        sw.draw(g);
        EXPECT(test::pixel_at(g, 20, 10) == field.pixel);
        EXPECT(test::pixel_at(g, 9, 10) == white.pixel);

        // track size feeds measure() and draw
        sw.set_track_size({60, 24});
        EXPECT(sw.measure().width == 60);
        sw.draw(g);
        EXPECT(test::pixel_at(g, 30, 12) == field.pixel);
    }

    return test::report("toggle_switch");
}