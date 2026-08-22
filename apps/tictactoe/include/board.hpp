#pragma once

#include "event.hpp"
#include "imui.hpp"
#include "tictactoe_defines.hpp"

namespace zb::app::tictactoe
{
    /*
     * Tic-tac-toe grid widget. Draws a 3x3 grid and the marks; a click on
     * a cell emits cell_clicked(row, col).
     */
    class Board : public zb::ui::Widget
    {
    public:
        Board() = default;

        void set_mark(const int row, const int col, const player_type p)
        {
            if (row >= 0 && row < 3 && col >= 0 && col < 3)
            {
                marks[row][col] = p;
                // a state setter reports its damage (the Widget contract);
                // the full-frame fallback masks a missing report today,
                // any partial-damage change would make it visible
                mark_dirty();
            }
        }

        [[nodiscard]] player_type get_mark(const int row, const int col) const
        {
            return (row >= 0 && row < 3 && col >= 0 && col < 3) ? marks[row][col] : player_type::none;
        }

        void clear_marks()
        {
            for (auto &row : marks)
            {
                for (auto &m : row)
                {
                    m = player_type::none;
                }
            }
            mark_dirty();
        }

        void set_grid_color(const zb::ui::core::Color &c) { grid_color = c; }
        void set_mark_color(const zb::ui::core::Color &c) { mark_color = c; }
        void set_mark_padding(const int p) { mark_padding = p; }

        // emitted when a cell is clicked: row, col
        zb::event::Event<int, int> cell_clicked;

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;

    private:
        void draw_mark(zb::ui::core::Graphics &area, const int row, const int col, const player_type p) const;

        player_type marks[3][3]{};
        zb::ui::core::Color grid_color = zb::ui::core::colors::Black;
        zb::ui::core::Color mark_color = zb::ui::core::colors::Black;
        int mark_padding = 10;
    };
}
