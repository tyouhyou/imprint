#pragma once

/*
 * The studio session-screen client (P1-10, SIXEL-first): the five-pane
 * layout mirroring the Textual prototype SessionApp (tianji repo
 * studio/ui.py, docs/09 §1.2) — history (top-left) / dialogue (under
 * it) / manuscript text window (right 60%) / output (full width) /
 * status line / input line.
 *
 * Zero logic on this side: every command goes to the Python prototype
 * through the studio-bridge child (docs/09 §9.1 wire — spawn +
 * JSON lines over pipes). The panes are projections of project.get
 * snapshots (fp-fingerprinted, polled ≈1s by the deterministic step
 * clock, the same cadence as the TUI's 1s poll); the presentation
 * rules (seat labels, prefix colors as text markers, the draft_ref
 * pointer line) mirror the TUI's projection code.
 *
 * Bindings (FI-11: the SIXEL shell delivers no Shift/Ctrl+arrow
 * chords yet, so the TUI's table is adapted — see studio/README.md):
 *   Ctrl+Q quit · dialogue Ctrl+E/Y · output Ctrl+W/S · wheel anywhere.
 * During a turn the input line is read-only and the status line says
 * 〔AI 回合中〕 — the bridge is the authority (busy comes in every
 * snapshot; turn.finished unlocks).
 */

#include "app_maker.hpp"

#include <memory>
#include <string>
#include <vector>

#include "canvas_window.hpp"

#include "bridge/bridge_client.hpp"

namespace zb::ui
{
    class Label;
    class ListBox;
    class ScrollPanel;
    class TextArea;
    class TextInput;
}

namespace zb::app::studio
{
    class StudioApp : public IApp
    {
    public:
        void create_window() override { create_window(1200, 800); }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height) override
        {
            create_window(max_client_width, max_client_height, nullptr);
        }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height, void *buffer) override;

        zb::SharedPtr<IWindow> window() noexcept override { return window_; }

        void input(const zb::input::input_event &ev) noexcept override;
        void paint() noexcept override { window_->paint(); }

        bool is_dirty() const noexcept override
        {
            return window_ && window_->is_dirty();
        }

        bool dirty_region(int &x, int &y, int &w, int &h) const noexcept override
        {
            return window_ && window_->dirty_region(x, y, w, h);
        }

        event::Subscription<const void *> on_painting(event::PAINT_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->painting.subscribe(h)
                           : event::Subscription<const void *>();
        }

        event::Subscription<const void *> on_painted(event::PAINT_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->painted.subscribe(h)
                           : event::Subscription<const void *>();
        }

        [[nodiscard]] event::Subscription<> on_closing(event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->closing.subscribe(h)
                           : event::Subscription<>();
        }

        [[nodiscard]] event::Subscription<> on_closed(event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->closed.subscribe(h)
                           : event::Subscription<>();
        }

        // R6: one wire poll per step; the fp-poll every
        // kProjectPollInterval steps (~1s at the 30ms tick)
        void step(std::uint32_t step_micros, std::uint64_t index) noexcept override;

    private:
        // the row sources the ListBox providers read (rows are
        // pre-wrapped display lines; the height provider re-estimates)
        struct RowContext
        {
            const std::vector<std::string> *rows = nullptr;
            int inner_width = 0;
        };

        static std::string dial_text(const void *arg, const size_t index);
        static int dial_height(const void *arg, const size_t index);
        static std::string out_text(const void *arg, const size_t index);
        static int out_height(const void *arg, const size_t index);
        static std::string hist_text(const void *arg, const size_t index);

        void build_panes(uint32_t w, uint32_t h);
        void apply_font(zb::ui::Widget &w);  // FI-04 配方第二步
        void load_font();
        void start_bridge();
        void request_project();
        bool send_hello();
        void submit_command();
        void apply_snapshot(const zb::tianji::Json &data);
        void handle_event(const zb::tianji::Json &event);
        void write_output(const std::string &line);
        void refresh_lists();
        void set_busy(const bool busy);
        void scroll_pane(zb::ui::ScrollPanel *pane, const int delta);
        void quit();

        zb::SharedPtr<CanvasWindow> window_;

        // the five panes + chrome
        zb::ui::ListBox *history_ = nullptr;
        zb::ui::ScrollPanel *dial_scroll_ = nullptr;
        zb::ui::ListBox *dialogue_ = nullptr;
        zb::ui::TextArea *manuscript_ = nullptr;
        zb::ui::ScrollPanel *out_scroll_ = nullptr;
        zb::ui::ListBox *output_ = nullptr;
        zb::ui::Label *status_ = nullptr;
        zb::ui::Label *subtitle_ = nullptr;
        zb::ui::TextInput *command_ = nullptr;

        // the list models (the providers read these)
        std::vector<std::string> hist_rows_;
        std::vector<std::string> hist_ids_;   // parallel: row -> session id
        std::vector<std::string> dial_rows_;
        std::vector<std::string> out_rows_;
        RowContext hist_ctx_{};
        RowContext dial_ctx_{};
        RowContext out_ctx_{};

        // the bridge (docs/09 §9.1)
        std::unique_ptr<zb::studio::BridgeClient> bridge_;
        std::string bridge_cwd_;
        std::string bridge_command_;
        std::string seat_ai_ = "ai.writer";
        bool bridge_ready_ = false;
        bool bridge_dead_ = false;  // reported once
        bool busy_ = false;
        std::string fp_;  // last applied projection fingerprint

        std::string command_text_;   // the submitted-event mirror
        std::uint64_t last_poll_ = 0;
        bool shot_done_ = false;     // TIANJI_STUDIO_PNG 存证（一次性）
    };
}  // namespace zb::app::studio
