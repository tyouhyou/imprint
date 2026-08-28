#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "canvas_window.hpp"
#include "iapp.hpp"
#include "ui_builder.hpp"

namespace zb::app::ui_preview
{
    /*
     * Design-file previewer: renders .ui documents (space-separated
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
        void on_painting(event::PAINT_EVENT::EventHandler) noexcept override;
        void on_painted(event::PAINT_EVENT::EventHandler) noexcept override;
        void on_closing(event::CLOSE_EVENT::EventHandler) noexcept override;
        void on_closed(event::CLOSE_EVENT::EventHandler) noexcept override;

    private:
        void make_window(uint32_t max_client_width,
                         uint32_t max_client_height, void *buffer = nullptr);
        void load_documents();
        void show_screen(const std::size_t index);

        int32_t _width{800};
        int32_t _height{600};

        zb::SharedPtr<CanvasWindow> window_;
        std::vector<std::string> files_;    // UI_PREVIEW_FILES paths
        std::vector<zb::ui::ui_node> docs_; // parsed documents
        std::vector<zb::ui::FlexPanel *> screens_;
        std::size_t current_ = 0;
    };
}  // namespace zb::app::ui_preview