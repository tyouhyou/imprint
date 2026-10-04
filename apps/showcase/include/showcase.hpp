#pragma once

#include <cstdint>
#include <memory>

#include "canvas_window.hpp"
#include "gauge_dial.hpp"
#include "iapp.hpp"
#include "progress_bar.hpp"
#include "slider.hpp"
#include "toggle_switch.hpp"
#include "trend_line.hpp"

namespace zb::app::showcase
{
    /*
     * SIGNAL-ONE flagship bridge (the ISV EVENT-HORIZON console): the UI
     * is one HTML design file (signal.html, packed by html_embed)
     * materialized through the declarative layer; the behavior lives in
     * this C++ class. A deterministic ship simulation is stepped purely
     * from the frame counter and the input stream: the throttle slider
     * drives warp / reactor load / core temp, the heading advances with
     * the throttle, the radar sweep needle rotates per frame (the
     * design's ::after pseudo re-specified through set_pseudo), the
     * system toggles feed the power draw, ALERT latches a red condition,
     * MODE cycles the accent themes, RESET restores boot state.
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
        void input(const zb::input::input_event &ev) noexcept override
        {
            window_->input(ev);
        }

        // one frame: advance the simulation first (the F-2 app-side
        // pattern -- pure function of the frame counter and the input
        // stream, no timers), then render
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

    private:
        void build_ui(uint32_t width, uint32_t height);
        void install_font();
        void apply_theme(const zb::ui::core::Color &accent);
        void apply_mode(int index);
        void apply_alert(bool on);
        void update_throttle(int value);
        void update_systems();
        void show_about();
        // current reactor load from the throttle + the online systems
        [[nodiscard]] int reactor_load() const;

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        zb::ui::Widget *about_overlay_ = nullptr;

        // handles into the materialized tree (the design file declares
        // the tags; this class reads them back through find_by_id)
        zb::ui::GaugeDial *helm_ = nullptr;
        zb::ui::GaugeDial *load_ = nullptr;
        zb::ui::ProgressBar *temp_ = nullptr;
        zb::ui::TrendLine *trend_ = nullptr;
        zb::ui::Slider *throttle_ = nullptr;
        zb::ui::ToggleSwitch *mod_a_ = nullptr;
        zb::ui::ToggleSwitch *mod_b_ = nullptr;
        zb::ui::ToggleSwitch *mod_c_ = nullptr;
        zb::ui::Button *alert_btn_ = nullptr;
        zb::ui::Widget *radar_ = nullptr;
        zb::ui::Widget *headval_ = nullptr;
        zb::ui::Widget *warpval_ = nullptr;
        zb::ui::Widget *brgval_ = nullptr;
        zb::ui::Widget *loadval_ = nullptr;
        zb::ui::Widget *tempval_ = nullptr;
        zb::ui::Widget *status_ = nullptr;
        zb::ui::Widget *fps_ = nullptr;

        int frame_ = 0;
        int mode_ = 0;
        bool alert_ = false;
        int heading_ = 0;
        int throttle_val_ = 35;
        bool ui_ready_ = false;
    };
}
