#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "tween.hpp"

namespace zb::ui
{
    class Button;
    class FlexPanel;
    class GaugeDial;
    class Knob;
    class Label;
    class ListBox;
    class Panel;
    class ProgressBar;
    class Slider;
    class ToggleSwitch;
    class TrendLine;
}

namespace zb::app::showcase
{
    class HeroChart;
    class ShadowCard;
    class AlphaImage;
/*
     * S3 showcase: three embedded .ui pages behind one window.
     *
     * Page 0 "hero" is a device control panel: three progress bars
     * advanced deterministically by input events while running (START /
     * STOP), a status label updated only on discrete transitions, and a
     * dark/light theme toggle. Page 1 "gallery" shows every widget and
     * wires the demo slider to the demo bar. Page 2 "dashboard" (V-5
     * step 3) is the factory-console face of the framework: live
     * GaugeDials, a scrolling TrendLine, a setpoint Knob synced to a
     * slider, pump/coolant ToggleSwitches feeding an alarm list, and a
     * self-check panel that drives the knobs with real input events and
     * stamps the result "PIXELS MATCH" when two renders of the same
     * state hash byte-identically.
     *
     * All pages are packed at build time by ui_embed and parsed back
     * with the library parser -- the same code path shipped embedded
     * apps use (no filesystem needed, NDS included).
     */
    class Showcase : public IApp
    {
    public:
        Showcase() = default;
        ~Showcase() override = default;

        void create_window() override { create_window(_width, _height); }
        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height) override;
        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height, void *buffer) override;

        zb::SharedPtr<IWindow> window() noexcept override;
        void input(const zb::input::input_event &ev) noexcept override;
        void paint() noexcept override;
        bool is_dirty() const noexcept override;
        bool dirty_region(int &, int &, int &, int &) const noexcept override;
        void on_painting(event::PAINT_EVENT::EventHandler) noexcept override;
        void on_painted(event::PAINT_EVENT::EventHandler) noexcept override;
        void on_closing(event::CLOSE_EVENT::EventHandler) noexcept override;
        void on_closed(event::CLOSE_EVENT::EventHandler) noexcept override;

    private:
        void make_window(uint32_t max_client_width,
                         uint32_t max_client_height, void *buffer);
        void load_pages();
        void install_chart();
        void install_assets();
        void install_dashboard();
        void wire_controls();
        void show_page(int index);
        void set_state(const char *text);
        void start();
        void stop();
        void replay();
        void toggle_theme();
        void advance();
        void advance_dashboard();
        void update_input_echo(const zb::input::input_event &ev);
        void run_bench();
        void bench_check_hash(int half);
        [[nodiscard]] uint32_t buffer_hash() const;

        // default desktop size (4:3, like FB 320x240 and NDS/WASM
        // 256x192 — the pages scale to whatever the shell passes)
        int32_t _width{640};
        int32_t _height{480};

        bool dark_ = false;
        bool running_ = false;

        // F-2 preview glue: the chart reveal, one step per paint request
        Tween reveal_{};

        zb::SharedPtr<CanvasWindow> window_;
        // Panel layout stacks children unconditionally, so exactly one
        // page is mounted under the root at a time; parked pages live in
        // the array, mounted_ points at the one in the tree (owned by it)
        std::unique_ptr<zb::ui::FlexPanel> pages_[3];
        zb::ui::FlexPanel *mounted_ = nullptr;
        int current_ = 0;

        HeroChart *chart_ = nullptr;

        // V-2 asset materialization: the RGBA8 arrays from asset_gen
        // converted once into the build's pixel layout (the buffers the
        // gallery's image views point at must outlive the widgets)
        std::vector<zb::ui::core::Color> asset_pixels_;
        zb::ui::core::image_t ball_img_{};
        zb::ui::core::image_t shadow_img_{};

        zb::ui::ProgressBar *cpu_bar_ = nullptr;
        zb::ui::ProgressBar *mem_bar_ = nullptr;
        zb::ui::ProgressBar *temp_bar_ = nullptr;
        zb::ui::Label *state_label_ = nullptr;
        zb::ui::Button *theme_btn_ = nullptr;
        zb::ui::Slider *demo_slider_ = nullptr;
        zb::ui::ProgressBar *demo_bar_ = nullptr;

        // V-5 step 3: the factory-console page
        zb::ui::GaugeDial *temp_gauge_ = nullptr;
        zb::ui::GaugeDial *press_gauge_ = nullptr;
        zb::ui::GaugeDial *flow_gauge_ = nullptr;
        zb::ui::TrendLine *trend_ = nullptr;
        zb::ui::Knob *setpoint_knob_ = nullptr;
        zb::ui::Slider *setpoint_slider_ = nullptr;
        zb::ui::Label *knob_readout_ = nullptr;
        zb::ui::Label *console_ppm_ = nullptr;
        zb::ui::ToggleSwitch *pump_toggle_ = nullptr;
        zb::ui::ToggleSwitch *coolant_toggle_ = nullptr;
        zb::ui::ListBox *alarm_list_ = nullptr;
        zb::ui::Button *bench_btn_ = nullptr;
        zb::ui::Label *bench_out_ = nullptr;
        zb::ui::Label *bench_in_ = nullptr;
        int sample_ = 0;
        std::vector<std::string> alarm_log_;

        // self-check runtime
        bool bench_active_ = false;
        bool bench_matched_ = true;
        int bench_half_ = 0;
        int bench_paints_ = 0;
        long long bench_px_ = 0;
        uint32_t bench_hash_[2]{0, 0};  // up-state / returned-state hashes
        std::chrono::steady_clock::time_point bench_t0_{};
        std::chrono::steady_clock::duration bench_dur_{};

        zb::event::Subscription<> sub_theme_;
        zb::event::Subscription<> sub_start_;
        zb::event::Subscription<> sub_stop_;
        zb::event::Subscription<> sub_replay_;
        zb::event::Subscription<> sub_gallery_;
        zb::event::Subscription<> sub_dashboard_;
        zb::event::Subscription<> sub_dash_back_;
        zb::event::Subscription<> sub_bench_;
        zb::event::Subscription<int> sub_setpoint_slider_;
        zb::event::Subscription<int> sub_setpoint_knob_;
        zb::event::Subscription<bool> sub_pump_;
        zb::event::Subscription<bool> sub_coolant_;
        zb::event::Subscription<const void *> sub_painted_;
        zb::event::Subscription<> sub_back_;
        zb::event::Subscription<int> sub_slider_;
    };
}  // namespace zb::app::showcase
