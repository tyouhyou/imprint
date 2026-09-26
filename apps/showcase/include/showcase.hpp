#pragma once

#include <cstdint>
#include <memory>

#include "canvas_window.hpp"
#include "iapp.hpp"
#include "trend_line.hpp"

namespace zb::app::showcase
{
    /*
     * SIGNAL-ONE mission console (the redesigned showcase, 2026-09-26):
     * the UI is one HTML design file (signal.html, packed by html_embed)
     * materialized through the declarative layer; the behavior lives in
     * this C++ class -- knob->readout linkage, RESET / MODE / ABOUT
     * buttons, module toggles, a live telemetry feed, and a frame-hash
     * readout in the status line (the determinism story, on screen).
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

        // one frame: advance the live feed first (the F-2 app-side
        // pattern -- pure function of the frame counter, no timers),
        // then render
        void paint() noexcept override;

        [[nodiscard]] bool is_dirty() const noexcept override
        {
            return window_->is_dirty();
        }
        [[nodiscard]] bool dirty_region(int &x, int &y, int &w, int &h) const noexcept override
        {
            return window_->dirty_region(x, y, w, h);
        }

        void on_painting(zb::event::PAINT_EVENT::EventHandler h) noexcept override
        {
            window_->painting += h;
        }
        void on_painted(zb::event::PAINT_EVENT::EventHandler h) noexcept override
        {
            window_->painted += h;
        }
        void on_closing(zb::event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            window_->closing += h;
        }
        void on_closed(zb::event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            window_->closed += h;
        }

    private:
        void build_ui(uint32_t width, uint32_t height);
        void install_font();
        void apply_mode(int index);
        void update_gain(int value);
        void show_about();

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        zb::ui::Widget *about_overlay_ = nullptr;

        // handles into the materialized tree (the design file declares
        // the tags; this class reads them back through find_by_id)
        zb::ui::GaugeDial *load_ = nullptr;
        zb::ui::ProgressBar *temp_ = nullptr;
        zb::ui::TrendLine *trend_ = nullptr;
        zb::ui::Knob *gain_ = nullptr;
        zb::ui::Widget *loadval_ = nullptr;
        zb::ui::Widget *tempval_ = nullptr;
        zb::ui::Widget *gainval_ = nullptr;
        zb::ui::Widget *status_ = nullptr;
        zb::ui::Widget *fps_ = nullptr;

        int frame_ = 0;
        int mode_ = 0;
        bool ui_ready_ = false;
    };
}
