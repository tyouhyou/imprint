#include "ui_preview.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#include "html.hpp"
#include "logging.hpp"

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "text/runtime_ttf_provider.hpp"
#endif

namespace zb::app::ui_preview
{
    bool is_html_path(const std::string &path)
    {
        if (path.size() >= 5 &&
            path.compare(path.size() - 5, 5, ".html") == 0)
        {
            return true;
        }
        return path.size() >= 4 &&
               path.compare(path.size() - 4, 4, ".htm") == 0;
    }

    std::pair<int, int> resolve_page_size(const zb::ui::html_page &page,
                                          int shell_w, int shell_h)
    {
        int w = (page.has_width && page.width > 0) ? page.width : shell_w;
        int h = (page.has_height && page.height > 0) ? page.height : shell_h;
        if (w <= 0)
        {
            LW << "ui_preview: no page/shell width; using default "
               << kDefaultScreenWidth;
            w = kDefaultScreenWidth;
        }
        if (h <= 0)
        {
            LW << "ui_preview: no page/shell height; using default "
               << kDefaultScreenHeight;
            h = kDefaultScreenHeight;
        }
        return {w, h};
    }

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

    void UiPreview::create_window(uint32_t max_client_width,
                                  uint32_t max_client_height)
    {
        make_window(max_client_width, max_client_height);
    }

    void UiPreview::create_window(uint32_t max_client_width,
                                  uint32_t max_client_height, void *buffer)
    {
        make_window(max_client_width, max_client_height, buffer);
    }

#if defined(IMCORE_HAS_TTF_RUNTIME)
    // Font-family install point (contract 2.4 plan 2): the preview host
    // resolves per-widget font_size declarations against this family.
    // Without it the builder keeps the 5x7 bitmap and font-size stays
    // ignored (documented degradation). UI_PREVIEW_FONT overrides the
    // build default; a load failure degrades to the bitmap with a log.
    void install_font_family()
    {
        if (zb::ui::has_font_family())
        {
            return;
        }
        const char *path = std::getenv("UI_PREVIEW_FONT");
#ifdef IM_PREVIEW_TTF_FONT
        if (path == nullptr)
        {
            path = IM_PREVIEW_TTF_FONT;
        }
#endif
        if (path == nullptr || *path == '\0')
        {
            LW << "ui_preview: no preview font (UI_PREVIEW_FONT unset, "
                  "no build default); font_size stays ignored";
            return;
        }
        try
        {
            // function-static anchor: the family state holds a copy,
            // the handle stays alive for static-lifetime widgets
            static zb::ui::TtfFamily family =
                zb::ui::TtfFamily::from_file(path);
            zb::ui::set_font_family(family);
            LD << "ui_preview: font family installed from '" << path << "'";
        }
        catch (const zb::ui::error &e)
        {
            LW << "ui_preview: cannot load font '" << path
               << "': " << e.what();
        }
    }
#endif

    void UiPreview::make_window(uint32_t max_client_width,
                                 uint32_t max_client_height, void *buffer)
    {
        _width = static_cast<int32_t>(max_client_width);
        _height = static_cast<int32_t>(max_client_height);
#if defined(IMCORE_HAS_TTF_RUNTIME)
        install_font_family();
#endif
        // B2: parse before creating, so the first usable document's page
        // can size the window. An externally supplied buffer is a
        // fixed-size host constraint (the NDS shape) and wins over the
        // page; otherwise the page wins over the shell dims.
        parse_documents();
        zb::ui::html_page first;
        if (!pages_.empty() && buffer == nullptr)
        {
            first = pages_.front();
        }
        const auto window_size = resolve_page_size(first, _width, _height);
        window_ = zb::make_shared<CanvasWindow>();
        window_->create(static_cast<uint32_t>(window_size.first),
                        static_cast<uint32_t>(window_size.second), buffer);
        // design-file host: layout is driven by the window (batch J5)
        window_->set_auto_layout(true);
        build_screens(window_size.first, window_size.second);
        if (!screens_.empty())
        {
            show_screen(0);
        }
    }

    void UiPreview::parse_documents()
    {
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
            zb::ui::ui_node doc;
            zb::ui::html_page pg;
            if (is_html_path(files_[i]))
            {
                doc = zb::ui::parse_html(text.c_str(), &ok, &pg);
            }
            else
            {
                doc = zb::ui::parse_ui_text(text.c_str(), &ok);
            }
            if (!ok)
            {
                LW << "ui_preview: '" << files_[i] << "' yields no widget; skipped";
                continue;
            }

            docs_.push_back(std::move(doc));
            pages_.push_back(pg);
            used_.push_back(files_[i]);
            LD << "ui_preview: loaded '" << files_[i] << "'";
        }
    }

    void UiPreview::build_screens(int window_width, int window_height)
    {
        zb::ui::Panel &root = window_->root();
        for (std::size_t i = 0; i < docs_.size(); ++i)
        {
            // B2: each screen resolves its own page against the window
            // box; a page background paints the screen when set
            const auto screen_size =
                resolve_page_size(pages_[i], window_width, window_height);
            auto screen = std::make_unique<zb::ui::FlexPanel>();
            screen->set_size(screen_size.first, screen_size.second);
            if (pages_[i].has_background)
            {
                screen->set_background_color(pages_[i].background);
            }
            zb::ui::FlexPanel *raw = screen.get();
            zb::ui::build(*screen, docs_[i]);
            root.add_child(std::move(screen));
            screens_.push_back(raw);
        }
        if (screens_.empty())
        {
            LD << "ui_preview: no usable documents; empty window";
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
        LD << "ui_preview: showing '" << used_[index] << "'";
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