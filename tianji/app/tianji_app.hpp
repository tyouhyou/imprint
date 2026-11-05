#pragma once

/*
 * The tianji client app (TJ-C3, SIXEL-first): the left message stream
 * (ScrollPanel + variable-height ListBox, follow-bottom), the right
 * 800x600 logical stage (AnnotateCanvas: background + annotation
 * strokes), and the bottom command line whose input resolves against
 * the server-enumerated legal-action proposals (the client never
 * invents actions).
 *
 * Feed wiring (the TJ-C2 seam):
 *   - TIANJI_SOCKET set -> the FeedClient polls the RoomServer
 *     (hello/join_room/subscribe(after_seq) once per shell step; the
 *     cursor rides TianjiView.last_seq so the history never doubles).
 *     The poll is synchronous by the v1 wire shape (one line per
 *     request) — a real-session non-blocking design waits on the
 *     unsolicited-push confirmation in tianji/for_tianji.md §4.
 *   - otherwise -> the offline replay: a mock transcript (or the file
 *     named by TIANJI_REPLAY, shaped like tianji's
 *     samples/run_子夜客栈.json) is synthesized by the TJ-C2 feeder
 *     and applied one message per 30 steps — a capture is a pure
 *     function of (feed source, step count), no wall clock (R6).
 *
 * Stream polish that stays open: per-category styled runs inside a
 * row (the batch-FI spec's colors ride prefixes until ListBox rows
 * carry runs).
 */

#include "app_maker.hpp"

#include <memory>
#include <string>
#include <vector>

#include "canvas_window.hpp"

#include "feed/feed_client.hpp"
#include "feed/view_builder.hpp"

namespace zb::ui
{
    class AnnotateCanvas;
    class Label;
    class ListBox;
    class ScrollPanel;
    class TextInput;
}

namespace zb::app::tianji
{
    class TianjiApp : public IApp
    {
    public:
        void create_window() override { create_window(1200, 680); }

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

        // R6: the deterministic step — one replay message per
        // kReplayStepInterval steps (offline), one wire poll per step
        // (socket mode)
        void step(std::uint32_t step_micros, std::uint64_t index) noexcept override;

    private:
        // the stream row sources the ListBox providers read
        struct StreamContext
        {
            const std::vector<zb::tianji::StreamRow> *rows = nullptr;
        };

        static std::string stream_text(const void *arg, const size_t index);
        static int stream_height(const void *arg, const size_t index);

        void refresh_stream();
        void refresh_proposals();
        void append_row(const zb::tianji::StreamRow &row);
        void submit_command();
        void poll_feed();

        zb::SharedPtr<CanvasWindow> window_;
        zb::ui::ListBox *stream_ = nullptr;
        zb::ui::ScrollPanel *stream_scroll_ = nullptr;
        zb::ui::Label *proposals_label_ = nullptr;
        zb::ui::TextInput *command_ = nullptr;
        zb::ui::AnnotateCanvas *stage_ = nullptr;

        StreamContext stream_ctx_{};
        zb::tianji::TianjiView view_{};
        std::vector<zb::tianji::Json> replay_;  // offline: the message queue
        std::size_t replay_at_ = 0;
        std::string command_text_;              // the submitted-event mirror
        std::string socket_path_;               // empty = offline replay
        std::unique_ptr<zb::tianji::FeedClient> feed_;
        bool socket_ready_ = false;
    };
}  // namespace zb::app::tianji
