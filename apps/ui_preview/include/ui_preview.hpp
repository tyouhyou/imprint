#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "canvas_window.hpp"
#include "html.hpp"
#include "iapp.hpp"
#include "ui_builder.hpp"

namespace zb::app::ui_preview
{
    /*
     * document-kind routing (batch H / D5): a ".html"/".htm" path runs
     * the HTML/CSS subset front-end, everything else the .ui
     * design-file front-end. A free function (not a method) so the
     * routing matrix is unit-testable without a window (test_preview).
     */
    bool is_html_path(const std::string &path);

    // App default screen size (B2 backstop): used when neither the
    // document page nor the shell provides dimensions. resolve warns
    // whenever it falls back here, so silent defaults never hide a
    // missing size during bug-chasing.
    constexpr int kDefaultScreenWidth = 800;
    constexpr int kDefaultScreenHeight = 600;

    /*
     * B2 page-size resolution (pure, unit-tested): document page first,
     * then the shell dims, then the app default (with an LW warning per
     * defaulted axis). Per-axis and independent; non-positive inputs at
     * any level count as absent.
     */
    std::pair<int, int> resolve_page_size(const zb::ui::html_page &page,
                                          int shell_w, int shell_h);

    /*
     * Design-file previewer: renders .ui/.html documents (space-separated
     * paths from the UI_PREVIEW_FILES environment variable) inside the
     * generic desktop shell. Right/Left switch documents, each parsed
     * with the library parser and materialized with build() — the same
     * code path a shipped app uses for packed documents.
     */
    class UiPreview : public IApp
    {
    public:
        UiPreview();
        ~UiPreview() override = default;

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
        event::PAINT_EVENT::Subscription on_painting(event::PAINT_EVENT::EventHandler) noexcept override;
        event::PAINT_EVENT::Subscription on_painted(event::PAINT_EVENT::EventHandler) noexcept override;
        event::CLOSE_EVENT::Subscription on_closing(event::CLOSE_EVENT::EventHandler) noexcept override;
        event::CLOSE_EVENT::Subscription on_closed(event::CLOSE_EVENT::EventHandler) noexcept override;

    private:
        void make_window(uint32_t max_client_width,
                         uint32_t max_client_height, void *buffer = nullptr);
        void parse_documents();
        void build_screens(int window_width, int window_height);
        void show_screen(const std::size_t index);

        int32_t _width{kDefaultScreenWidth};
        int32_t _height{kDefaultScreenHeight};

        zb::SharedPtr<CanvasWindow> window_;
        std::vector<std::string> files_;    // UI_PREVIEW_FILES paths
        std::vector<std::string> used_;     // files that parsed (parallel
                                            // to docs_/pages_/screens_)
        std::vector<zb::ui::ui_node> docs_; // parsed documents
        std::vector<zb::ui::html_page> pages_;  // page boxes, parallel
        std::vector<zb::ui::FlexPanel *> screens_;
        std::size_t current_ = 0;
    };
}  // namespace zb::app::ui_preview