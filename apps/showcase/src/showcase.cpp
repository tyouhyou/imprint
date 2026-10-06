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

    int Showcase::fuel_x10() const
    {
        // x100 under the hood: 83.4% - engine trim - weapon draw
        const int f = 8340 - (engine_->get_value() - 72) * 6 -
                      (weapons_->get_value() * 18) / 10;
        return clamp_i(f / 10, 220, 834);
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

        const int fuel = fuel_x10();
        std::snprintf(buf, sizeof(buf), "%d.%d%%", fuel / 10, fuel % 10);
        set_text(fuel_v_, buf);
        if (fuel_bar_ != nullptr)
        {
            fuel_bar_->set_value(fuel / 10);
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

    void Showcase::on_ship_map()
    {
        show_modal("GALACTIC NAVIGATOR // LYRA-09",
                   "Route A \xc2\xb7 minimum thermal load \xc2\xb7 arrival "
                   "window 17d 06h \xc2\xb7 confidence 99.2%");
        log_line("ok", "NAV", "galactic navigator opened");
    }

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

    // ---- keyboard bindings (the design's KEYS hint line) ----

    void Showcase::input(const zb::input::input_event &ev) noexcept
    {
        if (ev.type == zb::input::input_type::key_down)
        {
            // printable keys arrive as `ch` (lowercase ASCII, U-1);
            // mapped keys (escape, space) arrive as `key`
            switch (ev.ch)
            {
            case 'w':
                on_pitch_u();
                break;
            case 's':
                on_pitch_d();
                break;
            case 'a':
                on_yaw_l();
                break;
            case 'd':
                on_yaw_r();
                break;
            case 'q':
                on_roll_l();
                break;
            case 'e':
                on_roll_r();
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

    void Showcase::paint() noexcept
    {
        if (ui_ready_)
        {
            ++frame_;

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
            // drift; a pure function of the ship state)
            if (engine_ != nullptr && engine_->get_value() > 0)
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
