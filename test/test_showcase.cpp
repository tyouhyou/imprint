// SIGNAL-ONE showcase tests (2026-09-26 redesign): the HTML-designed
// console materializes, the knob->readout linkage tracks, the command
// buttons do their behaviors (MODE theme cycle, ABOUT overlay, RESET
// restore), and the live feed advances deterministically per frame.
#include "test.hpp"

#include "canvas_window.hpp"
#include "gauge_dial.hpp"
#include "input.hpp"
#include "knob.hpp"
#include "progress_bar.hpp"
#include "showcase.hpp"
#include "snapshot.hpp"
#include "theme.hpp"
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
        const auto win = app.window();
        const auto *pixels =
            static_cast<const zb::ui::core::Color *>(win->data());
        return pixels[y * win->width() + x].pixel;
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
}

int test_showcase()
{
    using namespace zb::ui;

    // the design materializes: the panel ids resolve to the expected
    // widget kinds, the design-file values landed, the boot theme is dark
    const auto holder = make_app(320, 240);
    auto &app = *holder;

    auto *load = static_cast<GaugeDial *>(root(app).find_by_id("load"));
    auto *temp = static_cast<ProgressBar *>(root(app).find_by_id("temp"));
    auto *trend = static_cast<TrendLine *>(root(app).find_by_id("trend"));
    auto *gain = static_cast<Knob *>(root(app).find_by_id("gain"));
    auto *gainval = root(app).find_by_id("gainval");
    auto *loadval = root(app).find_by_id("loadval");
    auto *status = root(app).find_by_id("status");
    EXPECT(load != nullptr && temp != nullptr && trend != nullptr &&
           gain != nullptr);
    EXPECT(gainval != nullptr && loadval != nullptr && status != nullptr);
    EXPECT(gain->get_value() == 35);
    EXPECT(load->get_value() == 35);  // the linkage applied at boot
    EXPECT(trend->get_count() == 48);  // seeded so the panel is never empty

    // knob -> readout linkage: a real drag updates the dB text, the load
    // gauge and its percentage label together
    {
        const auto p = gain->get_absolute_position();
        const auto s = gain->get_size();
        click(app, p.x + s.width / 2, p.y + s.height / 2);
        app.paint();
        zb::input::input_event key = {};
        key.type = zb::input::input_type::key_down;
        key.key = static_cast<int>(zb::input::key_code::right);
        app.input(key);
        app.input(key);
        app.paint();
        EXPECT(gain->get_value() == 37);
        EXPECT(load->get_value() == 37);
        EXPECT(loadval->get_text() == u"37%");
        EXPECT(gainval->get_text() == u"-8.8dB");  // 37/100 -> -8.8
    }

    // the live feed: one trend sample every other frame, pure in the
    // frame counter
    {
        // the feed scrolls the seeded window (count stays at the cap):
        // the rendered frame moves, the snapshot hash catches it
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
        const auto accent0 = theme().accent;
        click_center(app, *mode_btn);
        app.paint();
        const auto accent1 = theme().accent;
        EXPECT(!(accent1 == accent0));
        click_center(app, *mode_btn);
        click_center(app, *mode_btn);
        app.paint();
        EXPECT(theme().accent == accent0);  // three modes cycle back
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

    // RESET restores the boot state: knob, gauge, mode, trend seed
    {
        auto *reset_btn = root(app).find_by_id("reset");
        EXPECT(reset_btn != nullptr);
        click_center(app, *reset_btn);
        app.paint();
        EXPECT(gain->get_value() == 35);
        EXPECT(load->get_value() == 35);
        EXPECT(loadval->get_text() == u"35%");
        EXPECT(trend->get_count() == 48);
        EXPECT(status->get_text() == u"RESET OK");
    }

    return test::report("showcase");
}
