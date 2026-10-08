// ORION NX-07 tests: the deck (the HTML design materializes, the power
// sliders drive every derived readout through the design's sync()
// formulas, FAULT INJECT / ABORT / SHIELD SURGE / DEEP SCAN behave as
// labeled, the mission clock ticks, the sweep needle rotates) and the
// LONG RANGE voyage game on top of it (seeded routes, the debris /
// pirate / ion-storm encounters, the star-map course choice, the
// arrival recap with the flight-recorder checksum, and the
// determinism contract: the same seed and input stream produce
// byte-identical framebuffers).
#include "test.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

    // "96.8%" -> 96 (the stateful hull/fuel readouts)
    int pct_of(const std::string &s) { return std::atoi(s.c_str()); }

    // "875 K" -> 875
    int kelvin_of(const std::string &s) { return std::atoi(s.c_str()); }

    // drive the frame loop until the footer reports the expected phase
    // (bounded; the game must reach it within max_frames)
    int drive_until(zb::app::showcase::Showcase &app, const char *status,
                    const int max_frames)
    {
        for (int i = 0; i < max_frames; ++i)
        {
            app.paint();
            if (text_of(app, "status") == status)
            {
                return i;
            }
        }
        return -1;
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

    // the boot readouts all sit at the design's opening values (fuel
    // and hull are stateful game resources seeded at 83.4% / 96.8%)
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
    EXPECT(text_of(app, "fuel_v") == "83.4%");
    EXPECT(text_of(app, "hull_v") == "96.8%");
    EXPECT(text_of(app, "loadpct") == "74.9%");
    EXPECT(text_of(app, "avail") == "12.0 PW");
    EXPECT(text_of(app, "mission") == "00:48:12");
    EXPECT(text_of(app, "status") == "ALL SYSTEMS NOMINAL");
    EXPECT(text_of(app, "sector") == "LYRA-09");

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

    // freeze the voyage for the remaining deck tests: leg progress
    // scales with the throttle, so engine 0 parks the run in a stable
    // cruise and no encounter can start under the assertions
    engine->set_value(0);
    app.sync_power();

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

    // keyboard: WASD/QE maneuver outside encounters, SPACE brakes,
    // M opens the star map (course already laid in, no route buttons)
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
        EXPECT(text_of(app, "modal_title") == "STAR MAP // LYRA-09");
        auto *opt_a = root(app).find_by_id("opt_a");
        auto *opt_b = root(app).find_by_id("opt_b");
        EXPECT(opt_a != nullptr && opt_b != nullptr);
        EXPECT(opt_a != nullptr && !opt_a->is_visible());
        EXPECT(opt_b != nullptr && !opt_b->is_visible());
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

    // ================= LONG RANGE: the voyage game =================
    // a fresh 320x240 app (state-driven assertions; the embedded tier
    // clips the columns but every widget id stays addressable). The
    // long legs are crossed through fast_forward() -- the simulation
    // without the rasterization -- and paint() runs only where a
    // click needs settled geometry
    {
        const auto vholder = make_app(320, 240);
        auto &vapp = *vholder;
        auto &vroot = root(vapp);
        auto *vengine = static_cast<Slider *>(vroot.find_by_id("engine"));
        auto *vweapons = static_cast<Slider *>(vroot.find_by_id("weapons"));
        auto *vent = static_cast<ToggleSwitch *>(vroot.find_by_id("vent_sw"));
        auto *ship_mk = vroot.find_by_id("ship_mk");
        auto *mk0 = vroot.find_by_id("mk0");
        auto *mk3 = vroot.find_by_id("mk3");
        auto *vmodal = vroot.find_by_id("modal");
        EXPECT(vengine != nullptr && vweapons != nullptr && vent != nullptr);
        EXPECT(ship_mk != nullptr && mk0 != nullptr && mk3 != nullptr);

        // the voyage runs on seed 22, whose route table (a pure
        // function of seed + sector) plays debris -> storm -> pirate
        // -> quiet on routes A-B-B-B -- every hazard class once
        vapp.start_run(22);
        EXPECT(text_of(vapp, "sector") == "LYRA-09");
        vengine->set_value(100);
        vapp.sync_power();
        EXPECT(text_of(vapp, "engine_v") == "100%");

        // leg 1 rolled BRIGHT CHANNEL / DEBRIS FIELD / 1194: at full
        // burn the leg progresses 4 units/frame, so the transit ends
        // 299 frames in. One rock spawns on the ship (a free hit);
        // then steer with the helm -- WASD in an encounter -- toward
        // the nearest contact marker until the next impact lands
        vapp.fast_forward(310);
        EXPECT(text_of(vapp, "status") == "DEBRIS FIELD");
        EXPECT(ship_mk->is_visible());
        EXPECT(mk0->is_visible());
        const int hull_before = pct_of(text_of(vapp, "hull_v"));
        // the helm works in the game's own unit space (percent of the scope
        // box -- the same units the WASD keys, the pointer drag and the
        // collision radius share), so the loop steers off the app's
        // scope state and not off the marker rects: the rects are that
        // state's pixel projection, and at the 320x240 tiers the scope
        // box is only a handful of pixels across (the runtime-TTF line
        // boxes take the height), so the old 3 px dead zone was a third
        // of the whole scope -- the helm never left the middle and no
        // impact ever landed
        for (int i = 0; i < 200; ++i)
        {
            if (pct_of(text_of(vapp, "hull_v")) < hull_before)
            {
                break;
            }
            vapp.paint();  // one sim frame + a layout pass for the rects
            const auto scope = vapp.scope_state();
            if (scope.count == 0)
            {
                break;  // the encounter ended (or was never a drifting one)
            }
            int best = 0;
            long best_d = 1L << 30;
            for (int k = 0; k < scope.count; ++k)
            {
                const long dx = scope.x[k] - scope.ship_x;
                const long dy = scope.y[k] - scope.ship_y;
                const long d = dx * dx + dy * dy;
                if (d < best_d)
                {
                    best_d = d;
                    best = k;
                }
            }
            // the helm steps 5 units a press and the impact radius is
            // 7: close each axis to within 2, dominant axis first
            const int dx = scope.x[best] - scope.ship_x;
            const int dy = scope.y[best] - scope.ship_y;
            if (dx > 2)
            {
                press_char(vapp, 'd');
            }
            else if (dx < -2)
            {
                press_char(vapp, 'a');
            }
            else if (dy > 2)
            {
                press_char(vapp, 's');
            }
            else if (dy < -2)
            {
                press_char(vapp, 'w');
            }
        }
        EXPECT(pct_of(text_of(vapp, "hull_v")) < hull_before);
        // the impact above proves the helm keys moved the ship (the collision
        // runs in the same unit space); this pins the rendering half of
        // that path -- the marker follows the scope box on every tier
        // (percent offsets against the containing block), however small
        // the box gets
        vapp.paint();
        {
            auto *scope = vroot.find_by_id("scope_ring");
            EXPECT(scope != nullptr);
            if (scope != nullptr)
            {
                const auto sp = scope->get_absolute_position();
                const auto ss = scope->get_size();
                const auto mp = ship_mk->get_absolute_position();
                EXPECT(mp.x >= sp.x && mp.y >= sp.y &&
                       mp.x <= sp.x + ss.width && mp.y <= sp.y + ss.height);
            }
        }
        // the pulse cannon fragments a rock (weapons bus at 22% >= 15)
        press_char(vapp, 'f');
        EXPECT(text_of(vapp, "toast_tx") == "ROCK FRAGMENTED");
        // ride out the 360-frame encounter; the sector modal opens
        vapp.fast_forward(400);
        EXPECT(text_of(vapp, "sector") == "CASS-22");
        EXPECT(text_of(vapp, "modal_title") == "STAR MAP // CASS-22");
        // leg 2 offers QUIET A / ION STORM B -- take the storm
        EXPECT(text_of(vapp, "opt_a") ==
               "A \xc2\xb7 RIMWARD DRIFT \xc2\xb7 QUIET TRANSIT");
        EXPECT(text_of(vapp, "opt_b") ==
               "B \xc2\xb7 HIGH PASS \xc2\xb7 ION STORM");
        vapp.paint();  // settle the modal layout for the click
        click_id(vapp, "opt_b");
        vapp.paint();
        EXPECT(vmodal != nullptr && !vmodal->is_visible());
        EXPECT(text_of(vapp, "status") == "ALL SYSTEMS NOMINAL");

        // leg 2 is 918 units (230 frames of cruise) into the ion
        // storm; the storm rides +130 K on the core while thermal
        // venting is closed -- past the 860 K overtemperature line, so
        // the hull bleeds; opening venting drops the bump to +30 K and
        // stops the bleed (the toggle is set programmatically: at the
        // 320x240 tier the switch row can sit outside the buffer)
        vapp.fast_forward(250);
        EXPECT(text_of(vapp, "status") == "ION STORM");
        const int storm_base = temp_of(81, 64, 100, false);
        const int hull_storm = pct_of(text_of(vapp, "hull_v"));
        vapp.fast_forward(130);
        EXPECT(pct_of(text_of(vapp, "hull_v")) < hull_storm);
        vent->set_checked(true);
        vapp.fast_forward(70);
        EXPECT(kelvin_of(text_of(vapp, "temp_v")) == storm_base + 30);
        const int hull_vented = pct_of(text_of(vapp, "hull_v"));
        vapp.fast_forward(120);
        EXPECT(pct_of(text_of(vapp, "hull_v")) == hull_vented);
        // the storm passes; leg 3 offers ION A / PIRATE B
        vapp.fast_forward(300);
        EXPECT(text_of(vapp, "sector") == "TALOS RIFT");
        EXPECT(text_of(vapp, "opt_a") ==
               "A \xc2\xb7 HIGH PASS \xc2\xb7 ION STORM");
        EXPECT(text_of(vapp, "opt_b") ==
               "B \xc2\xb7 HIGH PASS \xc2\xb7 PIRATE PATROL");
        vapp.paint();
        click_id(vapp, "opt_b");

        // leg 3 is 772 units (193 frames of cruise) into the pirate
        // patrol (480 frames long). Weapons at 55% -> two-damage
        // cannon hits; three hits (45-frame recharge) kill it well
        // before it gets into salvo range
        vweapons->set_value(55);
        vapp.sync_power();
        vapp.fast_forward(470);
        EXPECT(text_of(vapp, "status") == "PIRATE PATROL");
        EXPECT(mk3->is_visible());
        for (int i = 0; i < 3; ++i)
        {
            press_char(vapp, 'f');
            vapp.fast_forward(50);
        }
        // the kill prepends its log line, then the arrival prepends
        // its own -- but the deterministic event stream interleaves on
        // its own cadence, so scan the visible ring instead of
        // pinning rows
        vapp.fast_forward(150);
        EXPECT(text_of(vapp, "sector") == "VEGA GATE");
        std::string ring;
        for (int i = 0; i < 8; ++i)
        {
            char id[8];
            std::snprintf(id, sizeof(id), "l%d_m", i);
            auto *row = vroot.find_by_id(id);
            if (row != nullptr)
            {
                ring += text_of(vapp, id);
                ring += '\n';
            }
        }
        EXPECT(ring.find("sector reached: VEGA GATE") != std::string::npos);
        EXPECT(ring.find("pirate destroyed") != std::string::npos);

        // leg 4 offers DEBRIS A / QUIET B -- take the quiet lane home
        EXPECT(text_of(vapp, "opt_a") ==
               "A \xc2\xb7 HIGH PASS \xc2\xb7 DEBRIS FIELD");
        EXPECT(text_of(vapp, "opt_b") ==
               "B \xc2\xb7 DUST RUN \xc2\xb7 QUIET TRANSIT");
        vapp.paint();
        click_id(vapp, "opt_b");
        vapp.fast_forward(900);
        EXPECT(text_of(vapp, "status") == "ARRIVAL CONFIRMED");
        EXPECT(text_of(vapp, "modal_title") == "ARRIVAL // KEPLER-442B");
        const std::string recap = text_of(vapp, "modal_text");
        EXPECT(recap.find("FLIGHT RECORDER") != std::string::npos);
        EXPECT(recap.find("checksum") != std::string::npos);

        // the seed chips restart the whole voyage from the recap modal
        vapp.paint();
        click_id(vapp, "seed_b");
        vapp.paint();
        EXPECT(vmodal != nullptr && !vmodal->is_visible());
        EXPECT(text_of(vapp, "sector") == "LYRA-09");
        EXPECT(text_of(vapp, "status") == "ALL SYSTEMS NOMINAL");
        EXPECT(text_of(vapp, "hull_v") == "96.8%");
        EXPECT(text_of(vapp, "fuel_v") == "83.4%");
        // seed 42 leg 1 route A is DEBRIS (843 units -- 211 frames of
        // cruise at full burn); stop inside the encounter
        vapp.fast_forward(400);
        EXPECT(text_of(vapp, "status") == "DEBRIS FIELD");
    }

    // ================= determinism: the recorder contract =================
    // two fresh apps, the same seed, the same input schedule on the
    // same frames: the framebuffers must match byte-for-byte at every
    // checkpoint (this is what a shared flight-recorder code replays).
    // Real paints only -- this is the rendered-bytes contract
    {
        auto a = make_app(320, 240);
        auto b = make_app(320, 240);
        static_cast<Slider *>(root(*a).find_by_id("engine"))->set_value(55);
        static_cast<Slider *>(root(*b).find_by_id("engine"))->set_value(55);
        a->sync_power();
        b->sync_power();
        // 'd' every 37 frames: yaw during cruise, helm during the
        // encounter -- both recorded, both deterministic. Engine 55
        // crosses the leg-1 transit in ~537 frames, so the
        // checkpoints land mid-cruise
        for (int f = 0; f < 160; ++f)
        {
            if (f % 37 == 0)
            {
                press_char(*a, 'd');
                press_char(*b, 'd');
            }
            a->paint();
            b->paint();
            if (f == 50 || f == 100 || f == 159)
            {
                EXPECT(zb::snap::framebuffer_hash(win(*a)) ==
                       zb::snap::framebuffer_hash(win(*b)));
            }
        }
    }

    return test::report("showcase");
}
