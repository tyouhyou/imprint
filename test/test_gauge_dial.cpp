#include "test.hpp"
#include "gauge_dial.hpp"
#include "theme.hpp"
#include <vector>

int test_gauge_dial()
{
    using namespace zb::ui;

    set_theme(light_theme());
    const core::Color blue  = theme().accent;
    const core::Color white = theme().background;

    GaugeDial g;
    EXPECT(g.measure().width == 64);
    EXPECT(g.measure().height == 64);
    EXPECT(g.get_min() == 0);
    EXPECT(g.get_max() == 100);
    EXPECT(g.get_value() == 0);
    EXPECT(g.get_start_deg() == -225);
    EXPECT(g.get_sweep_deg() == 270);
    EXPECT(!g.is_focusable());

    // set_value clamps; same value is a no-op (never forces a repaint)
    g.set_value(150);
    EXPECT(g.get_value() == 100);
    g.set_value(-5);
    EXPECT(g.get_value() == 0);
    g.set_value(50);
    EXPECT(g.get_value() == 50);
    g.set_value(50);
    EXPECT(g.get_value() == 50);

    // reversed range clamps to a point (max collapses to min, slider rule)
    g.set_range(10, 5);
    EXPECT(g.get_min() == 10);
    EXPECT(g.get_max() == g.get_min());
    g.set_range(0, 100);

    // start/sweep setters
    g.set_start_deg(-180);
    EXPECT(g.get_start_deg() == -180);
    g.set_sweep_deg(180);
    EXPECT(g.get_sweep_deg() == 180);
    g.set_start_deg(-225);
    g.set_sweep_deg(270);

    // circular hit: inside the face, outside the corners
    EXPECT(g.hit(32, 32));
    EXPECT(g.hit(5 + 32, 32)); // in
    EXPECT(!g.hit(1, 1));      // far corner out
    EXPECT(!g.hit(63, 0));     // corner out
    EXPECT(!g.hit(0, 63));
    g.set_visible(false);
    EXPECT(!g.hit(32, 32));  // the visible gate is preserved
    g.set_visible(true);
    EXPECT(g.hit(32, 32));

    // pixel geometry (value 100 = needle at +45°, diagonal to lower right)
    g.set_size({64, 64});
    g.set_value(100);
    std::vector<uint32_t> px(64 * 64);
    core::Graphics ga(64, 64, px.data());
    g.draw(ga);
    EXPECT(test::pixel_at(ga, 41, 41) == blue.pixel);  // needle diagonal
    EXPECT(test::pixel_at(ga, 32, 32) == blue.pixel);  // hub
    EXPECT(test::pixel_at(ga, 32, 20) == white.pixel); // plain face

    // value 50 = needle straight up (at -90°)
    g.set_value(50);
    g.draw(ga);
    EXPECT(test::pixel_at(ga, 32, 19) == blue.pixel); // needle vertical

    // value 0 = no value arc, needle at the left edge
    g.set_value(0);
    EXPECT(g.get_value() == 0);
    g.set_visible(false);
    g.draw(ga);
    g.set_visible(true);

    return test::report("gauge_dial");
}