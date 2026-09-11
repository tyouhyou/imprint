#include "test.hpp"
#include "knob.hpp"
#include "theme.hpp"
#include <vector>

int test_knob()
{
    using namespace zb::ui;
    using namespace zb::input;

    set_theme(light_theme());
    const core::Color blue  = theme().accent;
    const core::Color field = theme().field_bg;
    const core::Color black = theme().border;

    Knob k;
    EXPECT(k.measure().width == 40);
    EXPECT(k.measure().height == 40);
    EXPECT(k.get_min() == 0);
    EXPECT(k.get_max() == 100);
    EXPECT(k.get_value() == 0);
    EXPECT(k.get_step() == 1);
    // is_focusable is protected; proven indirectly by the dispatcher focus test
    // captures_pointer is proven by the drag scenario below

    // programmatic setters are silent
    int changed_count = 0;
    k.changed.sub([&](int) { ++changed_count; });
    k.set_value(42);
    EXPECT(k.get_value() == 42);
    EXPECT(changed_count == 0);

    // set_value clamps; same value is a no-op
    k.set_value(200);
    EXPECT(k.get_value() == 100);
    k.set_value(-1);
    EXPECT(k.get_value() == 0);
    k.set_value(42);
    EXPECT(k.get_value() == 42);
    k.set_value(42);
    EXPECT(k.get_value() == 42);

    // wheel: up one step, down one step
    input_event wheel;
    wheel.type = input_type::mouse_wheel;
    wheel.delta = 1;
    EXPECT(k.on_input(wheel));
    EXPECT(k.get_value() == 43);
    EXPECT(changed_count == 1);
    wheel.delta = -1;
    EXPECT(k.on_input(wheel));
    EXPECT(k.get_value() == 42);
    EXPECT(changed_count == 2);

    // do not fire on a clamped wheel
    k.set_value(100);
    EXPECT(k.get_value() == 100);
    wheel.delta = 1;
    EXPECT(!k.on_input(wheel));
    EXPECT(changed_count == 2);
    k.set_value(42);

    // arrow keys step by one
    input_event key;
    key.type = input_type::key_down;
    key.key = static_cast<int>(key_code::up);
    EXPECT(k.on_input(key));
    EXPECT(k.get_value() == 43);
    key.key = static_cast<int>(key_code::right);
    EXPECT(k.on_input(key));
    EXPECT(k.get_value() == 44);
    key.key = static_cast<int>(key_code::down);
    EXPECT(k.on_input(key));
    EXPECT(k.get_value() == 43);
    key.key = static_cast<int>(key_code::left);
    EXPECT(k.on_input(key));
    EXPECT(k.get_value() == 42);

    // vertical drag: one full widget height = whole range; up raises
    input_event down;
    down.type = input_type::mouse_left_down;
    down.y = 30;
    EXPECT(k.on_input(down));
    input_event move;
    move.type = input_type::mouse_move;
    move.y = 30 - 4;  // 4px up -> 10 units
    int fires = changed_count;
    EXPECT(k.on_input(move));
    EXPECT(k.get_value() == 52);
    EXPECT(changed_count == fires + 1);
    // sub-pixel remainder accumulates on a later move
    move.y = 30 - 4 - 1;  // 1px more
    k.on_input(move);
    EXPECT(k.get_value() == 54);  // rounded accumulator
    input_event up;
    up.type = input_type::mouse_left_up;
    EXPECT(k.on_input(up));

    // moves without a captured press are ignored
    int before = changed_count;
    k.on_input(move);
    EXPECT(changed_count == before);

    // circular hit with the visible gate
    EXPECT(k.hit(20, 20));
    EXPECT(!k.hit(1, 1));
    EXPECT(!k.hit(39, 0));
    k.set_visible(false);
    EXPECT(!k.hit(20, 20));
    k.set_visible(true);

    // pixel geometry
    k.set_size({40, 40});
    k.set_value(0);  // pointer to -135° (upper left diagonal)
    std::vector<uint32_t> px(48 * 48);
    core::Graphics g(48, 48, px.data());
    k.draw(g);
    EXPECT(test::pixel_at(g, 20, 20) == black.pixel); // hub
    EXPECT(test::pixel_at(g, 20, 6) == field.pixel);  // plain face at -90
    EXPECT(test::pixel_at(g, 14, 14) == blue.pixel);  // pointer diagonal
    k.set_value(100); // pointer to +135° (lower left diagonal)
    k.draw(g);
    EXPECT(test::pixel_at(g, 14, 26) == blue.pixel);
    k.set_value(50); // pointer to 0° (due east)
    k.draw(g);
    EXPECT(test::pixel_at(g, 27, 20) == blue.pixel);

    return test::report("knob");
}