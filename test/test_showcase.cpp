// SIGNAL-ONE flagship bridge tests (ISV EVENT-HORIZON redesign): the
// HTML-designed console materializes, the throttle->readout linkage
// tracks, the heading advances per frame, the radar sweep needle
// rotates, ALERT latches and clears, the system toggles feed the power
// draw, MODE cycles the accent themes, ABOUT overlay opens/closes, and
// RESET restores boot state.
#include "test.hpp"

#include "canvas_window.hpp"
#include "gauge_dial.hpp"
#include "input.hpp"
#include "progress_bar.hpp"
#include "showcase.hpp"
#include "slider.hpp"
#include "snapshot.hpp"
#include "text/utf8.hpp"
#include "theme.hpp"
#include "toggle_switch.hpp"
#include "trend_line.hpp"
#include "widget.hpp"

using namespace zb::app;

namespace
{
    std::unique_ptr<showcase::Showcase> make_app(uint32_t w, uint32_t h)
    {
        auto app = std::make_unique<showcase::Showcase>();
        app->create_window(w, h);
        app->paint();  // first frame settles the flex layout (positions)
        return app;
    }

    zb::app::CanvasWindow &win(showcase::Showcase &app)
    {
        return *static_cast<zb::app::CanvasWindow *>(app.window().get());
    }

    zb::ui::Widget &root(showcase::Showcase &app)
    {
        return win(app).root();
    }

    std::uint32_t px(showcase::Showcase &app, const int x, const int y)
    {
        const auto w = app.window();
        const auto *pixels =
            static_cast<const zb::ui::core::Color *>(w->data());
        return pixels[y * w->width() + x].pixel;
    }

    void click(zb::app::showcase::Showcase &app, const int x, const int y)
    {
        zb::input::input_event ev{};
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = x;
        ev.y = y;
        ev.touch_id = 0;
        app.input(ev);
        ev.type = zb::input::input_type::mouse_left_up;
        app.input(ev);
    }

    void click_center(zb::app::showcase::Showcase &app, zb::ui::Widget &w)
    {
        const auto p = w.get_absolute_position();
        const auto s = w.get_size();
        click(app, p.x + s.width / 2, p.y + s.height / 2);
    }

    // the published reactor-load rule: base + throttle draw + systems
    int reactor_load(const int throttle, const bool shields,
                     const bool life, const bool aux)
    {
        return 25 + (throttle * 55) / 100 +
               (shields ? 8 : 0) + (life ? 5 : 0) + (aux ? 6 : 0);
    }
}

int test_showcase()
{
    using namespace zb::ui;

    // the design materializes: the panel ids resolve to the expected
    // widget kinds, the design-file values landed, the boot theme is dark
    const auto holder = make_app(320, 240);
    auto &app = *holder;
    const auto accent0 = theme().accent;  // mode 0 (cyan)

    auto *helm = static_cast<GaugeDial *>(root(app).find_by_id("helm"));
    auto *load = static_cast<GaugeDial *>(root(app).find_by_id("load"));
    auto *temp = static_cast<ProgressBar *>(root(app).find_by_id("temp"));
    auto *trend = static_cast<TrendLine *>(root(app).find_by_id("trend"));
    auto *throttle = static_cast<Slider *>(root(app).find_by_id("throttle"));
    auto *mod_a = static_cast<ToggleSwitch *>(root(app).find_by_id("modA"));
    auto *mod_b = static_cast<ToggleSwitch *>(root(app).find_by_id("modB"));
    auto *mod_c = static_cast<ToggleSwitch *>(root(app).find_by_id("modC"));
    auto *radar = root(app).find_by_id("sweep"); // the needle pseudo host
    auto *loadval = root(app).find_by_id("loadval");
    auto *status = root(app).find_by_id("status");
    EXPECT(helm != nullptr && load != nullptr && temp != nullptr &&
           trend != nullptr && throttle != nullptr);
    EXPECT(mod_a != nullptr && mod_b != nullptr && mod_c != nullptr);
    EXPECT(radar != nullptr && loadval != nullptr && status != nullptr);
    EXPECT(throttle->get_value() == 35);
    EXPECT(mod_a->is_checked() && mod_b->is_checked() && !mod_c->is_checked());
    const int boot_load = reactor_load(35, true, true, false);
    EXPECT(load->get_value() == boot_load);  // the linkage applied at boot
    EXPECT(trend->get_count() == 48);  // seeded so the panel is never empty

    // throttle -> readout linkage: focusing the slider and stepping it
    // (arrow keys) updates the load gauge and its percentage label
    {
        const auto p = throttle->get_absolute_position();
        const auto s = throttle->get_size();
        click(app, p.x + s.width / 2, p.y + s.height / 2);
        app.paint();
        const int v0 = throttle->get_value();
        zb::input::input_event key = {};
        key.type = zb::input::input_type::key_down;
        key.key = static_cast<int>(zb::input::key_code::right);
        app.input(key);
        app.input(key);
        app.paint();
        EXPECT(throttle->get_value() == v0 + 2);
        const int expect_load = reactor_load(v0 + 2, true, true, false);
        EXPECT(load->get_value() == expect_load);
        EXPECT(loadval->get_text() ==
               utf8_to_utf16((std::to_string(expect_load) + "%").c_str()));
    }

    // the helm: heading advances with the throttle (1 + throttle/25 deg
    // per paint), a pure function of the frame counter and the inputs
    {
        const int h0 = helm->get_value();
        app.paint();
        const int step = 1 + throttle->get_value() / 25;
        EXPECT(helm->get_value() == (h0 + step) % 360);
    }

    // the radar sweep needle: the design's ::after pseudo re-specified
    // per frame (paint-only box, no layout)
    {
        const auto *before = radar->pseudo(1);
        EXPECT(before != nullptr);
        const auto ang0 = before->rot_ang;
        app.paint();
        const auto *after = radar->pseudo(1);
        EXPECT(after != nullptr && after->rot_ang != ang0);
    }

    // the live feed: one trend sample every other frame, pure in the
    // frame counter -- the rendered frame moves
    {
        const auto h0 = zb::snap::framebuffer_hash(win(app));
        app.paint();
        app.paint();
        app.paint();
        const auto h1 = zb::snap::framebuffer_hash(win(app));
        EXPECT(h0 != h1);
    }

    // MODE cycles the accent theme (three modes, then back around)
    {
        auto *mode_btn = root(app).find_by_id("mode");
        EXPECT(mode_btn != nullptr);
        click_center(app, *mode_btn);
        app.paint();
        const auto accent1 = theme().accent;
        EXPECT(!(accent1 == accent0));
        click_center(app, *mode_btn);
        click_center(app, *mode_btn);
        app.paint();
        EXPECT(theme().accent == accent0);  // three modes cycle back
    }

    // ALERT latches a red condition (the status text is set
    // synchronously by the click handler); CANCEL restores the accent
    {
        auto *alert_btn = root(app).find_by_id("alert");
        EXPECT(alert_btn != nullptr);
        click_center(app, *alert_btn);
        EXPECT(status->get_text() == u"RED ALERT \u00B7 ALL HANDS");
        app.paint();
        EXPECT(!(theme().accent == accent0));
        click_center(app, *alert_btn);
        app.paint();
        EXPECT(theme().accent == accent0);
    }

    // the system toggles feed the power draw (and the status line)
    {
        const int load_before = load->get_value();
        click_center(app, *mod_c);
        EXPECT(mod_c->is_checked());
        EXPECT(load->get_value() == load_before + 6);
        EXPECT(status->get_text() == u"AUX SENSORS ONLINE");
        click_center(app, *mod_c);
        EXPECT(!mod_c->is_checked());
        EXPECT(load->get_value() == load_before);
    }

    // ABOUT opens the declarative overlay; CLOSE hides it
    {
        auto *about_btn = root(app).find_by_id("about");
        auto *close_btn = root(app).find_by_id("close_btn");
        EXPECT(about_btn != nullptr && close_btn != nullptr);
        // the overlay backdrop dims the left panel edge (a pixel the
        // about card itself never covers)
        const auto undimmed = px(app, 10, 120);
        click_center(app, *about_btn);
        app.paint();
        EXPECT(px(app, 10, 120) != undimmed);
        click_center(app, *close_btn);
        app.paint();
        EXPECT(px(app, 10, 120) == undimmed);
    }

    // RESET restores the boot state: throttle, systems, mode, alert,
    // trend seed -- even from a latched red alert
    {
        auto *reset_btn = root(app).find_by_id("reset");
        auto *alert_btn = root(app).find_by_id("alert");
        EXPECT(reset_btn != nullptr);
        click_center(app, *alert_btn);  // latch red first
        app.paint();
        click_center(app, *reset_btn);
        EXPECT(throttle->get_value() == 35);
        EXPECT(load->get_value() == boot_load);
        EXPECT(loadval->get_text() == utf8_to_utf16("57%"));
        EXPECT(trend->get_count() == 48);
        EXPECT(status->get_text() == u"BOOT STATE RESTORED");
        app.paint();
        EXPECT(theme().accent == accent0);  // mode 0 restored
    }

    return test::report("showcase");
}
