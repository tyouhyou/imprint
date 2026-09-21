#pragma once

#include <cstdint>
#include <string>

#include "canvas_window.hpp"
#include "html.hpp"
#include "iapp.hpp"
#include "ui_builder.hpp"

namespace zb::app::showcase_html
{
    /*
     * HTML-design showcase: materializes one HTML document (the embedded
     * space.html snapshot, or SHOWCASE_HTML_FILE at runtime on desktop)
     * through parse_html + build() — the same code path ui_preview uses
     * for external files, so the NDS target (no filesystem) renders the
     * identical tree from embedded bytes.
     *
     * Sizing: the document carries no page box (the chassis is 100%),
     * so the window resolves from the shell request — NDS 256x192, the
     * framebuffer shell 320x240, desktop shells the 480x360 default.
     * A document page box (body inline w/h), when present, wins over the
     * request unless an external buffer was supplied (the ui_preview
     * rule for fixed-size hosts).
     */
    class ShowcaseHtml : public IApp
    {
    public:
        ShowcaseHtml();
        ~ShowcaseHtml() override = default;

        void create_window() override { create_window(kDesktopWidth, kDesktopHeight); }
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
                         uint32_t max_client_height, void *buffer = nullptr);
        bool load_document(std::string &text);

        static constexpr int kDesktopWidth = 480;
        static constexpr int kDesktopHeight = 360;

        zb::SharedPtr<CanvasWindow> window_;
    };
}  // namespace zb::app::showcase_html
