#include "showcase.hpp"

#include "button.hpp"
#include "flex_panel.hpp"
#include "label.hpp"
#include "logging.hpp"
#include "progress_bar.hpp"
#include "slider.hpp"
#include "theme.hpp"
#include "ui_file.hpp"

#include "showcase_ui.gen.hpp"  // packed by ui_embed at build time

namespace zb::app::showcase
{
    void Showcase::create_window(uint32_t max_client_width,
                                 uint32_t max_client_height)
    {
        make_window(max_client_width, max_client_height, nullptr);
    }

    void Showcase::create_window(uint32_t max_client_width,
                                 uint32_t max_client_height, void *buffer)
    {
        make_window(max_client_width, max_client_height, buffer);
    }

    void Showcase::make_window(uint32_t max_client_width,
                               uint32_t max_client_height, void *buffer)
    {
        _width = static_cast<int32_t>(max_client_width);
        _height = static_cast<int32_t>(max_client_height);
        window_ = zb::make_shared<CanvasWindow>();
        window_->create(max_client_width, max_client_height, buffer);
        // design-file host: layout is driven by the window (batch J5)
        window_->set_auto_layout(true);
        load_pages();
        wire_controls();
        show_page(0);
    }

    void Showcase::load_pages()
    {
        static const char *const names[2] = {"hero.ui", "gallery.ui"};
        zb::ui::Panel &root = window_->root();
        for (int i = 0; i < 2; ++i)
        {
            const embedded_ui_file *doc = find_ui_file(names[i]);
            if (doc == nullptr)
            {
                LW << "showcase: embedded document '" << names[i] << "' missing";
                continue;
            }
            bool ok = false;
            zb::ui::ui_node tree = zb::ui::parse_ui_text(
                reinterpret_cast<const char *>(doc->data), &ok);
            if (!ok)
            {
                LW << "showcase: '" << names[i] << "' yields no widget; skipped";
                continue;
            }
            auto page = std::make_unique<zb::ui::FlexPanel>();
            page->set_size(_width, _height);
            zb::ui::FlexPanel *raw = page.get();
            zb::ui::build(*page, tree);
            root.add_child(std::move(page));
            pages_[i] = raw;
        }
    }

    void Showcase::wire_controls()
    {
        zb::ui::Widget &root = window_->root();
        auto *start_btn = static_cast<zb::ui::Button *>(root.find_by_id("start_btn"));
        auto *stop_btn = static_cast<zb::ui::Button *>(root.find_by_id("stop_btn"));
        auto *gallery_btn = static_cast<zb::ui::Button *>(root.find_by_id("gallery_btn"));
        auto *back_btn = static_cast<zb::ui::Button *>(root.find_by_id("back_btn"));
        theme_btn_ = static_cast<zb::ui::Button *>(root.find_by_id("theme_btn"));
        state_label_ = static_cast<zb::ui::Label *>(root.find_by_id("state_value"));
        cpu_bar_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("cpu_bar"));
        mem_bar_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("mem_bar"));
        temp_bar_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("temp_bar"));
        demo_slider_ = static_cast<zb::ui::Slider *>(root.find_by_id("demo_slider"));
        demo_bar_ = static_cast<zb::ui::ProgressBar *>(root.find_by_id("demo_bar"));

        if (start_btn != nullptr)
        {
            sub_start_ = start_btn->clicked.subscribe([this] { start(); });
        }
        if (stop_btn != nullptr)
        {
            sub_stop_ = stop_btn->clicked.subscribe([this] { stop(); });
        }
        if (gallery_btn != nullptr)
        {
            sub_gallery_ = gallery_btn->clicked.subscribe([this] { show_page(1); });
        }
        if (back_btn != nullptr)
        {
            sub_back_ = back_btn->clicked.subscribe([this] { show_page(0); });
        }
        if (theme_btn_ != nullptr)
        {
            sub_theme_ = theme_btn_->clicked.subscribe([this] { toggle_theme(); });
        }
        if (demo_slider_ != nullptr && demo_bar_ != nullptr)
        {
            sub_slider_ = demo_slider_->changed.subscribe(
                [this](const int v) { demo_bar_->set_value(v); });
        }
    }

    void Showcase::show_page(const int index)
    {
        for (int i = 0; i < 2; ++i)
        {
            if (pages_[i] != nullptr)
            {
                pages_[i]->set_visible(i == index);
            }
        }
        current_ = index;
        window_->root().layout();
    }

    void Showcase::set_state(const char *text)
    {
        if (state_label_ != nullptr)
        {
            state_label_->set_text(text);
        }
    }

    void Showcase::start()
    {
        if (cpu_bar_ != nullptr && cpu_bar_->get_value() >= cpu_bar_->get_max())
        {
            // DONE: restart from zero
            cpu_bar_->set_value(0);
            if (mem_bar_ != nullptr) mem_bar_->set_value(0);
            if (temp_bar_ != nullptr) temp_bar_->set_value(0);
        }
        running_ = true;
        set_state("RUNNING");
    }

    void Showcase::stop()
    {
        running_ = false;
        set_state("IDLE");
    }

    void Showcase::toggle_theme()
    {
        dark_ = !dark_;
        zb::ui::set_theme(dark_ ? zb::ui::dark_theme() : zb::ui::light_theme());
        if (theme_btn_ != nullptr)
        {
            theme_btn_->set_text(dark_ ? "LIGHT" : "DARK");
        }
    }

    // one deterministic step per input event while running (repaint on
    // demand: there is no animation loop); the status label changes only
    // on the discrete DONE transition, never in this per-event path
    void Showcase::advance()
    {
        if (cpu_bar_ != nullptr) cpu_bar_->set_value(cpu_bar_->get_value() + 1);
        if (mem_bar_ != nullptr) mem_bar_->set_value(mem_bar_->get_value() + 2);
        if (temp_bar_ != nullptr) temp_bar_->set_value(temp_bar_->get_value() + 3);
        if (cpu_bar_ != nullptr && mem_bar_ != nullptr && temp_bar_ != nullptr &&
            cpu_bar_->get_value() >= cpu_bar_->get_max() &&
            mem_bar_->get_value() >= mem_bar_->get_max() &&
            temp_bar_->get_value() >= temp_bar_->get_max())
        {
            running_ = false;
            set_state("DONE");
        }
    }

    zb::SharedPtr<IWindow> Showcase::window() noexcept
    {
        return window_;
    }

    void Showcase::input(const zb::input::input_event &ev) noexcept
    {
        if (running_)
        {
            advance();
        }
        window_->input(ev);
    }

    void Showcase::paint() noexcept
    {
        window_->paint();
    }

    bool Showcase::is_dirty() const noexcept
    {
        return window_ && window_->is_dirty();
    }

    bool Showcase::dirty_region(int &x, int &y, int &rw, int &rh) const noexcept
    {
        return window_ && window_->dirty_region(x, y, rw, rh);
    }

    void Showcase::on_painting(const event::PAINT_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->painting += handler;
    }

    void Showcase::on_painted(const event::PAINT_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->painted += handler;
    }

    void Showcase::on_closing(const event::CLOSE_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->closing += handler;
    }

    void Showcase::on_closed(const event::CLOSE_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->closed += handler;
    }
}  // namespace zb::app::showcase
