#include "board.hpp"

using namespace zb::app::tictactoe;
using namespace zb::ui;

void Board::draw_at(core::Graphics &area) const
{
    const auto s = get_size();
    const int cell = (s.width < s.height ? s.width : s.height) / 3;
    if (cell <= 0)
    {
        return;
    }

    // grid lines
    for (int i = 1; i < 3; ++i)
    {
        area.draw_line(0, cell * i, s.width, cell * i, grid_color);
        area.draw_line(cell * i, 0, cell * i, s.height, grid_color);
    }

    // marks
    for (int r = 0; r < 3; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            if (marks[r][c] != player_type::none)
            {
                draw_mark(area, r, c, marks[r][c]);
            }
        }
    }
}

void Board::draw_mark(core::Graphics &area, const int row, const int col, const player_type p) const
{
    const auto s = get_size();
    const int cell = (s.width < s.height ? s.width : s.height) / 3;
    const int x0 = col * cell;
    const int y0 = row * cell;

    if (player_type::x == p)
    {
        area.draw_line(x0 + mark_padding, y0 + mark_padding,
                       x0 + cell - mark_padding, y0 + cell - mark_padding, mark_color);
        area.draw_line(x0 + cell - mark_padding, y0 + mark_padding,
                       x0 + mark_padding, y0 + cell - mark_padding, mark_color);
    }
    else if (player_type::o == p)
    {
        area.draw_circle(x0 + cell / 2, y0 + cell / 2, cell / 2 - mark_padding, mark_color);
    }
}

bool Board::on_input(const input::input_event &ev)
{
    if (ev.type != input::input_type::mouse_left_down &&
        ev.type != input::input_type::touch_down)
    {
        return false;
    }

    const auto abs = get_absolute_position();
    const int lx = ev.x - abs.x;
    const int ly = ev.y - abs.y;
    const auto s = get_size();
    if (lx < 0 || ly < 0 || lx >= s.width || ly >= s.height)
    {
        return false;
    }

    const int cell = (s.width < s.height ? s.width : s.height) / 3;
    if (cell <= 0)
    {
        return false;
    }
    const int col = lx / cell;
    const int row = ly / cell;
    if (col >= 3 || row >= 3)
    {
        return false;  // trailing pixels of a non-divisible board: no cell
    }

    cell_clicked(row, col);
    return true;
}
