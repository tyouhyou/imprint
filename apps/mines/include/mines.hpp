#ifndef MINES_HPP
#define MINES_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::mines
{
    /*
     * Classic tier (the kWidth x kHeight design minimum) is the 9x9 /
     * 14px-cell field; larger host buffers grow the cell count (the cell
     * itself only grows to kMaxCellPx, capped at kMaxCells). The grid is
     * square, derived from the space left after the header row, so the
     * same formula reproduces the classic tier exactly at 256x192.
     */
    constexpr int kMinCells = 9;
    constexpr int kMaxCells = 32;
    constexpr int kCell = 14;      // classic cell size (design minimum tier)
    constexpr int kMaxCellPx = 22; // cell size cap on large screens
    constexpr int kGap = 1;

    /*
     * The minefield: a cols x rows grid of cells drawn by the board
     * (covered / flagged / revealed states); revealed numbers render
     * through child Labels (framework text path, centered in the cell).
     * The app owns the game state.
     */
    class MinesBoard : public zb::ui::Panel
    {
    public:
        MinesBoard(int cols, int rows, int cell, int gap);

        // cell = 0..8 number (0 = blank), -1 covered, -2 flag, -3 mine,
        // -4 wrong flag, -5 boom
        void set_cell(int row, int col, int cell);

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;

    private:
        int cols_ = 0;
        int rows_ = 0;
        int cell_ = 0;
        int gap_ = 0;
        zb::ui::Label *tiles_[kMaxCells][kMaxCells] = {};
        int state_[kMaxCells][kMaxCells] = {}; // the last value handed to set_cell
    };

    /*
     * Minesweeper demo app. Left click / tap reveals, FLAG toggles the
     * flag mode (the wasm glue sends no right-click, so the mode button
     * is the touch path too). Mine placement comes from a fixed-seed LCG
     * re-rolled on the first reveal (excluding that cell), so a replay of
     * the same input stream plays the same field. The field geometry is
     * decided once in create_window from the host buffer size.
     */
    class Mines : public IApp
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
        static constexpr int kHeaderH = 30; // status/button row height

        enum state : int
        {
            playing = 0,
            won,
            lost
        };

        // field geometry, decided once in create_window
        int cols_ = kMinCells;
        int rows_ = kMinCells;
        int cell_ = kCell;
        int gap_ = kGap;
        int board_x_ = 0;
        int board_y_ = kHeaderH;
        int mines_ = 10;

        // board state; covered_[i] etc. indexed r * cols_ + c
        bool mine_[kMaxCells * kMaxCells] = {};
        bool flagged_[kMaxCells * kMaxCells] = {};
        bool covered_[kMaxCells * kMaxCells] = {};
        int adjacent_[kMaxCells * kMaxCells] = {};
        state state_ = playing;
        bool placed_ = false;   // mines placed on first reveal
        int flags_ = 0;
        int moves_ = 0;
        int boom_ = -1;
        unsigned seed_ = 20260101u;

        int down_x_ = -1;
        int down_y_ = -1;

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        MinesBoard *board_ = nullptr;
        zb::ui::Label *status_ = nullptr;
        zb::ui::Button *flag_btn_ = nullptr;
        zb::ui::Dialog *end_dialog_ = nullptr;
        bool flag_mode_ = false;

        event::Subscription<> sub_flag_;
        event::Subscription<> sub_again_;
        event::Subscription<> sub_quit_;

        void new_game();
        void place_mines(int safe_cell);
        [[nodiscard]] int lcg_rand();
        void reveal(int cell);
        void flood(int cell);
        void toggle_flag(int cell);
        void refresh_board();
        void refresh_status();
        void check_win();
        void show_end_dialog();
        void close_end_dialog();
        // screen -> cell, -1 outside the field
        [[nodiscard]] int cell_at(int x, int y) const;
    };
}  // namespace zb::app::mines

#endif // !MINES_HPP
