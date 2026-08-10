#include "game.hpp"

using namespace zb::app::tictactoe;

int Game::get_rows() const
{
    return rows;
}

void Game::set_human(const player_type &human)
{
    this->human = human;
    this->computer = (human == player_type::x) ? player_type::o : player_type::x;
}

player_type Game::get_human() const
{
    return human;
}

player_type Game::get_computer() const
{
    return computer;
}

void Game::start_round(const player_type &first)
{
    for (auto &row : board)
    {
        for (auto &cell : row)
        {
            cell = player_type::none;
        }
    }
    current_player = first;
}

bool Game::place_mark(const int row, const int col)
{
    if (!set_mark(row, col, current_player))
    {
        return false;
    }
    toggle_current();
    return true;
}

bool Game::set_mark(const int row, const int col, const player_type p)
{
    if (p == player_type::none || row < 0 || row >= rows || col < 0 || col >= rows)
    {
        return false;
    }
    if (board[row][col] != player_type::none)
    {
        return false;
    }
    board[row][col] = p;
    return true;
}

player_type Game::get_mark(const int row, const int col) const
{
    return (row >= 0 && row < rows && col >= 0 && col < rows)
               ? board[row][col]
               : player_type::none;
}

player_type Game::check_winner() const
{
    for (int r = 0; r < rows; ++r)
    {
        const auto p = board[r][0];
        if (p != player_type::none && board[r][1] == p && board[r][2] == p)
        {
            return p;
        }
    }
    for (int c = 0; c < rows; ++c)
    {
        const auto p = board[0][c];
        if (p != player_type::none && board[1][c] == p && board[2][c] == p)
        {
            return p;
        }
    }
    const auto d1 = board[0][0];
    if (d1 != player_type::none && board[1][1] == d1 && board[2][2] == d1)
    {
        return d1;
    }
    const auto d2 = board[0][2];
    if (d2 != player_type::none && board[1][1] == d2 && board[2][0] == d2)
    {
        return d2;
    }
    return player_type::none;
}

bool Game::is_full() const
{
    for (const auto &row : board)
    {
        for (const auto &cell : row)
        {
            if (cell == player_type::none)
            {
                return false;
            }
        }
    }
    return true;
}

player_type Game::get_current_player() const
{
    return current_player;
}

void Game::toggle_current()
{
    current_player = (current_player == player_type::x) ? player_type::o : player_type::x;
}
