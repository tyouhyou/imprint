#include "ui_preview.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#include "logging.hpp"

namespace zb::app::ui_preview
{
    UiPreview::UiPreview()
    {
        const char *files = std::getenv("UI_PREVIEW_FILES");
        if (files == nullptr)
        {
            LW << "ui_preview: UI_PREVIEW_FILES is not set; nothing to preview";
            return;
        }
        std::istringstream ss(files);
        std::string path;
        while (ss >> path)
        {
            files_.push_back(std::move(path));
        }
        if (files_.empty())
        {
            LW << "ui_preview: UI_PREVIEW_FILES is empty";
        }
    }

    void UiPreview::create_window(const uint32_t &max_client_width,
                                  const uint32_t &max_client_height)
    {
        make_window(max_client_width, max_client_height);
    }

    void UiPreview::create_window(const uint32_t &max_client_width,
                                  const uint32_t &max_client_height, void *buffer)
    {
        make_window(max_client_width, max_client_height, buffer);
    }

    void UiPreview::make_window(const uint32_t &max_client_width,
                                const uint32_t &max_client_height, void *buffer)
    {
        _width = static_cast<int32_t>(max_client_width);
        _height = static_cast<int32_t>(max_client_height);
        window_ = zb::make_shared<CanvasWindow>();
        window_->create(max_client_width, max_client_height, buffer);
        // design-file host: layout is driven by the window (batch J5)
        window_->set_auto_layout(true);
        load_documents();
        if (!screens_.empty())
        {
            show_screen(0);
        }
    }

    void UiPreview::load_documents()
    {
        zb::ui::Panel &root = window_->root();
        for (std::size_t i = 0; i < files_.size(); ++i)
        {
            std::ifstream f(files_[i], std::ios::binary);
            if (!f)
            {
                LW << "ui_preview: cannot open '" << files_[i] << "'";
                continue;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            const std::string text = ss.str();

            bool ok = false;
            zb::ui::ui_node doc = zb::ui::parse_ui_text(text.c_str(), &ok);
            if (!ok)
            {
                LW << "ui_preview: '" << files_[i] << "' yields no widget; skipped";
                continue;
            }

            auto screen = std::make_unique<zb::ui::FlexPanel>();
            screen->set_size(_width, _height);
            zb::ui::FlexPanel *raw = screen.get();
            zb::ui::build(*screen, doc);
            root.add_child(std::move(screen));
            screens_.push_back(raw);
            docs_.push_back(std::move(doc));
            LI << "ui_preview: loaded '" << files_[i] << "'";
        }
        if (screens_.empty())
        {
            LI << "ui_preview: no usable documents; empty window";
        }
    }

    void UiPreview::show_screen(const std::size_t index)
    {
        for (std::size_t i = 0; i < screens_.size(); ++i)
        {
            screens_[i]->set_visible(i == index);
        }
        current_ = index;
        window_->root().layout();
        LI << "ui_preview: showing '" << files_[index] << "'";
    }

    zb::SharedPtr<IWindow> UiPreview::window() noexcept
    {
        return window_;
    }

    void UiPreview::input(const zb::input::input_event &ev) noexcept
    {
        if (ev.type == zb::input::input_type::key_down && screens_.size() > 1)
        {
            if (ev.key == static_cast<int>(zb::input::key_code::right))
            {
                show_screen((current_ + 1) % screens_.size());
                return;
            }
            if (ev.key == static_cast<int>(zb::input::key_code::left))
            {
                show_screen((current_ + screens_.size() - 1) % screens_.size());
                return;
            }
        }
        window_->input(ev);
    }

    void UiPreview::paint() noexcept
    {
        window_->paint();
    }

    bool UiPreview::is_dirty() const noexcept
    {
        return window_ && window_->is_dirty();
    }

    bool UiPreview::dirty_region(int &x, int &y, int &rw, int &rh) const noexcept
    {
        return window_ && window_->dirty_region(x, y, rw, rh);
    }

    void UiPreview::on_painting(const event::PAINT_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->painting += handler;
    }

    void UiPreview::on_painted(const event::PAINT_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->painted += handler;
    }

    void UiPreview::on_closing(const event::CLOSE_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->closing += handler;
    }

    void UiPreview::on_closed(const event::CLOSE_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->closed += handler;
    }
}  // namespace zb::app::ui_preview