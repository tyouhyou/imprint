#pragma once

#include <cstdint>
#include <memory>
#include <string>

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
     * class as a deterministic ship simulation stepped purely from the
     * frame counter and the input stream (the F-2 app-side pattern --
     * no timers, no RNG):
     *
     *   - the four power sliders recompute the derived readouts (core
     *     temp, velocity, fuel, bus load, available power, the four
     *     propulsion gauges) through the design's sync() formulas in
     *     integer arithmetic;
     *   - the maneuver buttons (and the WASD/QE keys) nudge heading and
     *     altitude, SPACE brakes the engines, M toggles the star-map
     *     modal, ESC closes it;
     *   - FAULT INJECT degrades hull/core/segment labels, ABORT arms the
     *     protocol modal and zeroes the engines;
     *   - the mission clock ticks off frames, the radar sweep needle
     *     rotates (the design's ::after pseudo re-specified through
     *     set_pseudo), the event stream and the command toast cycle
     *     deterministically.
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
        void on_yaw_r();
        void on_pitch_u();
        void on_pitch_d();
        void on_roll_l();
        void on_roll_r();
        void on_strafe_l();
        void on_strafe_r();
        void on_close_modal();
        void maneuver(const char *what, int heading_nudge_x10, int alt_nudge);

        // command feedback
        void toast(const char *msg);
        // severity: ok / warn / bad; tag: the stream's channel label
        // (CORE, THERM, NAV, ...)
        void log_line(const char *severity, const char *tag, const char *msg);
        void render_log();
        void show_modal(const char *title, const char *text);
        void hide_modal();

        // the design's sync() formulas, integer arithmetic
        [[nodiscard]] int coolant() const;
        [[nodiscard]] int temp_k() const;
        [[nodiscard]] int velocity_x1000() const;
        [[nodiscard]] int fuel_x10() const;
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
    };
}
