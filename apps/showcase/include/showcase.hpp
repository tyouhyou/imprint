#pragma once

#include <cstdint>
#include <memory>

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"

namespace zb::ui
{
    class Button;
    class FlexPanel;
    class Label;
    class ProgressBar;
    class Slider;
}

namespace zb::app::showcase
{
    /*
     * S3 showcase: two embedded .ui pages behind one window.
     *
     * Page 0 "hero" is a device control panel: three progress bars
     * advanced deterministically by input events while running (START /
     * STOP), a status label updated only on discrete transitions, and a
     * dark/light theme toggle. Page 1 "gallery" shows every widget and
     * wires the demo slider to the demo bar.
     *
     * Both documents are packed at build time by ui_embed and parsed
     * back with the library parser — the same code path shipped embedded
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
        void wire_controls();
        void show_page(int index);
        void set_state(const char *text);
        void start();
        void stop();
        void toggle_theme();
        void advance();

        // default desktop size (4:3, like FB 320x240 and NDS/WASM
        // 256x192 — the pages scale to whatever the shell passes)
        int32_t _width{640};
        int32_t _height{480};

        bool dark_ = false;
        bool running_ = false;

        zb::SharedPtr<CanvasWindow> window_;
        // Panel layout stacks children unconditionally, so exactly one
        // page is mounted under the root at a time; parked pages live in
        // the array, mounted_ points at the one in the tree (owned by it)
        std::unique_ptr<zb::ui::FlexPanel> pages_[2];
        zb::ui::FlexPanel *mounted_ = nullptr;
        int current_ = 0;

        zb::ui::ProgressBar *cpu_bar_ = nullptr;
        zb::ui::ProgressBar *mem_bar_ = nullptr;
        zb::ui::ProgressBar *temp_bar_ = nullptr;
        zb::ui::Label *state_label_ = nullptr;
        zb::ui::Button *theme_btn_ = nullptr;
        zb::ui::Slider *demo_slider_ = nullptr;
        zb::ui::ProgressBar *demo_bar_ = nullptr;

        zb::event::Subscription<> sub_theme_;
        zb::event::Subscription<> sub_start_;
        zb::event::Subscription<> sub_stop_;
        zb::event::Subscription<> sub_gallery_;
        zb::event::Subscription<> sub_back_;
        zb::event::Subscription<int> sub_slider_;
    };
}  // namespace zb::app::showcase
