#include "showcase.hpp"

#include <cstdio>

#include "button.hpp"
#include "html.hpp"
#include "logging.hpp"
#include "text/utf8.hpp"
#include "theme.hpp"
#include "ui_builder.hpp"

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "text/runtime_ttf_provider.hpp"
#endif

#include "showcase_orion.gen.hpp"   // packed by html_embed at build time
#include "showcase_font.gen.hpp" // packed by bytes_embed at build time

namespace zb::app::showcase
{
    namespace
    {
        constexpr int kLogLines = 8;
        constexpr int kToastFrames = 84;   // ~1.4 s at 60 fps
        constexpr int kScanFrames = 54;    // DEEP SCAN link-busy window

        // ---- LONG RANGE constants ----
        constexpr int kPirateLegFrames = 480;   // pirate closes 100 -> 0
        constexpr int kEncounterFrames = 360;   // debris / ion-storm length
        constexpr int kFuelWarn = 200;          // 20%: engines cap at 40

        const char *kNodes[5] = {"LYRA-09", "CASS-22", "TALOS RIFT",
                                 "VEGA GATE", "KEPLER-442B"};
        const char *kHazards[4] = {"QUIET TRANSIT", "DEBRIS FIELD",
                                   "PIRATE PATROL", "ION STORM"};
        const char *kRoutes[6] = {"COREWARD SPUR", "RIMWARD DRIFT",
                                  "SHADOW LANE", "HIGH PASS", "DUST RUN",
                                  "BRIGHT CHANNEL"};

        // deterministic wave: a triangle, pure in the frame counter
        // (no timers, no RNG -- the F-2 app-side pattern)
        int wave_at(int frame)
        {
            const int phase = frame % 64;
            return (phase < 32) ? (phase * 28) / 32
                                : ((64 - phase) * 28) / 32;
        }

        void set_text(zb::ui::Widget *w, const std::string &s)
        {
            if (w != nullptr)
            {
                w->set_text(zb::ui::utf8_to_utf16(s.c_str()));
            }
        }

        int clamp_i(const int v, const int lo, const int hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        }
    }

    void Showcase::install_font()
    {
#if defined(IMCORE_HAS_TTF_RUNTIME)
        if (zb::ui::has_font_family())
        {
            return;
        }
        // from_memory borrows: the packed blob has static storage
        // (bytes_embed output) and outlives the family. The file path
        // form could never work on NDS (no filesystem) -- the blob is
        // the whole point of the bytes_embed build step
        static const zb::ui::TtfFamily family =
            zb::ui::TtfFamily::from_memory(showcase_font, showcase_font_len);
        zb::ui::set_font_family(family);
#endif
    }

    void Showcase::bind_button(const char *id, void (Showcase::*slot)())
    {
        if (auto *w = window_->root().find_by_id(id))
        {
            auto *btn = static_cast<zb::ui::Button *>(w);
            btn->clicked += [this, slot]()
            { (this->*slot)(); };
        }
    }

    // ---- the design's sync() formulas, integer arithmetic ----

    int Showcase::coolant() const
    {
        return clamp_i(86 - (react_->get_value() * 35) / 100, 28, 86);
    }

    int Showcase::temp_k() const
    {
        // 585 + r*1.55 + e*0.55 - s*0.22 (+ fault bump), summed first so
        // the rounding lands on the design's value (736 K at boot)
        return 585 + (react_->get_value() * 155 + engine_->get_value() * 55 -
                      shield_->get_value() * 22 + 50) /
                         100 +
               (damaged_ ? 38 : 0);
    }

    int Showcase::velocity_x1000() const
    {
        return clamp_i(410 + (engine_->get_value() * 420 +
                              react_->get_value() * 150 + 50) /
                                 100,
                       0, 995);
    }

    int Showcase::load_x10() const
    {
        return clamp_i((react_->get_value() * 45 + shield_->get_value() * 18 +
                        weapons_->get_value() * 8 + engine_->get_value() * 35 +
                        5) /
                           10,
                       0, 999);
    }

    int Showcase::avail_x10() const
    {
        // 15.2 PW - reactor draw - weapon draw
        return clamp_i((15200 - react_->get_value() * 35 -
                        weapons_->get_value() * 18 + 50) /
                           100,
                       21, 152);
    }

    std::string Showcase::mission_clock() const
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", mission_s_ / 3600,
                      (mission_s_ % 3600) / 60, mission_s_ % 60);
        return buf;
    }

    void Showcase::sync_power()
    {
        if (react_ == nullptr || shield_ == nullptr || weapons_ == nullptr ||
            engine_ == nullptr)
        {
            return;
        }
        const int r = react_->get_value();
        const int s = shield_->get_value();
        const int w = weapons_->get_value();
        const int e = engine_->get_value();

        char buf[48];
        std::snprintf(buf, sizeof(buf), "%d%%", r);
        set_text(react_v_, buf);
        std::snprintf(buf, sizeof(buf), "%d%%", s);
        set_text(shield_v_, buf);
        std::snprintf(buf, sizeof(buf), "%d%%", w);
        set_text(weapon_v_, buf);
        std::snprintf(buf, sizeof(buf), "%d%%", e);
        set_text(engine_v_, buf);

        // the four propulsion gauges track their bus directly
        if (g_react_ != nullptr)
        {
            g_react_->set_value(r);
        }
        if (g_thrust_ != nullptr)
        {
            g_thrust_->set_value(e);
        }
        if (g_shield_ != nullptr)
        {
            g_shield_->set_value(s);
        }
        if (g_cool_ != nullptr)
        {
            g_cool_->set_value(coolant());
        }
        set_text(gv_[0], std::to_string(r));
        set_text(gv_[1], std::to_string(e));
        set_text(gv_[2], std::to_string(s));
        set_text(gv_[3], std::to_string(coolant()));

        const int temp = temp_k();
        std::snprintf(buf, sizeof(buf), "%d K", temp);
        set_text(temp_v_, buf);
        if (temp_bar_ != nullptr)
        {
            temp_bar_->set_value(clamp_i((temp - 520) / 5, 22, 100));
        }

        const int vel = velocity_x1000();
        std::snprintf(buf, sizeof(buf), "0.%03d c", vel);
        set_text(velocity_, buf);
        if (vel_bar_ != nullptr)
        {
            vel_bar_->set_value(vel / 10);
        }
        set_text(vel_text_, vel >= 950 ? "approaching corridor limit"
                                       : "cruise corridor stable");

        // fuel and hull are stateful game resources (LONG RANGE)
        std::snprintf(buf, sizeof(buf), "%d.%d%%", fuel_x10_ / 10,
                      fuel_x10_ % 10);
        set_text(fuel_v_, buf);
        if (fuel_bar_ != nullptr)
        {
            fuel_bar_->set_value(fuel_x10_ / 10);
        }

        std::snprintf(buf, sizeof(buf), "%d.%d%%", hull_x10_ / 10,
                      hull_x10_ % 10);
        set_text(hull_v_, buf);
        if (hull_bar_ != nullptr)
        {
            hull_bar_->set_value(hull_x10_ / 10);
        }

        const int load = load_x10();
        std::snprintf(buf, sizeof(buf), "%d.%d%%", load / 10, load % 10);
        set_text(loadpct_, buf);

        const int avail = avail_x10();
        std::snprintf(buf, sizeof(buf), "%d.%d PW", avail / 10, avail % 10);
        set_text(avail_, buf);
    }

    // ---- command feedback ----

    void Showcase::toast(const char *msg)
    {
        set_text(toast_tx_, msg);
        if (toast_ != nullptr)
        {
            toast_->set_visible(true);
        }
        toast_frames_ = kToastFrames;
    }

    void Showcase::render_log()
    {
        static const zb::ui::core::Color kSeverity[3] = {
            zb::ui::core::Color::from(0x54, 0xf3, 0xa2, 255),  // ok
            zb::ui::core::Color::from(0xff, 0xd1, 0x6a, 255),  // warn
            zb::ui::core::Color::from(0xff, 0x5f, 0x79, 255),  // bad
        };
        for (int i = 0; i < kLogLines; ++i)
        {
            set_text(log_[i].t, entries_[i].time);
            set_text(log_[i].k, entries_[i].kind);
            if (log_[i].k != nullptr)
            {
                log_[i].k->set_text_color(kSeverity[entries_[i].severity]);
            }
            set_text(log_[i].m, entries_[i].msg);
        }
    }

    void Showcase::log_line(const char *severity, const char *tag,
                            const char *msg)
    {
        // prepend into the ring; the fixed l0..l7 rows re-render
        for (int i = kLogLines - 1; i > 0; --i)
        {
            entries_[i] = entries_[i - 1];
        }
        const int sev = severity[0] == 'o' ? 0 : severity[0] == 'w' ? 1 : 2;
        entries_[0] = {mission_clock(), tag, msg, sev};
        render_log();
    }

    void Showcase::maneuver(const char *what, const int heading_nudge_x10,
                            const int alt_nudge)
    {
        heading_x10_ = (heading_x10_ + heading_nudge_x10 + 3600) % 3600;
        alt_x1000_ = clamp_i(alt_x1000_ + alt_nudge, 20, 60);
        std::string what_s(what);
        toast(("GNC // " + what_s).c_str());
        log_line("ok", "GNC", ("manual maneuver " + what_s).c_str());
    }

    void Showcase::show_modal(const char *title, const char *text)
    {
        set_text(modal_title_, title);
        set_text(modal_text_, text);
        if (modal_ != nullptr)
        {
            modal_->set_visible(true);
        }
        modal_open_ = true;
        // the route buttons only ever show for a pending course choice
        // (open_map re-shows them); every other modal hides them
        if (opt_a_ != nullptr)
        {
            opt_a_->set_visible(false);
        }
        if (opt_b_ != nullptr)
        {
            opt_b_->set_visible(false);
        }
        // the toast paints above the modal backdrop (later sibling);
        // retire it so it can never intercept the modal's own clicks
        if (toast_ != nullptr)
        {
            toast_->set_visible(false);
        }
        toast_frames_ = 0;
        window_->invalidate();
    }

    void Showcase::hide_modal()
    {
        if (modal_ != nullptr)
        {
            modal_->set_visible(false);
        }
        modal_open_ = false;
        window_->invalidate();
    }

    void Showcase::build_ui(uint32_t w, uint32_t h)
    {
        bool ok = false;
        const auto &doc_file = kHtmlFiles[0];
        const std::string text(reinterpret_cast<const char *>(doc_file.data),
                               doc_file.size);
        zb::ui::ui_node doc = zb::ui::parse_html(text.c_str(), &ok);
        if (!ok)
        {
            // the packed document is validated at build time; a failure
            // here is an invariant break, not a runtime condition
            LE << "showcase: the packed design document does not parse";
            return;
        }
        window_->set_auto_layout(true);
        auto screen = std::make_unique<zb::ui::FlexPanel>();
        screen->set_size(static_cast<int>(w), static_cast<int>(h));
        // binding rides materialization (code-contract 4): each id
        // attaches to the widget its node created
        zb::ui::build(*screen, doc, [](const std::string &) {});
        window_->root().add_child(std::move(screen));

        zb::ui::Widget &root = window_->root();
        react_ = static_cast<zb::ui::Slider *>(root.find_by_id("react"));
        shield_ = static_cast<zb::ui::Slider *>(root.find_by_id("shield"));
        weapons_ = static_cast<zb::ui::Slider *>(root.find_by_id("weapons"));
        engine_ = static_cast<zb::ui::Slider *>(root.find_by_id("engine"));
        ion_sw_ = static_cast<zb::ui::ToggleSwitch *>(root.find_by_id("ion_sw"));
        grav_sw_ = static_cast<zb::ui::ToggleSwitch *>(root.find_by_id("grav_sw"));
        vent_sw_ = static_cast<zb::ui::ToggleSwitch *>(root.find_by_id("vent_sw"));
        g_react_ = static_cast<zb::ui::GaugeDial *>(root.find_by_id("g_react"));
        g_thrust_ = static_cast<zb::ui::GaugeDial *>(root.find_by_id("g_thrust"));
        g_shield_ = static_cast<zb::ui::GaugeDial *>(root.find_by_id("g_shield"));
        g_cool_ = static_cast<zb::ui::GaugeDial *>(root.find_by_id("g_cool"));
        vel_bar_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("vel_bar"));
        hull_bar_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("hull_bar"));
        temp_bar_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("temp_bar"));
        fuel_bar_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("fuel_bar"));
        mission_ = root.find_by_id("mission");
        link_ = root.find_by_id("link");
        avail_ = root.find_by_id("avail");
        loadpct_ = root.find_by_id("loadpct");
        react_v_ = root.find_by_id("react_v");
        shield_v_ = root.find_by_id("shield_v");
        weapon_v_ = root.find_by_id("weapon_v");
        engine_v_ = root.find_by_id("engine_v");
        gv_[0] = root.find_by_id("gv1");
        gv_[1] = root.find_by_id("gv2");
        gv_[2] = root.find_by_id("gv3");
        gv_[3] = root.find_by_id("gv4");
        velocity_ = root.find_by_id("velocity");
        vel_text_ = root.find_by_id("vel_text");
        hull_v_ = root.find_by_id("hull_v");
        temp_v_ = root.find_by_id("temp_v");
        fuel_v_ = root.find_by_id("fuel_v");
        heading_rd_ = root.find_by_id("heading_rd");
        coherence_ = root.find_by_id("coherence");
        flux_ = root.find_by_id("flux");
        core_tag_ = root.find_by_id("core_tag");
        segment_label_ = root.find_by_id("segment_label");
        status_ = root.find_by_id("status");
        sweep_ = root.find_by_id("scope_ring"); // the needle pseudo host
        modal_ = root.find_by_id("modal");
        modal_title_ = root.find_by_id("modal_title");
        modal_text_ = root.find_by_id("modal_text");
        toast_ = root.find_by_id("toast");
        toast_tx_ = root.find_by_id("toast_tx");
        sector_ = root.find_by_id("sector");
        ship_mk_ = root.find_by_id("ship_mk");
        for (int i = 0; i < 4; ++i)
        {
            char id[8];
            std::snprintf(id, sizeof(id), "mk%d", i);
            mk_[i] = root.find_by_id(id);
        }
        for (int i = 0; i < kLogLines; ++i)
        {
            char id[8];
            std::snprintf(id, sizeof(id), "l%d_t", i);
            log_[i].t = root.find_by_id(id);
            std::snprintf(id, sizeof(id), "l%d_k", i);
            log_[i].k = root.find_by_id(id);
            std::snprintf(id, sizeof(id), "l%d_m", i);
            log_[i].m = root.find_by_id(id);
        }

        // the power sliders drive every derived readout
        auto on_slider = [this](const int) { sync_power(); };
        react_->changed += on_slider;
        shield_->changed += on_slider;
        weapons_->changed += on_slider;
        engine_->changed += on_slider;

        // the system switches: command feedback only (the design keeps
        // them out of the load math)
        auto on_switch = [this](const bool on)
        {
            toast(on ? "SYSTEM ONLINE" : "SYSTEM OFFLINE");
            log_line(on ? "ok" : "warn", "PWR", "system power state changed");
        };
        ion_sw_->changed += on_switch;
        grav_sw_->changed += on_switch;
        vent_sw_->changed += on_switch;

        // header actions
        bind_button("auto", &Showcase::on_auto);
        bind_button("ship_map", &Showcase::on_ship_map);
        bind_button("fault", &Showcase::on_fault);
        bind_button("abort_btn", &Showcase::on_abort);
        // scope commands + weapons
        bind_button("scan", &Showcase::on_scan);
        bind_button("lock", &Showcase::on_lock);
        bind_button("laser", &Showcase::on_laser);
        bind_button("torpedo", &Showcase::on_torpedo);
        bind_button("point", &Showcase::on_point);
        bind_button("boost", &Showcase::on_boost);
        // maneuvers
        bind_button("mv_yaw_l", &Showcase::on_yaw_l);
        bind_button("mv_yaw_r", &Showcase::on_yaw_r);
        bind_button("mv_pitch_u", &Showcase::on_pitch_u);
        bind_button("mv_pitch_d", &Showcase::on_pitch_d);
        bind_button("mv_roll_l", &Showcase::on_roll_l);
        bind_button("mv_roll_r", &Showcase::on_roll_r);
        bind_button("mv_strafe_l", &Showcase::on_strafe_l);
        bind_button("mv_strafe_r", &Showcase::on_strafe_r);
        bind_button("close_btn", &Showcase::on_close_modal);
        // LONG RANGE: the star map's route and seed buttons
        bind_button("opt_a", &Showcase::on_route_a);
        bind_button("opt_b", &Showcase::on_route_b);
        bind_button("seed_a", &Showcase::on_seed_a);
        bind_button("seed_b", &Showcase::on_seed_b);
        bind_button("seed_c", &Showcase::on_seed_c);

        // the modal overlay and the toast are absolutely positioned in
        // the design; an absolute element's percent size does not
        // resolve against the containing block, so hand them their box
        // here (a resized buffer re-creates the app)
        if (modal_ != nullptr)
        {
            modal_->set_size(static_cast<int>(w), static_cast<int>(h));
        }
        if (toast_ != nullptr)
        {
            toast_->set_size(static_cast<int>(w) * 2 / 5, 24);
        }

        // boot event stream (the design document's opening rows)
        constexpr struct
        {
            const char *kind;
            const char *msg;
        } kBoot[kLogLines] = {
            {"ok", "CORE phase stabilized at 98.7%"},
            {"warn", "THERM port engine +17 K"},
            {"ok", "NAV target solution refreshed"},
            {"ok", "COMMS relay handoff accepted"},
            {"warn", "GNC micro-correction +0.013"},
            {"bad", "SCAN debris cluster @ 0.18 AU"},
            {"ok", "NAV route checksum verified"},
            {"ok", "CORE coolant loop pressure nominal"},
        };
        // stamp descending time-of-watch so the stream reads like the
        // design's opening log
        for (int i = 0; i < kLogLines; ++i)
        {
            const int t = mission_s_ - (kLogLines - 1 - i) * 23;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t / 3600,
                          (t % 3600) / 60, t % 60);
            entries_[i] = {buf, kBoot[i].kind, kBoot[i].msg,
                           kBoot[i].kind[0] == 'o'
                               ? 0
                               : (kBoot[i].kind[0] == 'w' ? 1 : 2)};
        }
        render_log();

        // boot the LONG RANGE voyage: leg 1's course comes rolled from
        // the default seed so the deck is playable immediately
        opt_a_ = root.find_by_id("opt_a");
        opt_b_ = root.find_by_id("opt_b");
        if (opt_a_ != nullptr)
        {
            opt_a_->set_visible(false);
        }
        if (opt_b_ != nullptr)
        {
            opt_b_->set_visible(false);
        }
        start_run(7);

        sync_power();
    }

    // ---- command slots ----

    void Showcase::on_auto()
    {
        autopilot_ = !autopilot_;
        set_text(status_, autopilot_ ? "AUTO-NAV GUIDANCE ENGAGED"
                                     : "MANUAL PILOT CONTROL");
        toast(autopilot_ ? "AUTO PILOT ENGAGED" : "MANUAL CONTROL");
        log_line(autopilot_ ? "ok" : "warn", "NAV",
                 autopilot_ ? "autopilot engaged" : "manual guidance engaged");
    }

    void Showcase::on_ship_map() { open_map(); }

    void Showcase::on_fault()
    {
        damaged_ = !damaged_;
        hull_x10_ = damaged_ ? 914 : 968;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d.%d%%", hull_x10_ / 10,
                      hull_x10_ % 10);
        set_text(hull_v_, buf);
        if (hull_bar_ != nullptr)
        {
            hull_bar_->set_value(hull_x10_ / 10);
        }
        set_text(core_tag_, damaged_ ? "DEGRADED" : "STABLE");
        set_text(segment_label_,
                 damaged_ ? "STAR-B \xc2\xb7 THERMAL ANOMALY \xc2\xb7 FAULT "
                            "ISOLATION ACTIVE"
                          : "STAR-B \xc2\xb7 THERMAL ANOMALY \xc2\xb7 REST "
                            "NOMINAL");
        set_text(status_, damaged_ ? "FAULT ISOLATION ACTIVE"
                                   : "ALL SYSTEMS NOMINAL");
        toast(damaged_ ? "FAULTS INJECTED" : "FAULTS CLEARED");
        log_line(damaged_ ? "bad" : "ok", "SYS",
                 damaged_
                     ? "simulation fault injected: starboard thermal loop"
                     : "simulation faults cleared");
        sync_power();  // the core temp reads the damaged state
    }

    void Showcase::on_abort()
    {
        if (engine_ != nullptr)
        {
            engine_->set_value(0);
        }
        sync_power();  // set_value is programmatic and silent
        show_modal("ABORT PROTOCOL // SIMULATION",
                   "EMERGENCY DECELERATION ARMED. All weapons safe. Reactor "
                   "preload reduced. Navigation solution retained. This is a "
                   "fictional interface demo; it is not connected to any real "
                   "vehicle or hardware.");
        log_line("bad", "SYS", "ABORT sequence armed");
    }

    void Showcase::on_scan()
    {
        scan_frames_ = kScanFrames;
        set_text(link_, "SCANNING");
        toast("DEEP SCAN // COMPLETE");
        log_line("ok", "SCAN",
                 "scan resolved 3 contacts \xc2\xb7 confidence 99.1%");
    }

    void Showcase::on_lock()
    {
        locked_ = !locked_;
        toast(locked_ ? "TARGET LOCKED" : "LOCK RELEASED");
        log_line(locked_ ? "ok" : "warn", "TAC",
                 locked_ ? "NX-ARC firing solution locked"
                         : "target lock released");
    }

    void Showcase::on_laser()
    {
        toast("PULSE CANNON // ARMED");
        log_line("warn", "WPN", "pulse cannon charge cycle initiated");
    }

    void Showcase::on_torpedo()
    {
        toast("KINETIC POD // SIMULATION");
        log_line("bad", "WPN", "kinetic pod launch simulation - no live payload");
    }

    void Showcase::on_point()
    {
        point_on_ = !point_on_;
        toast(point_on_ ? "POINT DEFENSE ONLINE" : "POINT DEFENSE OFFLINE");
    }

    void Showcase::on_boost()
    {
        if (shield_ != nullptr)
        {
            const int s = shield_->get_value();
            shield_->set_value(s > 82 ? 100 : s + 18);
        }
        sync_power();  // set_value is programmatic and silent
        toast("SHIELD SURGE // +18%");
        log_line("ok", "DEF", "shield capacitor surge complete");
    }

    void Showcase::on_yaw_l() { maneuver("YAW LEFT", -30, 0); }
    void Showcase::on_yaw_r() { maneuver("YAW RIGHT", 30, 0); }
    void Showcase::on_pitch_u() { maneuver("PITCH UP", 0, 2); }
    void Showcase::on_pitch_d() { maneuver("PITCH DOWN", 0, -2); }
    void Showcase::on_roll_l() { maneuver("ROLL LEFT", 0, 0); }
    void Showcase::on_roll_r() { maneuver("ROLL RIGHT", 0, 0); }
    void Showcase::on_strafe_l() { maneuver("STRAFE LEFT", 0, 0); }
    void Showcase::on_strafe_r() { maneuver("STRAFE RIGHT", 0, 0); }

    void Showcase::on_close_modal() { hide_modal(); }

    // ---- LONG RANGE: a deterministic voyage in four legs ----

    uint32_t Showcase::lcg()
    {
        // the classic linear congruential generator (Numerical Recipes
        // constants): the whole voyage is a pure function of the seed
        rng_ = rng_ * 1664525u + 1013904223u;
        return rng_ >> 8;  // 24 usable bits per roll
    }

    int Showcase::eff_engine() const
    {
        const int e = engine_ != nullptr ? engine_->get_value() : 0;
        return fuel_x10_ < kFuelWarn ? (e < 40 ? e : 40) : e;
    }

    uint32_t Showcase::recorder_hash() const
    {
        // FNV-1a over the recorded stream (the end-screen checksum
        // players compare to prove two runs played out identically)
        uint32_t h = 2166136261u;
        const auto mix = [&h](uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
            {
                h ^= v & 0xffu;
                h *= 16777619u;
                v >>= 8;
            }
        };
        mix(seed_);
        mix(static_cast<uint32_t>(rec_.size()));
        mix(static_cast<uint32_t>(frame_));
        for (const auto &ev : rec_)
        {
            mix(static_cast<uint32_t>(ev.type));
            mix(static_cast<uint32_t>(ev.x));
            mix(static_cast<uint32_t>(ev.y));
            mix(static_cast<uint32_t>(ev.key));
            mix(static_cast<uint32_t>(ev.ch));
        }
        return h;
    }

    void Showcase::start_run(uint32_t seed)
    {
        seed_ = seed;
        rng_ = seed;
        // a re-roll from the seed chips may land while a modal is up
        hide_modal();
        phase_ = Phase::Cruise;
        node_ = 0;
        fuel_x10_ = 834;
        hull_x10_ = 968;
        pod_charges_ = 2;
        route_pending_ = false;
        fuel_warned_ = false;
        cur_kind_ = EncounterKind::Quiet;
        shield_pool_x10_ = 640;
        pirate_hp_ = 0;
        // leg 1's course rolls immediately so the deck is playable at boot
        reroll_routes();
        cur_kind_ = opt_kind_[0];
        leg_left_ = opt_dist_[0];
        set_text(sector_, kNodes[0]);
        set_text(status_, "ALL SYSTEMS NOMINAL");
        sync_power();
        char buf[48];
        std::snprintf(buf, sizeof(buf), "long-range cruise engaged \xc2\xb7 seed %u",
                      seed_);
        log_line("ok", "NAV", buf);
        update_markers();
    }

    void Showcase::reroll_routes()
    {
        // the route table is a pure function of (seed, sector): the
        // rolls live on their own derived stream, so what the previous
        // leg was like -- hits taken, rocks shot -- cannot shift the
        // courses on offer (a seed always maps to the same voyage map)
        uint32_t rs = seed_ * 2654435761u +
                      static_cast<uint32_t>(node_) * 40503u + 7u;
        const auto roll = [&rs]() -> uint32_t
        {
            rs = rs * 1664525u + 1013904223u;
            return rs >> 8;
        };
        for (int i = 0; i < 2; ++i)
        {
            opt_kind_[i] = static_cast<EncounterKind>(roll() % 4);
            opt_dist_[i] = 700 + static_cast<int>(roll() % 500);
            opt_name_[i] = kRoutes[roll() % 6];
        }
    }

    void Showcase::open_map()
    {
        char buf[160];
        if (phase_ == Phase::Cruise)
        {
            std::snprintf(buf, sizeof(buf),
                          "STAR MAP // %s", kNodes[node_]);
            show_modal(buf, "");
            if (route_pending_)
            {
                std::snprintf(buf, sizeof(buf),
                              "LEG %d OF 4 \xc2\xb7 fuel %d.%d%% \xc2\xb7 hull "
                              "%d.%d%% \xc2\xb7 choose the transit route",
                              node_ + 1, fuel_x10_ / 10, fuel_x10_ % 10,
                              hull_x10_ / 10, hull_x10_ % 10);
                set_text(modal_text_, buf);
                if (opt_a_ != nullptr)
                {
                    std::snprintf(buf, sizeof(buf), "A \xc2\xb7 %s \xc2\xb7 %s",
                                  opt_name_[0].c_str(),
                                  kHazards[static_cast<int>(opt_kind_[0])]);
                    set_text(opt_a_, buf);
                    opt_a_->set_visible(true);
                }
                if (opt_b_ != nullptr)
                {
                    std::snprintf(buf, sizeof(buf), "B \xc2\xb7 %s \xc2\xb7 %s",
                                  opt_name_[1].c_str(),
                                  kHazards[static_cast<int>(opt_kind_[1])]);
                    set_text(opt_b_, buf);
                    opt_b_->set_visible(true);
                }
            }
            else
            {
                set_text(modal_text_, "course already laid in \xc2\xb7 the ship "
                                      "is under way");
            }
        }
        else if (phase_ == Phase::Encounter)
        {
            std::snprintf(buf, sizeof(buf), "STAR MAP // %s", kNodes[node_]);
            show_modal(buf, "");
            std::snprintf(buf, sizeof(buf),
                          "transit in progress \xc2\xb7 %s \xc2\xb7 the "
                          "helm and weapons are live",
                          kHazards[static_cast<int>(kind_)]);
            set_text(modal_text_, buf);
        }
        else
        {
            // Arrived / Lost: the map becomes the voyage recap
            show_modal(phase_ == Phase::Arrived
                           ? "STAR MAP // VOYAGE COMPLETE"
                           : "STAR MAP // VOYAGE LOST",
                       "");
            std::snprintf(buf, sizeof(buf),
                          "%s \xc2\xb7 seed %u \xc2\xb7 hull %d.%d%% \xc2\xb7 "
                          "fuel %d.%d%% \xc2\xb7 clock %s \xc2\xb7 FLIGHT "
                          "RECORDER: %d events \xc2\xb7 checksum %08X",
                          phase_ == Phase::Arrived
                              ? "Kepler-442B orbit reached"
                              : "the ORION was lost in transit",
                          seed_, hull_x10_ / 10, hull_x10_ % 10,
                          fuel_x10_ / 10, fuel_x10_ % 10,
                          mission_clock().c_str(), static_cast<int>(rec_.size()),
                          recorder_hash());
            set_text(modal_text_, buf);
        }
        log_line("ok", "NAV", "star map opened");
    }

    void Showcase::choose_route(int which)
    {
        if (!route_pending_ || phase_ != Phase::Cruise)
        {
            return;
        }
        cur_kind_ = opt_kind_[which];
        leg_left_ = opt_dist_[which];
        route_pending_ = false;
        hide_modal();
        set_text(status_, "ALL SYSTEMS NOMINAL");
        toast("COURSE LAID IN");
        log_line("ok", "NAV",
                 ("course: " + opt_name_[which] + " \xc2\xb7 " +
                  kHazards[static_cast<int>(cur_kind_)])
                     .c_str());
    }

    void Showcase::arrive_at_node()
    {
        ++node_;
        if (node_ >= 4)
        {
            end_game(true);
            return;
        }
        set_text(sector_, kNodes[node_]);
        route_pending_ = true;
        // fresh courses for the new sector: the derived stream makes
        // the offering a pure function of (seed, sector)
        reroll_routes();
        set_text(status_, "AWAITING COURSE");
        log_line("ok", "NAV",
                 ("sector reached: " + std::string(kNodes[node_])).c_str());
        toast("SECTOR REACHED // CHOOSE NEXT JUMP");
        open_map();
    }

    void Showcase::start_encounter(EncounterKind kind)
    {
        phase_ = Phase::Encounter;
        kind_ = kind;
        enc_left_ = kind == EncounterKind::Pirate ? kPirateLegFrames
                                                  : kEncounterFrames;
        if (autopilot_)
        {
            autopilot_ = false;
            log_line("warn", "NAV", "autopilot disengaged \xc2\xb7 manual "
                                    "guidance required");
        }
        ship_px_ = 50;
        ship_py_ = 50;
        ship_roll_ = 0;
        hit_cd_ = 0;
        cannon_cd_ = 0;
        switch (kind)
        {
        case EncounterKind::Debris:
            for (int i = 0; i < 3; ++i)
            {
                mk_x_[i] = static_cast<int>(lcg() % 100);
                mk_y_[i] = static_cast<int>(lcg() % 100);
                mk_vx_[i] = 1 + static_cast<int>(lcg() % 3);
                mk_vy_[i] = 1 + static_cast<int>(lcg() % 2);
                if ((lcg() & 1) != 0)
                {
                    mk_vx_[i] = -mk_vx_[i];
                }
                if ((lcg() & 1) != 0)
                {
                    mk_vy_[i] = -mk_vy_[i];
                }
            }
            break;
        case EncounterKind::Pirate:
            pirate_hp_ = 6;
            pirate_dist_ = 100;
            pirate_shot_cd_ = 150;
            shield_pool_x10_ = clamp_i(shield_->get_value() * 10, 100, 1000);
            break;
        case EncounterKind::IonStorm:
            for (int i = 0; i < 3; ++i)
            {
                mk_x_[i] = static_cast<int>(lcg() % 100);
                mk_y_[i] = static_cast<int>(lcg() % 100);
                mk_vx_[i] = mk_y_[i] % 3 - 1;
                mk_vy_[i] = mk_x_[i] % 3 - 1;
            }
            break;
        default:
            break;
        }
        set_text(link_, "CONTACT");
        set_text(status_, kHazards[static_cast<int>(kind)]);
        toast("CONTACT // EVADE OR ENGAGE");
        log_line("bad", "SCAN",
                 ("transit hazard: " + std::string(kHazards[static_cast<int>(kind)])).c_str());
        update_markers();
    }

    void Showcase::finish_encounter()
    {
        phase_ = Phase::Cruise;
        update_markers();
        set_text(link_, "STABLE");
        arrive_at_node();
    }

    void Showcase::damage_hull(int amount_x10, const char *tag,
                               const char *why)
    {
        hull_x10_ = clamp_i(hull_x10_ - amount_x10, 0, 968);
        sync_power();
        log_line("bad", tag, why);
        if (hull_x10_ <= 0)
        {
            end_game(false);
        }
    }

    void Showcase::end_game(bool won)
    {
        phase_ = won ? Phase::Arrived : Phase::Lost;
        update_markers();
        set_text(link_, won ? "DOCKED" : "LOST");
        set_text(status_, won ? "ARRIVAL CONFIRMED" : "SHIP LOST");
        char buf[224];
        std::snprintf(buf, sizeof(buf),
                      "%s \xc2\xb7 seed %u \xc2\xb7 hull %d.%d%% \xc2\xb7 fuel "
                      "%d.%d%% \xc2\xb7 clock %s \xc2\xb7 FLIGHT RECORDER: %d "
                      "events \xc2\xb7 checksum %08X",
                      won ? "KEPLER-442B ORBIT REACHED. The long cruise is "
                            "over; the deck stands down."
                          : "HULL COLLAPSE. The ORION breaks up in transit; "
                            "the recorder buoy carries the log home.",
                      seed_, hull_x10_ / 10, hull_x10_ % 10, fuel_x10_ / 10,
                      fuel_x10_ % 10, mission_clock().c_str(),
                      static_cast<int>(rec_.size()), recorder_hash());
        show_modal(won ? "ARRIVAL // KEPLER-442B" : "SHIP LOST", buf);
        log_line(won ? "ok" : "bad", "NAV",
                 won ? "voyage complete" : "ship lost in transit");
    }

    void Showcase::update_markers()
    {
        const bool enc = phase_ == Phase::Encounter;
        if (ship_mk_ != nullptr)
        {
            ship_mk_->set_visible(enc);
            if (enc)
            {
                ship_mk_->set_abs_offset(0, ship_px_, true);
                ship_mk_->set_abs_offset(1, ship_py_, true);
            }
        }
        const bool rocks = enc && kind_ == EncounterKind::Debris;
        const bool dust = enc && kind_ == EncounterKind::IonStorm;
        for (int i = 0; i < 3; ++i)
        {
            if (mk_[i] == nullptr)
            {
                continue;
            }
            mk_[i]->set_visible(rocks || dust);
            if (rocks || dust)
            {
                mk_[i]->set_abs_offset(0, clamp_i(mk_x_[i], 0, 98), true);
                mk_[i]->set_abs_offset(1, clamp_i(mk_y_[i], 0, 98), true);
            }
        }
        if (mk_[3] != nullptr)
        {
            const bool show = enc && kind_ == EncounterKind::Pirate;
            mk_[3]->set_visible(show);
            if (show)
            {
                mk_[3]->set_abs_offset(
                    0, clamp_i(50 + pirate_dist_ * 45 / 100, 0, 98), true);
                mk_[3]->set_abs_offset(1, 50, true);
            }
        }
    }

    Showcase::ScopeState Showcase::scope_state() const
    {
        ScopeState s;
        s.ship_x = ship_px_;
        s.ship_y = ship_py_;
        s.count = phase_ == Phase::Encounter &&
                          (kind_ == EncounterKind::Debris ||
                           kind_ == EncounterKind::IonStorm)
                      ? ScopeState::kMax
                      : 0;
        for (int i = 0; i < ScopeState::kMax; ++i)
        {
            s.x[i] = mk_x_[i];
            s.y[i] = mk_y_[i];
        }
        return s;
    }

    void Showcase::step_game()
    {
        if (phase_ == Phase::Cruise)
        {
            step_cruise();
        }
        else if (phase_ == Phase::Encounter)
        {
            step_encounter();
        }
    }

    void Showcase::step_cruise()
    {
        // fuel burns once per second of cruising (engines + housekeeping)
        if ((frame_ % 60) == 0)
        {
            const int burn = 2 + eff_engine() / 40;
            fuel_x10_ = fuel_x10_ > burn ? fuel_x10_ - burn : 0;
            if (fuel_x10_ < kFuelWarn && !fuel_warned_)
            {
                fuel_warned_ = true;
                toast("FUEL CRITICAL");
                log_line("warn", "FUEL",
                         "reserve below 20% \xc2\xb7 engines capped at 40%");
            }
        }
        if (leg_left_ > 0)
        {
            // throttle-scaled progress: ~4 units/frame at full burn,
            // so a 1000-unit leg cruises for seconds and the
            // encounters, not the transits, own the clock
            leg_left_ -= eff_engine() / 24;
            if (leg_left_ <= 0)
            {
                leg_left_ = 0;
                if (cur_kind_ == EncounterKind::Quiet)
                {
                    log_line("ok", "NAV", "quiet transit \xc2\xb7 no contacts");
                    arrive_at_node();
                }
                else
                {
                    start_encounter(cur_kind_);
                }
            }
        }
    }

    void Showcase::step_encounter()
    {
        if (cannon_cd_ > 0)
        {
            --cannon_cd_;
        }
        if (hit_cd_ > 0)
        {
            --hit_cd_;
        }
        if (ship_roll_ > 0)
        {
            --ship_roll_;
        }
        if (enc_left_ > 0)
        {
            --enc_left_;
        }

        if (kind_ == EncounterKind::Debris)
        {
            for (int i = 0; i < 3; ++i)
            {
                mk_x_[i] += mk_vx_[i];
                mk_y_[i] += mk_vy_[i];
                if (mk_x_[i] < 0 || mk_x_[i] > 100)
                {
                    mk_vx_[i] = -mk_vx_[i];
                    mk_x_[i] = clamp_i(mk_x_[i], 0, 100);
                }
                if (mk_y_[i] < 0 || mk_y_[i] > 100)
                {
                    mk_vy_[i] = -mk_vy_[i];
                    mk_y_[i] = clamp_i(mk_y_[i], 0, 100);
                }
                if (hit_cd_ == 0)
                {
                    const int r = ship_roll_ > 0 ? 4 : 7;
                    const int dx = mk_x_[i] - ship_px_;
                    const int dy = mk_y_[i] - ship_py_;
                    if (dx > -r && dx < r && dy > -r && dy < r)
                    {
                        damage_hull(30 + static_cast<int>(lcg() % 40), "SCAN",
                                    "micro-debris impact");
                        if (phase_ != Phase::Encounter)
                        {
                            return;  // the hit ended the voyage
                        }
                        hit_cd_ = 45;
                        // the rock breaks up; a fresh one drifts in
                        mk_x_[i] = (lcg() & 1) != 0 ? 100 : 0;
                        mk_y_[i] = static_cast<int>(lcg() % 100);
                    }
                }
            }
        }
        else if (kind_ == EncounterKind::Pirate)
        {
            pirate_dist_ = enc_left_ * 100 / kPirateLegFrames;
            if ((frame_ % 60) == 0)
            {
                // the capacitor regens off the shield bus (~3%/s at 64%)
                shield_pool_x10_ = clamp_i(
                    shield_pool_x10_ + shield_->get_value() / 2, 0, 1000);
            }
            if (pirate_dist_ < 45 && --pirate_shot_cd_ <= 0)
            {
                pirate_shot_cd_ = 90;
                const int shot = 160 + static_cast<int>(lcg() % 80);
                if (shield_pool_x10_ >= shot)
                {
                    shield_pool_x10_ -= shot;
                    log_line("warn", "DEF", "shields absorbed a salvo");
                }
                else
                {
                    const int pierce = shot - shield_pool_x10_;
                    shield_pool_x10_ = 0;
                    damage_hull(pierce, "WPN",
                                "pirate salvo pierced the shields");
                    if (phase_ != Phase::Encounter)
                    {
                        return;
                    }
                }
            }
        }
        else if (kind_ == EncounterKind::IonStorm)
        {
            for (int i = 0; i < 3; ++i)
            {
                mk_x_[i] = clamp_i(mk_x_[i] + mk_vx_[i], 0, 100);
                mk_y_[i] = clamp_i(mk_y_[i] + mk_vy_[i], 0, 100);
            }
            if ((frame_ % 60) == 0)
            {
                // the storm rides +130 K on the core (enough to breach
                // the 860 K overtemperature line at cruise settings)
                // unless thermal venting is open (+30 K)
                const bool venting =
                    vent_sw_ != nullptr && vent_sw_->is_checked();
                const int t = temp_k() + (venting ? 30 : 130);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d K", t);
                set_text(temp_v_, buf);
                if (temp_bar_ != nullptr)
                {
                    temp_bar_->set_value(clamp_i((t - 520) / 5, 22, 100));
                }
                if (t > 860)
                {
                    damage_hull(15, "THERM",
                                "core overtemperature inside the storm");
                    if (phase_ != Phase::Encounter)
                    {
                        return;
                    }
                }
            }
        }

        if (enc_left_ == 0)
        {
            if (kind_ == EncounterKind::Pirate && pirate_hp_ > 0)
            {
                damage_hull(100, "WPN", "pirate ram as the chase ended");
                if (phase_ != Phase::Encounter)
                {
                    return;
                }
            }
            finish_encounter();
        }
        else
        {
            update_markers();
        }
    }

    void Showcase::fire_cannon()
    {
        if (phase_ != Phase::Encounter)
        {
            toast("NO TARGET SOLUTION");
            return;
        }
        if (cannon_cd_ > 0)
        {
            toast("CANNON RECHARGING");
            return;
        }
        const int w = weapons_ != nullptr ? weapons_->get_value() : 0;
        if (w < 15)
        {
            toast("WEAPONS BUS OFFLINE");
            log_line("warn", "WPN",
                     "pulse cannon needs the weapons bus at 15%");
            return;
        }
        cannon_cd_ = 45;
        if (kind_ == EncounterKind::Pirate)
        {
            pirate_hp_ -= w >= 55 ? 2 : 1;
            if (pirate_hp_ <= 0)
            {
                fuel_x10_ = clamp_i(fuel_x10_ + 80, 0, 834);
                toast("PIRATE DESTROYED");
                log_line("ok", "WPN",
                         "pirate destroyed \xc2\xb7 salvage recovered +8%");
                finish_encounter();
                return;
            }
            toast("DIRECT HIT");
            log_line("ok", "WPN", "pulse cannon hit confirmed");
        }
        else if (kind_ == EncounterKind::Debris)
        {
            // fragment the closest rock; it respawns at the rim
            int best = 0;
            int best_d = 1 << 30;
            for (int i = 0; i < 3; ++i)
            {
                const int d = (mk_x_[i] - ship_px_) * (mk_x_[i] - ship_px_) +
                              (mk_y_[i] - ship_py_) * (mk_y_[i] - ship_py_);
                if (d < best_d)
                {
                    best_d = d;
                    best = i;
                }
            }
            mk_x_[best] = (lcg() & 1) != 0 ? 100 : 0;
            mk_y_[best] = static_cast<int>(lcg() % 100);
            mk_vx_[best] = 1 + static_cast<int>(lcg() % 3);
            if ((lcg() & 1) != 0)
            {
                mk_vx_[best] = -mk_vx_[best];
            }
            toast("ROCK FRAGMENTED");
            log_line("ok", "WPN", "rock fragmented by the pulse cannon");
        }
        else
        {
            toast("NO TARGET SOLUTION");
        }
    }

    void Showcase::launch_pod()
    {
        if (phase_ != Phase::Encounter)
        {
            toast("NO TARGET SOLUTION");
            return;
        }
        if (pod_charges_ <= 0)
        {
            toast("POD RACKS EMPTY");
            return;
        }
        --pod_charges_;
        if (kind_ == EncounterKind::Pirate)
        {
            pirate_hp_ -= 4;
            if (pirate_hp_ <= 0)
            {
                fuel_x10_ = clamp_i(fuel_x10_ + 80, 0, 834);
                toast("PIRATE DESTROYED");
                log_line("ok", "WPN",
                         "kinetic pod kill \xc2\xb7 salvage recovered +8%");
                finish_encounter();
                return;
            }
            toast("POD IMPACT");
            log_line("bad", "WPN", "kinetic pod impact confirmed");
        }
        else if (kind_ == EncounterKind::Debris)
        {
            for (int i = 0; i < 3; ++i)
            {
                mk_x_[i] = (lcg() & 1) != 0 ? 100 : 0;
                mk_y_[i] = static_cast<int>(lcg() % 100);
            }
            toast("FIELD CLEARED");
            log_line("ok", "WPN", "kinetic pod dispersed the rock field");
        }
        else
        {
            toast("NO TARGET SOLUTION");
        }
    }

    // ---- keyboard + pointer bindings (the design's KEYS hint line) ----

    void Showcase::input(const zb::input::input_event &ev) noexcept
    {
        // the flight recorder: seed + this stream replays the run
        if (rec_.size() < static_cast<size_t>(kReplayCap))
        {
            rec_.push_back(ev);
            rec_frame_.push_back(static_cast<uint32_t>(frame_));
        }
        else if (!rec_full_)
        {
            rec_full_ = true;
            LW << "showcase: flight recorder full, recording stopped";
        }

        // helm steering by pointer: a touch drag, or a mouse drag on
        // the scope (touch covers the wasm host, where the page maps
        // mouse input to the touch ABI)
        if (ev.type == zb::input::input_type::touch_move ||
            (ev.type == zb::input::input_type::mouse_move && drag_active_))
        {
            if (phase_ == Phase::Encounter && sweep_ != nullptr &&
                sweep_->get_size().width > 0)
            {
                const auto p = sweep_->get_absolute_position();
                const auto s = sweep_->get_size();
                ship_px_ = clamp_i(((ev.x - p.x) * 100) / s.width, 4, 96);
                ship_py_ = clamp_i(((ev.y - p.y) * 100) / s.height, 4, 96);
            }
        }
        if (ev.type == zb::input::input_type::mouse_left_down)
        {
            drag_active_ = true;
        }
        if (ev.type == zb::input::input_type::mouse_left_up)
        {
            drag_active_ = false;
        }

        if (ev.type == zb::input::input_type::key_down)
        {
            // printable keys arrive as `ch` (lowercase ASCII, U-1);
            // mapped keys (escape, space) arrive as `key`
            switch (ev.ch)
            {
            case 'w':
                if (phase_ == Phase::Encounter)
                {
                    ship_py_ = clamp_i(ship_py_ - 5, 4, 96);
                }
                else
                {
                    on_pitch_u();
                }
                break;
            case 's':
                if (phase_ == Phase::Encounter)
                {
                    ship_py_ = clamp_i(ship_py_ + 5, 4, 96);
                }
                else
                {
                    on_pitch_d();
                }
                break;
            case 'a':
                if (phase_ == Phase::Encounter)
                {
                    ship_px_ = clamp_i(ship_px_ - 5, 4, 96);
                }
                else
                {
                    on_yaw_l();
                }
                break;
            case 'd':
                if (phase_ == Phase::Encounter)
                {
                    ship_px_ = clamp_i(ship_px_ + 5, 4, 96);
                }
                else
                {
                    on_yaw_r();
                }
                break;
            case 'q':
            case 'e':
                if (phase_ == Phase::Encounter)
                {
                    ship_roll_ = 45;  // evasive profile ~0.75 s
                    toast("EVASIVE PROFILE");
                }
                else
                {
                    if (ev.ch == 'q')
                    {
                        on_roll_l();
                    }
                    else
                    {
                        on_roll_r();
                    }
                }
                break;
            case 'f':
                fire_cannon();
                break;
            case 'g':
                launch_pod();
                break;
            case 'm':
                if (modal_open_)
                {
                    hide_modal();
                }
                else
                {
                    on_ship_map();
                }
                break;
            case ' ':
                if (engine_ != nullptr)
                {
                    const int e = engine_->get_value();
                    engine_->set_value(e < 9 ? 0 : e - 9);
                }
                sync_power();
                toast("BRAKE VECTOR");
                break;
            default:
                if (ev.key == 27 && modal_open_)  // escape
                {
                    hide_modal();
                }
                break;
            }
        }
        window_->input(ev);
    }

    // ---- frame stepping ----

    void Showcase::advance_frame()
    {
        ++frame_;

        // LONG RANGE: one simulation step per frame; the voyage
        // pauses whenever the star map (or any modal) is open
        if (!modal_open_)
        {
            step_game();
        }

        // the radar sweep needle: the design's ::after pseudo (a
        // solid box -- rotation paints solid only), re-specified per
        // frame with the pivot at the needle's top-center (~1.5
        // deg/frame, the design's 4 s sweep)
        if (sweep_ != nullptr)
        {
            const zb::ui::Widget::pseudo_spec *sp = sweep_->pseudo(1);
            if (sp != nullptr)
            {
                zb::ui::Widget::pseudo_spec spec = *sp;
                spec.rot_ang =
                    static_cast<int16_t>((frame_ * 3) / 2 % 360);
                spec.rot_ox = 50;
                spec.rot_ox_pct = 1;
                spec.rot_oy = 0;
                spec.rot_oy_pct = 0;
                sweep_->set_pseudo(1, spec);
            }
        }

        // the mission clock + the quantum-core wobble, one tick per
        // second of frames
        if ((frame_ % 60) == 0)
        {
            ++mission_s_;
            set_text(mission_, mission_clock());
            const int coh = 985 + wave_at(frame_ / 60) - 14;
            char buf[40];
            std::snprintf(buf, sizeof(buf), "phase coherence %d.%d%%",
                          coh / 10, coh % 10);
            set_text(coherence_, buf);
            const int flux = 418 + wave_at(frame_ / 30) - 14;
            std::snprintf(buf, sizeof(buf),
                          "flux %d.%02d PW \xc2\xb7 jitter 0.02",
                          flux / 100, flux % 100);
            set_text(flux_, buf);
        }

        // DEEP SCAN busy window on the LINK readout
        if (scan_frames_ > 0 && --scan_frames_ == 0)
        {
            set_text(link_, "STABLE");
        }

        // the toast retires itself
        if (toast_frames_ > 0 && --toast_frames_ == 0)
        {
            if (toast_ != nullptr)
            {
                toast_->set_visible(false);
            }
        }

        // the deterministic event stream: one entry every ~8 s
        if ((frame_ % 480) == 0)
        {
            static const struct
            {
                const char *kind;
                const char *tag;
                const char *msg;
            } kStream[] = {
                {"ok", "NAV", "route checksum verified"},
                {"warn", "THERM", "thermal compensation recalculated"},
                {"ok", "CORE", "quantum phase trim complete"},
                {"warn", "ENV", "dust density +2.4%"},
                {"ok", "COMMS", "relay handshake accepted"},
                {"bad", "SCAN", "micro-debris contact classified"},
            };
            const auto &e = kStream[log_next_ % 6];
            ++log_next_;
            log_line(e.kind, e.tag, e.msg);
        }

        // the helm drifts with the engines (faster engines, faster
        // drift; a pure function of the ship state; cruising only --
        // in an encounter the scope markers own the helm)
        if (phase_ == Phase::Cruise && engine_ != nullptr &&
            engine_->get_value() > 0)
        {
            drift_acc_ += engine_->get_value();
            if (drift_acc_ >= 2160)
            {
                drift_acc_ = 0;
                heading_x10_ = (heading_x10_ + 1) % 3600;
            }
        }
        else
        {
            drift_acc_ = 0;
        }
        if ((frame_ % 8) == 0 && heading_rd_ != nullptr)
        {
            // short on purpose: the nav row shares its flex line with
            // the DEEP SCAN / LOCK buttons, and content that overflows
            // the row box is unpickable (containers clip hit-testing
            // to their own rect)
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "HDG %d.%d \xc2\xb7 ALT 0.0%dG",
                          heading_x10_ / 10, heading_x10_ % 10,
                          alt_x1000_);
            set_text(heading_rd_, buf);
        }
    }

    void Showcase::fast_forward(const int frames)
    {
        if (!ui_ready_)
        {
            return;
        }
        for (int i = 0; i < frames; ++i)
        {
            advance_frame();
        }
    }

    void Showcase::paint() noexcept
    {
        if (ui_ready_)
        {
            advance_frame();
            window_->invalidate();
        }
        window_->paint();
    }

    void Showcase::create_window(uint32_t w, uint32_t h, void *buffer)
    {
        zb::ui::set_theme(zb::ui::dark_theme());
        install_font();
        window_ = zb::make_shared<zb::app::CanvasWindow>();
        window_->create(w, h, buffer);
        build_ui(w, h);
        ui_ready_ = true;
    }
}
