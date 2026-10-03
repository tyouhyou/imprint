#ifndef G2048_HPP
#define G2048_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::g2048
{
    /*
     * The 2048 board: a 4x4 grid of colored cells with the tile value
     * rendered by a child Label (framework text path, centered in the
     * cell). The app owns the game state; the board only mirrors it.
     */
    class Board2048 : public zb::ui::Panel
    {
    public:
        Board2048();

        // mirrors one tile: value 0 hides the label; v > 2048 uses the
        // "beyond" palette entry
        void set_cell(int row, int col, int v);

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;

    private:
        static constexpr int kGap = 4;
        static constexpr int kCell = 30;

        zb::ui::Label *tiles_[4][4] = {};
    };

    /*
     * 2048 demo app. Arrow keys / WASD and touch swipes move the tiles;
     * NEW starts over. Spawns come from a fixed-seed LCG so a replay of
     * the same input stream plays the same game (the time-travel demo on
     * the gh-pages page relies on this).
     */
    class G2048 : public IApp
    {
    public:
        void create_window() override { create_window(kWidth, kHeight); }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height) override
        {
            create_window(max_client_width, max_client_height, nullptr);
        }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height, void *buffer) override;

        zb::SharedPtr<IWindow> window() noexcept override { return window_; }

        void input(const zb::input::input_event &ev) noexcept override;
        void paint() noexcept override { if (window_) window_->paint(); }

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
            return window_ ? window_->painting.subscribe(h) : event::Subscription<const void *>();
        }

        event::Subscription<const void *> on_painted(event::PAINT_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->painted.subscribe(h) : event::Subscription<const void *>();
        }

        event::Subscription<> on_closing(event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->closing.subscribe(h) : event::Subscription<>();
        }

        event::Subscription<> on_closed(event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->closed.subscribe(h) : event::Subscription<>();
        }

    private:
        static constexpr uint32_t kWidth = 256;
        static constexpr uint32_t kHeight = 192;

        // pure game state (a replay re-derives all of it from the inputs)
        int grid_[4][4] = {};
        int score_ = 0;
        int best_ = 0;
        unsigned seed_ = 20260101u;
        bool won_ = false;      // a 2048 tile exists (win dialog shows once)
        bool win_shown_ = false;
        bool over_ = false;

        // last touch-down point for the swipe gesture
        int down_x_ = -1;
        int down_y_ = -1;

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        Board2048 *board_ = nullptr;
        zb::ui::Label *score_label_ = nullptr;
        zb::ui::Label *best_label_ = nullptr;
        zb::ui::Dialog *end_dialog_ = nullptr;

        // held for the window's lifetime (discarding a registration
        // return unsubscribes the moment the temporary dies)
        event::Subscription<> sub_again_;
        event::Subscription<> sub_cont_;
        event::Subscription<> sub_quit_;

        void new_game();
        void spawn_tile();
        // returns true when anything moved
        bool move(int dr, int dc);
        [[nodiscard]] bool can_move() const;
        void refresh();
        void show_end_dialog(bool won);
        void close_end_dialog();

        [[nodiscard]] int lcg_rand();
    };
}  // namespace zb::app::g2048

#endif // !G2048_HPP
