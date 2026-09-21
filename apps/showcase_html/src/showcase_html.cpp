#include "showcase_html.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "html.hpp"
#include "logging.hpp"

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "text/runtime_ttf_provider.hpp"
#endif

#include "showcase_html.gen.hpp"  // packed by html_embed at build time

namespace zb::app::showcase_html
{
    namespace
    {
        constexpr char kEmbeddedFile[] = "space.html";
        constexpr char kFileEnv[] = "SHOWCASE_HTML_FILE";

#if defined(IMCORE_HAS_TTF_RUNTIME)
        // Font-family install point (the ui_preview contract): per-widget
        // font_size declarations resolve against this family. Without it
        // the builder keeps the 5x7 bitmap and font-size stays ignored
        // (documented degradation). SHOWCASE_HTML_FONT overrides the
        // build default; a load failure degrades to the bitmap with a log.
        void install_font_family()
        {
            if (zb::ui::has_font_family())
            {
                return;
            }
            const char *path = std::getenv("SHOWCASE_HTML_FONT");
#ifdef IM_HTML_TTF_FONT
            if (path == nullptr)
            {
                path = IM_HTML_TTF_FONT;
            }
#endif
            if (path == nullptr || *path == '\0')
            {
                LW << "showcase_html: no display font (SHOWCASE_HTML_FONT "
                      "unset, no build default); font_size stays ignored";
                return;
            }
            try
            {
                // function-static anchor: the family state holds a copy,
                // the handle stays alive for static-lifetime widgets
                static zb::ui::TtfFamily family =
                    zb::ui::TtfFamily::from_file(path);
                zb::ui::set_font_family(family);
                LD << "showcase_html: font family installed from '" << path
                   << "'";
            }
            catch (const zb::ui::error &e)
            {
                LW << "showcase_html: cannot load font '" << path
                   << "': " << e.what();
            }
        }
#endif
    }  // namespace

    ShowcaseHtml::ShowcaseHtml() = default;

    void ShowcaseHtml::create_window(uint32_t max_client_width,
                                     uint32_t max_client_height)
    {
        make_window(max_client_width, max_client_height, nullptr);
    }

    void ShowcaseHtml::create_window(uint32_t max_client_width,
                                     uint32_t max_client_height, void *buffer)
    {
        make_window(max_client_width, max_client_height, buffer);
    }

    bool ShowcaseHtml::load_document(std::string &text)
    {
        // desktop iteration path: an explicit file wins when it reads
        // (NDS has no environment/filesystem, so it always embeds)
        if (const char *path = std::getenv(kFileEnv);
            path != nullptr && *path != '\0')
        {
            std::ifstream f(path, std::ios::binary);
            if (f)
            {
                std::ostringstream ss;
                ss << f.rdbuf();
                text = ss.str();
                LD << "showcase_html: loaded '" << path << "' from disk";
                return true;
            }
            LW << "showcase_html: cannot open '" << path
               << "'; falling back to the embedded document";
        }
        const embedded_html_file *doc = find_html_file(kEmbeddedFile);
        if (doc == nullptr)
        {
            LW << "showcase_html: embedded document '" << kEmbeddedFile
               << "' missing";
            return false;
        }
        text.assign(reinterpret_cast<const char *>(doc->data), doc->size);
        return true;
    }

    void ShowcaseHtml::make_window(uint32_t max_client_width,
                                   uint32_t max_client_height, void *buffer)
    {
#if defined(IMCORE_HAS_TTF_RUNTIME)
        install_font_family();
#endif
        std::string text;
        zb::ui::ui_node doc;
        zb::ui::html_page page;
        bool ok = false;
        if (load_document(text))
        {
            doc = zb::ui::parse_html(text.c_str(), &ok, &page);
        }
        if (!ok)
        {
            LW << "showcase_html: document yields no widget; empty window";
        }

        // the page box wins over the shell request (a sized body grounds
        // the screen); a fixed host buffer wins over everything, and an
        // unsized document keeps the shell dims (the chassis is 100%)
        int width = static_cast<int>(max_client_width);
        int height = static_cast<int>(max_client_height);
        if (buffer == nullptr)
        {
            if (page.has_width && page.width > 0)
            {
                width = page.width;
            }
            if (page.has_height && page.height > 0)
            {
                height = page.height;
            }
        }
        if (width <= 0 || height <= 0)
        {
            LW << "showcase_html: no usable size; using the desktop default";
            width = kDesktopWidth;
            height = kDesktopHeight;
        }

        window_ = zb::make_shared<CanvasWindow>();
        window_->create(static_cast<uint32_t>(width),
                        static_cast<uint32_t>(height), buffer);
        window_->set_title("Imprint UI -- space");
        // design-file host: layout is driven by the window (batch J5)
        window_->set_auto_layout(true);
        if (ok)
        {
            // the document materializes into a sized FlexPanel screen
            // (the ui_preview pattern): building straight into the plain
            // root Panel leaves percent children without a flex parent
            auto screen = std::make_unique<zb::ui::FlexPanel>();
            screen->set_size(width, height);
            if (page.has_background)
            {
                screen->set_background_color(page.background);
            }
            zb::ui::build(*screen, doc);
            window_->root().add_child(std::move(screen));
            window_->root().layout();
        }
        LD << "showcase_html: window " << width << "x" << height;
    }

    zb::SharedPtr<IWindow> ShowcaseHtml::window() noexcept
    {
        return window_;
    }

    void ShowcaseHtml::input(const zb::input::input_event &ev) noexcept
    {
        if (window_)
        {
            window_->input(ev);
        }
    }

    void ShowcaseHtml::paint() noexcept
    {
        if (window_)
        {
            window_->paint();
        }
    }

    bool ShowcaseHtml::is_dirty() const noexcept
    {
        return window_ && window_->is_dirty();
    }

    bool ShowcaseHtml::dirty_region(int &x, int &y, int &rw, int &rh) const noexcept
    {
        return window_ && window_->dirty_region(x, y, rw, rh);
    }

    void ShowcaseHtml::on_painting(const event::PAINT_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->painting += handler;
    }

    void ShowcaseHtml::on_painted(const event::PAINT_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->painted += handler;
    }

    void ShowcaseHtml::on_closing(const event::CLOSE_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->closing += handler;
    }

    void ShowcaseHtml::on_closed(const event::CLOSE_EVENT::EventHandler handler) noexcept
    {
        if (window_) window_->closed += handler;
    }
}  // namespace zb::app::showcase_html
