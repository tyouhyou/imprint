#pragma once

#include <cstdio>

#include "canvas_window.hpp"
#include "iapp.hpp"

namespace zb::app::hello
{
    /*
     * The smallest interactive Imprint UI app: a label and a button that
     * counts its own clicks. Walkthrough in docs/getting-started.md.
     */
    class Hello : public IApp
    {
    public:
        void create_window() override { create_window(320, 240); }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height) override
        {
            create_window(max_client_width, max_client_height, nullptr);
        }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height, void *buffer) override
        {
            window_ = zb::make_shared<CanvasWindow>();
            window_->create(max_client_width, max_client_height, buffer);

            auto &root = window_->root();

            auto title = std::make_unique<zb::ui::Label>();
            title->set_text("Hello, Imprint UI!");
            title->set_position(20, 20);
            root.add_child(std::move(title));

            auto counter = std::make_unique<zb::ui::Label>();
            counter->set_text("Clicks: 0");
            counter->set_position(20, 60);
            zb::ui::Label *counter_ptr = counter.get();
            root.add_child(std::move(counter));

            auto button = std::make_unique<zb::ui::Button>();
            button->set_text("Click me");
            button->set_size(120, 40);
            button->set_position(20, 100);
            button->clicked += [this, counter_ptr] {
                ++clicks_;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "Clicks: %d", clicks_);
                counter_ptr->set_text(buf);
            };
            root.add_child(std::move(button));
        }

        zb::SharedPtr<IWindow> window() noexcept override { return window_; }

        void input(const zb::input::input_event &ev) noexcept override
        {
            window_->input(ev);
        }

        void paint() noexcept override { window_->paint(); }

        bool is_dirty() const noexcept override
        {
            return window_ && window_->is_dirty();
        }

        bool dirty_region(int &x, int &y, int &w, int &h) const noexcept override
        {
            return window_ && window_->dirty_region(x, y, w, h);
        }

        void on_painting(event::PAINT_EVENT::EventHandler h) noexcept override
        {
            if (window_) window_->painting += h;
        }

        void on_painted(event::PAINT_EVENT::EventHandler h) noexcept override
        {
            if (window_) window_->painted += h;
        }

        void on_closing(event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            if (window_) window_->closing += h;
        }

        void on_closed(event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            if (window_) window_->closed += h;
        }

    private:
        zb::SharedPtr<CanvasWindow> window_;
        int clicks_ = 0;
    };
}  // namespace zb::app::hello
