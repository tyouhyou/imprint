// ORION NX-07 command-deck tests: the HTML
// design materializes, the power sliders drive every derived readout
// through the design's sync() formulas, the maneuver buttons and the
// WASD/SPACE keys steer, FAULT INJECT / ABORT / SHIELD SURGE / DEEP SCAN
// behave as labeled, the star-map modal opens and closes, the mission
// clock ticks off frames, the radar sweep needle rotates, and the
// command toast retires itself.
#include "test.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

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

    void click_id(zb::app::showcase::Showcase &app, const char *id)
    {
        auto *w = root(app).find_by_id(id);
        EXPECT(w != nullptr);
        if (w != nullptr)
        {
            click_center(app, *w);
        }
    }

    void press_char(zb::app::showcase::Showcase &app, const char ch)
    {
        zb::input::input_event ev{};
        ev.type = zb::input::input_type::key_down;
        ev.ch = static_cast<int>(ch);
        app.input(ev);
    }

    void press_key(zb::app::showcase::Showcase &app,
                   const zb::input::key_code code)
    {
        zb::input::input_event ev{};
        ev.type = zb::input::input_type::key_down;
        ev.key = static_cast<int>(code);
        app.input(ev);
    }

    std::string text_of(zb::app::showcase::Showcase &app, const char *id)
    {
        auto *w = root(app).find_by_id(id);
        EXPECT(w != nullptr);
        if (w == nullptr)
        {
            return {};
        }
        // u16 -> u8 (BMP plane is all the app ever writes)
        const auto u16 = w->get_text();
        std::string out;
        for (const char16_t c : u16)
        {
            if (c < 0x80)
            {
                out.push_back(static_cast<char>(c));
            }
            else if (c < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xE0 | (c >> 12)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        return out;
    }

    // "HH:MM:SS" -> seconds (the mission-clock assertions)
    int clock_seconds(const std::string &s)
    {
        return ((s[0] - '0') * 10 + (s[1] - '0')) * 3600 +
               ((s[3] - '0') * 10 + (s[4] - '0')) * 60 +
               ((s[6] - '0') * 10 + (s[7] - '0'));
    }

    // ---- the published sync() formulas (mirrored from the app) ----

    int coolant_of(const int r) { return std::max(28, 86 - (r * 35) / 100); }

    int temp_of(const int r, const int s, const int e, const bool damaged)
    {
        return 585 + (r * 155 + e * 55 - s * 22 + 50) / 100 + (damaged ? 38 : 0);
    }

    int vel_of(const int r, const int e)
    {
        return std::min(995, 410 + (e * 420 + r * 150 + 50) / 100);
    }

    int fuel10_of(const int w, const int e)
    {
        return std::min(834, std::max(220, (8340 - (e - 72) * 6 -
                                            (w * 18) / 10) /
                                               10));
    }

    int load10_of(const int r, const int s, const int w, const int e)
    {
        return std::min(999, (r * 45 + s * 18 + w * 8 + e * 35 + 5) / 10);
    }

    int avail10_of(const int r, const int w)
    {
        return std::max(21, std::min(152, (15200 - r * 35 - w * 18 + 50) / 100));
    }
}

int test_showcase()
{
    using namespace zb::ui;

    // boot state: r=81 s=64 w=22 e=72 (the design document's sliders).
    // The ORION deck is a dense desktop-console design -- the buffer
    // mirrors the wide tier the wasm host picks from the viewport; the
    // 320x240 embedded tier clips the side columns by design
    const auto holder = make_app(800, 600);
    auto &app = *holder;

    auto *react = static_cast<Slider *>(root(app).find_by_id("react"));
    auto *shield = static_cast<Slider *>(root(app).find_by_id("shield"));
    auto *weapons = static_cast<Slider *>(root(app).find_by_id("weapons"));
    auto *engine = static_cast<Slider *>(root(app).find_by_id("engine"));
    auto *ion_sw = static_cast<ToggleSwitch *>(root(app).find_by_id("ion_sw"));
    auto *grav_sw = static_cast<ToggleSwitch *>(root(app).find_by_id("grav_sw"));
    auto *vent_sw = static_cast<ToggleSwitch *>(root(app).find_by_id("vent_sw"));
    auto *g_react = static_cast<GaugeDial *>(root(app).find_by_id("g_react"));
    auto *g_thrust = static_cast<GaugeDial *>(root(app).find_by_id("g_thrust"));
    auto *g_shield = static_cast<GaugeDial *>(root(app).find_by_id("g_shield"));
    auto *g_cool = static_cast<GaugeDial *>(root(app).find_by_id("g_cool"));
    auto *vel_bar = static_cast<ProgressBar *>(root(app).find_by_id("vel_bar"));
    auto *hull_bar = static_cast<ProgressBar *>(root(app).find_by_id("hull_bar"));
    auto *temp_bar = static_cast<ProgressBar *>(root(app).find_by_id("temp_bar"));
    auto *fuel_bar = static_cast<ProgressBar *>(root(app).find_by_id("fuel_bar"));
    auto *sweep = root(app).find_by_id("scope_ring"); // the pseudo host
    auto *toast = root(app).find_by_id("toast");
    EXPECT(react != nullptr && shield != nullptr && weapons != nullptr &&
           engine != nullptr);
    EXPECT(ion_sw != nullptr && grav_sw != nullptr && vent_sw != nullptr);
    EXPECT(g_react != nullptr && g_thrust != nullptr && g_shield != nullptr &&
           g_cool != nullptr);
    EXPECT(vel_bar != nullptr && hull_bar != nullptr && temp_bar != nullptr &&
           fuel_bar != nullptr);
    EXPECT(sweep != nullptr && toast != nullptr);

    // the boot readouts all sit at the design's opening values
    EXPECT(react->get_value() == 81 && shield->get_value() == 64 &&
           weapons->get_value() == 22 && engine->get_value() == 72);
    EXPECT(ion_sw->is_checked() && grav_sw->is_checked() &&
           !vent_sw->is_checked());
    EXPECT(g_react->get_value() == 81 && g_thrust->get_value() == 72 &&
           g_shield->get_value() == 64 && g_cool->get_value() == coolant_of(81));
    EXPECT(vel_bar->get_value() == 83 && hull_bar->get_value() == 96 &&
           temp_bar->get_value() == 43 && fuel_bar->get_value() == 83);
    EXPECT(text_of(app, "temp_v") == "736 K");
    EXPECT(text_of(app, "velocity") == "0.834 c");
    EXPECT(text_of(app, "fuel_v") == "83.0%");
    EXPECT(text_of(app, "loadpct") == "74.9%");
    EXPECT(text_of(app, "avail") == "12.0 PW");
    EXPECT(text_of(app, "mission") == "00:48:12");
    EXPECT(text_of(app, "status") == "ALL SYSTEMS NOMINAL");

    // slider -> readout linkage: focus the engine slider (the click
    // lands mid-track), step it with an arrow, every derived readout
    // follows the published formulas
    {
        const auto p = engine->get_absolute_position();
        const auto s = engine->get_size();
        click(app, p.x + s.width / 2, p.y + s.height / 2);
        app.paint();
        const int e0 = engine->get_value();
        press_key(app, zb::input::key_code::right);
        app.paint();
        const int e1 = engine->get_value();
        EXPECT(e1 == e0 + 1);
        const int r = 81, s2 = 64, w = 22;
        EXPECT(text_of(app, "engine_v") == std::to_string(e1) + "%");
        EXPECT(g_thrust->get_value() == e1);
        char buf[24];
        std::snprintf(buf, sizeof(buf), "0.%03d c", vel_of(r, e1));
        EXPECT(text_of(app, "velocity") == buf);
        std::snprintf(buf, sizeof(buf), "%d K", temp_of(r, s2, e1, false));
        EXPECT(text_of(app, "temp_v") == buf);
        const int f = fuel10_of(w, e1);
        std::snprintf(buf, sizeof(buf), "%d.%d%%", f / 10, f % 10);
        EXPECT(text_of(app, "fuel_v") == buf);
        const int l = load10_of(r, s2, w, e1);
        std::snprintf(buf, sizeof(buf), "%d.%d%%", l / 10, l % 10);
        EXPECT(text_of(app, "loadpct") == buf);
        const int a = avail10_of(r, w);
        std::snprintf(buf, sizeof(buf), "%d.%d PW", a / 10, a % 10);
        EXPECT(text_of(app, "avail") == buf);
        // restore the boot level through the same channel
        press_key(app, zb::input::key_code::left);
        app.paint();
        EXPECT(engine->get_value() == e0);
    }

    // maneuver buttons: YAW RIGHT nudges the heading, the toast names
    // the command, and the readout row picks the new heading up
    {
        click_id(app, "mv_yaw_r");
        EXPECT(toast->is_visible());
        EXPECT(text_of(app, "toast_tx") == "GNC // YAW RIGHT");
        for (int i = 0; i < 8; ++i)
        {
            app.paint();
        }
        EXPECT(text_of(app, "heading_rd").find("HDG 277.6") !=
               std::string::npos);
        // the toast retires itself after its visibility window
        for (int i = 0; i < 90; ++i)
        {
            app.paint();
        }
        EXPECT(!toast->is_visible());
    }

    // SHIELD SURGE: +18 on the shield bus (clamped into the slider)
    {
        click_id(app, "boost");
        EXPECT(shield->get_value() == 82);
        EXPECT(text_of(app, "shield_v") == "82%");
        EXPECT(g_shield->get_value() == 82);
        EXPECT(text_of(app, "toast_tx") == "SHIELD SURGE // +18%");
    }

    // keyboard: WASD/QE maneuver, SPACE brakes, M opens the map
    {
        press_char(app, 'a');
        EXPECT(text_of(app, "toast_tx") == "GNC // YAW LEFT");
        press_char(app, 'w');
        EXPECT(text_of(app, "toast_tx") == "GNC // PITCH UP");
        const int e_before = engine->get_value();
        press_char(app, ' ');
        EXPECT(engine->get_value() == (e_before < 9 ? 0 : e_before - 9));
        EXPECT(text_of(app, "toast_tx") == "BRAKE VECTOR");
        EXPECT(app.window() != nullptr);
        press_char(app, 'm');
        auto *modal = root(app).find_by_id("modal");
        EXPECT(modal != nullptr && modal->is_visible());
        EXPECT(text_of(app, "modal_title") ==
               "GALACTIC NAVIGATOR // LYRA-09");
        press_char(app, 'm');  // M toggles the map closed again
        EXPECT(modal != nullptr && !modal->is_visible());
    }

    // FAULT INJECT degrades the hull, the core tag and the segment
    // label; the second click clears everything
    {
        click_id(app, "fault");
        EXPECT(text_of(app, "hull_v") == "91.4%");
        EXPECT(text_of(app, "core_tag") == "DEGRADED");
        EXPECT(text_of(app, "status") == "FAULT ISOLATION ACTIVE");
        // the core temp reads the +38 K fault bump
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d K",
                      temp_of(81, 82, engine->get_value(), true));
        EXPECT(text_of(app, "temp_v") == buf);
        click_id(app, "fault");
        EXPECT(text_of(app, "hull_v") == "96.8%");
        EXPECT(text_of(app, "core_tag") == "STABLE");
        EXPECT(text_of(app, "status") == "ALL SYSTEMS NOMINAL");
    }

    // ABORT zeroes the engines and arms the protocol modal; CLOSE
    // dismisses it
    {
        click_id(app, "abort_btn");
        EXPECT(engine->get_value() == 0);
        auto *modal = root(app).find_by_id("modal");
        EXPECT(modal != nullptr && modal->is_visible());
        EXPECT(text_of(app, "modal_title") == "ABORT PROTOCOL // SIMULATION");
        click_id(app, "close_btn");
        app.paint();
        EXPECT(modal != nullptr && !modal->is_visible());
    }

    // DEEP SCAN: the LINK readout goes busy and settles back
    {
        click_id(app, "scan");
        EXPECT(text_of(app, "link") == "SCANNING");
        for (int i = 0; i < 60; ++i)
        {
            app.paint();
        }
        EXPECT(text_of(app, "link") == "STABLE");
    }

    // the event stream: a new command prepends, the ring shifts down
    {
        click_id(app, "laser");
        EXPECT(text_of(app, "l0_k") == "WPN");
        EXPECT(text_of(app, "l0_m") == "pulse cannon charge cycle initiated");
        EXPECT(text_of(app, "l1_m") == "scan resolved 3 contacts \xc2\xb7 "
                                       "confidence 99.1%");
    }

    // the mission clock ticks exactly once per second of frames
    // (relative: earlier blocks already burned frames)
    {
        const int before = clock_seconds(text_of(app, "mission"));
        for (int i = 0; i < 60; ++i)
        {
            app.paint();
        }
        EXPECT(clock_seconds(text_of(app, "mission")) == before + 1);
    }

    // the radar sweep needle: the design's ::after pseudo re-specified
    // per frame (paint-only box, no layout)
    {
        const auto *before = sweep->pseudo(1);
        EXPECT(before != nullptr);
        const auto ang0 = before->rot_ang;
        app.paint();
        const auto *after = sweep->pseudo(1);
        EXPECT(after != nullptr && after->rot_ang != ang0);
    }

    // the rendered frame moves (the sweep needle rotates every frame;
    // ten frames also cross the heading-readout refresh)
    {
        const auto h0 = zb::snap::framebuffer_hash(win(app));
        for (int i = 0; i < 10; ++i)
        {
            app.paint();
        }
        const auto h1 = zb::snap::framebuffer_hash(win(app));
        EXPECT(h0 != h1);
    }

    // the system switches give command feedback (and stay out of the
    // load math, per the design)
    {
        const std::string load_before = text_of(app, "loadpct");
        click_center(app, *vent_sw);
        EXPECT(vent_sw->is_checked());
        EXPECT(text_of(app, "toast_tx") == "SYSTEM ONLINE");
        EXPECT(text_of(app, "loadpct") == load_before);
        click_center(app, *vent_sw);
        EXPECT(!vent_sw->is_checked());
    }

    return test::report("showcase");
}
