#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "canvas_window.hpp"
#include "gauge_dial.hpp"
#include "iapp.hpp"
#include "progress_bar.hpp"
#include "slider.hpp"
#include "toggle_switch.hpp"

namespace zb::app::showcase
{
    /*
     * ORION NX-07 console: the command deck (design/
     * starship_console.html translated onto the declarative subset as
     * orion.html, packed by html_embed). The behavior lives in this C++
     * class as LONG RANGE, a deterministic voyage game stepped purely
     * from the frame counter and the input stream (the F-2 app-side
     * pattern -- no timers, no wall clock; all randomness flows through
     * a seedable LCG):
     *
     *   - the four power sliders recompute the derived readouts (core
     *     temp, velocity, bus load, available power, the four
     *     propulsion gauges) through the design's sync() formulas in
     *     integer arithmetic; fuel and hull are stateful game resources;
     *   - LONG RANGE: fly LYRA-09 -> KEPLER-442b in four legs. Each leg
     *     is chosen in the star-map modal (two routes with different
     *     hazards, rolled from the seed) and cruised by engine power;
     *     the transit hazard is played out on the tactical scope:
     *     debris fields are dodged with the helm (WASD, the mouse drag
     *     or a finger) or fragmented with the pulse cannon, pirates are
     *     shot down before they close to firing range, ion storms are
     *     survived by venting heat. Seeds 7/42/2026 in the map restart
     *     the whole voyage;
     *   - FAULT INJECT / ABORT / DEEP SCAN keep their deck roles;
     *   - the mission clock ticks off frames, the radar sweep needle
     *     rotates (the design's ::after pseudo re-specified through
     *     set_pseudo), the event stream and the command toast cycle
     *     deterministically;
     *   - the flight recorder keeps every input event with its frame
     *     number: seed + that stream IS the save game -- replaying it
     *     reproduces the framebuffer byte-for-byte (proven in
     *     test_showcase).
     *
     * Text renders through the runtime TTF family when the build carries
     * IMCORE_HAS_TTF_RUNTIME (the packed Inter blob); otherwise the 5x7
     * bitmap fallback applies (documented degradation, contract 2.4).
     */
    class Showcase : public IApp
    {
    public:
        Showcase() = default;
        ~Showcase() override = default;

        void create_window() override { create_window(320, 240); }
        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height) override
        {
            create_window(max_client_width, max_client_height, nullptr);
        }
        void create_window(uint32_t max_client_width, uint32_t max_client_height,
                           void *buffer) override;

        zb::SharedPtr<IWindow> window() noexcept override { return window_; }
        void input(const zb::input::input_event &ev) noexcept override;

        // one frame: advance the simulation first (pure function of the
        // frame counter and the input stream), then render
        void paint() noexcept override;

        // test seam (the sync_power() precedent): advance the simulation
        // n frames without rendering -- the state machine, the clock and
        // the recorder move exactly as they would under n paint() calls;
        // the next paint() renders whatever state was reached. The
        // voyage takes thousands of frames and the -O0 test builds pay
        // real rasterization cost per paint, so the state-driven tests
        // cross the long legs through here.
        void fast_forward(int frames);

        // start (or restart) the voyage on a seed: the seed chips and
        // the tests share this entry point
        void start_run(uint32_t seed);

        [[nodiscard]] bool is_dirty() const noexcept override
        {
            return window_->is_dirty();
        }
        [[nodiscard]] bool dirty_region(int &x, int &y, int &w, int &h) const noexcept override
        {
            return window_->dirty_region(x, y, w, h);
        }

        zb::event::Subscription<const void *> on_painting(zb::event::PAINT_EVENT::EventHandler h) noexcept override
        {
            return window_->painting.subscribe(h);
        }
        zb::event::Subscription<const void *> on_painted(zb::event::PAINT_EVENT::EventHandler h) noexcept override
        {
            return window_->painted.subscribe(h);
        }
        zb::event::Subscription<> on_closing(zb::event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_->closing.subscribe(h);
        }
        zb::event::Subscription<> on_closed(zb::event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_->closed.subscribe(h);
        }

        // the design's sync(): every derived readout from the four power
        // sliders (public so the tests can drive it directly)
        void sync_power();

    private:
        void build_ui(uint32_t width, uint32_t height);
        void install_font();
        void bind_button(const char *id, void (Showcase::*slot)());

        // command slots (buttons + the key bindings route here)
        void on_auto();
        void on_ship_map();
        void on_fault();
        void on_abort();
        void on_scan();
        void on_lock();
        void on_laser();
        void on_torpedo();
        void on_point();
        void on_boost();
        void on_yaw_l();
        void on_yaw_r();        void on_pitch_u();
        void on_pitch_d();
        void on_roll_l();
        void on_roll_r();
        void on_strafe_l();
        void on_strafe_r();
        void on_close_modal();
        // star-map route choices and the seed chips
        void on_route_a() { choose_route(0); }
        void on_route_b() { choose_route(1); }
        void on_seed_a() { start_run(7); }
        void on_seed_b() { start_run(42); }
        void on_seed_c() { start_run(2026); }
        void maneuver(const char *what, int heading_nudge_x10, int alt_nudge);

        // command feedback
        void toast(const char *msg);
        // severity: ok / warn / bad; tag: the stream's channel label
        // (CORE, THERM, NAV, ...)
        void log_line(const char *severity, const char *tag, const char *msg);
        void render_log();
        void show_modal(const char *title, const char *text);
        void hide_modal();

        // ---- LONG RANGE voyage game (seed-deterministic) ----
        enum class Phase : uint8_t
        {
            Cruise,
            Encounter,
            Arrived,
            Lost
        };
        enum class EncounterKind : uint8_t
        {
            Quiet,
            Debris,
            Pirate,
            IonStorm
        };
        void reroll_routes();
        void open_map();
        void choose_route(int which);
        void arrive_at_node();
        void start_encounter(EncounterKind kind);
        void finish_encounter();
        void fire_cannon();
        void launch_pod();
        void damage_hull(int amount_x10, const char *tag, const char *why);
        void end_game(bool won);
        void update_markers();
        // engine power the ship can actually spend (fuel-critical caps it)
        [[nodiscard]] int eff_engine() const;
        [[nodiscard]] uint32_t lcg();
        [[nodiscard]] uint32_t recorder_hash() const;
        void step_game();
        void step_cruise();
        void step_encounter();
        // the per-frame simulation paint() runs before rendering
        void advance_frame();

        // the design's sync() formulas, integer arithmetic
        [[nodiscard]] int coolant() const;
        [[nodiscard]] int temp_k() const;
        [[nodiscard]] int velocity_x1000() const;
        [[nodiscard]] int load_x10() const;
        [[nodiscard]] int avail_x10() const;
        [[nodiscard]] std::string mission_clock() const;

        zb::SharedPtr<zb::app::CanvasWindow> window_;

        // handles into the materialized tree (the design file declares
        // the tags; this class reads them back through find_by_id)
        zb::ui::Slider *react_ = nullptr;
        zb::ui::Slider *shield_ = nullptr;
        zb::ui::Slider *weapons_ = nullptr;
        zb::ui::Slider *engine_ = nullptr;
        zb::ui::ToggleSwitch *ion_sw_ = nullptr;
        zb::ui::ToggleSwitch *grav_sw_ = nullptr;
        zb::ui::ToggleSwitch *vent_sw_ = nullptr;
        zb::ui::GaugeDial *g_react_ = nullptr;
        zb::ui::GaugeDial *g_thrust_ = nullptr;
        zb::ui::GaugeDial *g_shield_ = nullptr;
        zb::ui::GaugeDial *g_cool_ = nullptr;
        zb::ui::ProgressBar *vel_bar_ = nullptr;
        zb::ui::ProgressBar *hull_bar_ = nullptr;
        zb::ui::ProgressBar *temp_bar_ = nullptr;
        zb::ui::ProgressBar *fuel_bar_ = nullptr;
        zb::ui::Widget *mission_ = nullptr;
        zb::ui::Widget *link_ = nullptr;
        zb::ui::Widget *avail_ = nullptr;
        zb::ui::Widget *loadpct_ = nullptr;
        zb::ui::Widget *react_v_ = nullptr;
        zb::ui::Widget *shield_v_ = nullptr;
        zb::ui::Widget *weapon_v_ = nullptr;
        zb::ui::Widget *engine_v_ = nullptr;
        zb::ui::Widget *gv_[4] = {nullptr, nullptr, nullptr, nullptr};
        zb::ui::Widget *velocity_ = nullptr;
        zb::ui::Widget *vel_text_ = nullptr;
        zb::ui::Widget *hull_v_ = nullptr;
        zb::ui::Widget *temp_v_ = nullptr;
        zb::ui::Widget *fuel_v_ = nullptr;
        zb::ui::Widget *heading_rd_ = nullptr;
        zb::ui::Widget *coherence_ = nullptr;
        zb::ui::Widget *flux_ = nullptr;
        zb::ui::Widget *core_tag_ = nullptr;
        zb::ui::Widget *segment_label_ = nullptr;
        zb::ui::Widget *status_ = nullptr;
        zb::ui::Widget *sweep_ = nullptr;   // the needle pseudo host
        zb::ui::Widget *modal_ = nullptr;
        zb::ui::Widget *modal_title_ = nullptr;
        zb::ui::Widget *modal_text_ = nullptr;
        zb::ui::Widget *toast_ = nullptr;
        zb::ui::Widget *toast_tx_ = nullptr;
        struct LogLine
        {
            zb::ui::Widget *t = nullptr;  // time stamp
            zb::ui::Widget *k = nullptr;  // kind (color carries severity)
            zb::ui::Widget *m = nullptr;  // message
        };
        LogLine log_[8] = {};

        // the event-stream ring (rendered into the fixed l0..l7 rows)
        struct LogEntry
        {
            std::string time;
            std::string kind;
            std::string msg;
            int severity = 0;  // 0 ok / 1 warn / 2 bad
        };
        LogEntry entries_[8];

        // ship state (boot values from the design document)
        int frame_ = 0;
        int mission_s_ = 48 * 60 + 12;
        int heading_x10_ = 2746;  // 274.6 deg
        int alt_x1000_ = 31;      // 0.031 G
        int hull_x10_ = 968;      // 96.8 %
        bool damaged_ = false;
        bool locked_ = false;
        bool autopilot_ = true;
        bool point_on_ = true;
        bool modal_open_ = false;
        int scan_frames_ = 0;     // DEEP SCAN link-busy countdown
        int toast_frames_ = 0;    // toast visibility countdown
        int log_next_ = 0;        // next periodic stream entry
        int drift_acc_ = 0;       // heading drift accumulator
        bool ui_ready_ = false;

        // ---- LONG RANGE state ----
        Phase phase_ = Phase::Cruise;
        EncounterKind cur_kind_ = EncounterKind::Quiet;  // this leg's hazard
        EncounterKind kind_ = EncounterKind::Quiet;      // live encounter
        EncounterKind opt_kind_[2] = {};                 // routes on offer
        int opt_dist_[2] = {};                           // leg length, units (~4/frame @ e=100)
        std::string opt_name_[2];                        // route names on offer
        uint32_t rng_ = 0;                               // LCG state
        uint32_t seed_ = 7;
        int node_ = 0;            // 0..4 along LYRA-09 -> KEPLER-442b
        int leg_left_ = 0;        // cruise distance left before the hazard
        int enc_left_ = 0;        // frames left in the encounter
        int fuel_x10_ = 834;      // stateful (the design's fuel gauge)
        int shield_pool_x10_ = 640;
        int cannon_cd_ = 0;       // pulse-cannon recharge frames
        int hit_cd_ = 0;          // i-frames after a hit
        int ship_roll_ = 0;       // evasive-profile frames (Q/E)
        int pod_charges_ = 2;
        int pirate_hp_ = 0;
        int pirate_dist_ = 0;     // 100 (far) .. 0 (on top of us)
        int pirate_shot_cd_ = 0;
        int ship_px_ = 50, ship_py_ = 50;  // helm marker, % of the scope
        bool route_pending_ = false;       // a course must be chosen
        bool fuel_warned_ = false;
        // rocks (debris) / dust (ion storm), % of the scope
        int mk_x_[3] = {}, mk_y_[3] = {};
        int mk_vx_[3] = {}, mk_vy_[3] = {};

        // the flight recorder: every input event with the frame it
        // arrived on. Seed + this stream is the whole save game; the
        // end-screen checksum lets players compare runs. Bounded: past
        // the cap the recorder stops (and says so once in the log).
        static constexpr int kReplayCap = 4096;
        std::vector<zb::input::input_event> rec_;
        std::vector<uint32_t> rec_frame_;
        bool rec_full_ = false;

        // markers inside the tactical scope (px-sized absolute dots,
        // repositioned per frame; scoped static svg contacts stay put)
        zb::ui::Widget *sector_ = nullptr;
        zb::ui::Widget *ship_mk_ = nullptr;
        zb::ui::Widget *mk_[4] = {};  // 0..2 rocks/dust, 3 the pirate
        zb::ui::Widget *opt_a_ = nullptr;  // star-map route buttons
        zb::ui::Widget *opt_b_ = nullptr;
        bool drag_active_ = false;    // mouse-drag helm steering
    };
}
