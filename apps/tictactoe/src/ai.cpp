#include "ai.hpp"

#include <cstdlib>

namespace zb::app::tictactoe
{
    namespace
    {
        constexpr int size = 3;

        using board_t = player_type[size][size];

        void load_board(const Game &game, board_t &b)
        {
            for (int r = 0; r < size; ++r)
            {
                for (int c = 0; c < size; ++c)
                {
                    b[r][c] = game.get_mark(r, c);
                }
            }
        }

        player_type winner_of(const board_t &b)
        {
            for (int r = 0; r < size; ++r)
            {
                if (b[r][0] != player_type::none && b[r][1] == b[r][0] && b[r][2] == b[r][0])
                {
                    return b[r][0];
                }
            }
            for (int c = 0; c < size; ++c)
            {
                if (b[0][c] != player_type::none && b[1][c] == b[0][c] && b[2][c] == b[0][c])
                {
                    return b[0][c];
                }
            }
            if (b[0][0] != player_type::none && b[1][1] == b[0][0] && b[2][2] == b[0][0])
            {
                return b[0][0];
            }
            if (b[0][2] != player_type::none && b[1][1] == b[0][2] && b[2][0] == b[0][2])
            {
                return b[0][2];
            }
            return player_type::none;
        }

        bool full_of(const board_t &b)
        {
            for (int r = 0; r < size; ++r)
            {
                for (int c = 0; c < size; ++c)
                {
                    if (b[r][c] == player_type::none)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        /*
         * +1 if `for_player` wins with optimal play from `to_move`'s turn,
         * -1 if the other side forces a win, 0 for a draw.
         */
        int minimax(board_t &b, const player_type for_player, const player_type to_move)
        {
            const auto w = winner_of(b);
            if (w == for_player)
            {
                return 1;
            }
            if (w != player_type::none)
            {
                return -1;
            }
            if (full_of(b))
            {
                return 0;
            }

            const auto other = (to_move == player_type::x) ? player_type::o : player_type::x;
            int best = (to_move == for_player) ? -1 : 1;
            for (int r = 0; r < size; ++r)
            {
                for (int c = 0; c < size; ++c)
                {
                    if (b[r][c] == player_type::none)
                    {
                        b[r][c] = to_move;
                        const int s = minimax(b, for_player, other);
                        b[r][c] = player_type::none;
                        if (to_move == for_player)
                        {
                            if (s > best)
                            {
                                best = s;
                                if (best == 1)
                                {
                                    return best;  // a win is already optimal
                                }
                            }
                        }
                        else if (s < best)
                        {
                            best = s;
                            if (best == -1)
                            {
                                return best;  // the opponent found a forced win
                            }
                        }
                    }
                }
            }
            return best;
        }

        // all eight winning lines
        const int lines[8][3][2] = {
            {{0, 0}, {0, 1}, {0, 2}},
            {{1, 0}, {1, 1}, {1, 2}},
            {{2, 0}, {2, 1}, {2, 2}},
            {{0, 0}, {1, 0}, {2, 0}},
            {{0, 1}, {1, 1}, {2, 1}},
            {{0, 2}, {1, 2}, {2, 2}},
            {{0, 0}, {1, 1}, {2, 2}},
            {{0, 2}, {1, 1}, {2, 0}},
        };

        // true if playing `p` at (row, col) completes a winning line
        bool completes_line(const Game &game, const int row, const int col, const player_type p)
        {
            for (const auto &line : lines)
            {
                int p_count = 0;
                int empty_count = 0;
                int empty_row = -1;
                int empty_col = -1;
                for (const auto &cell : line)
                {
                    const auto m = game.get_mark(cell[0], cell[1]);
                    if (m == p)
                    {
                        ++p_count;
                    }
                    else if (m == player_type::none)
                    {
                        ++empty_count;
                        empty_row = cell[0];
                        empty_col = cell[1];
                    }
                }
                if (p_count == 2 && empty_count == 1 && empty_row == row && empty_col == col)
                {
                    return true;
                }
            }
            return false;
        }

        // first empty cell (in scan order) where playing `p` wins
        ai_move find_completing(const Game &game, const player_type p)
        {
            for (int r = 0; r < size; ++r)
            {
                for (int c = 0; c < size; ++c)
                {
                    if (game.get_mark(r, c) == player_type::none && completes_line(game, r, c, p))
                    {
                        return {r, c};
                    }
                }
            }
            return {-1, -1};
        }

        // first empty cell in scan order
        ai_move find_first_empty(const Game &game)
        {
            for (int r = 0; r < size; ++r)
            {
                for (int c = 0; c < size; ++c)
                {
                    if (game.get_mark(r, c) == player_type::none)
                    {
                        return {r, c};
                    }
                }
            }
            return {-1, -1};
        }

        ai_move choose_hard(const Game &game, const player_type computer)
        {
            board_t b{};
            load_board(game, b);
            const auto other = (computer == player_type::x) ? player_type::o : player_type::x;

            ai_move best{-1, -1};
            int best_score = -2;
            for (int r = 0; r < size; ++r)
            {
                for (int c = 0; c < size; ++c)
                {
                    if (b[r][c] == player_type::none)
                    {
                        b[r][c] = computer;
                        const int s = minimax(b, computer, other);
                        b[r][c] = player_type::none;
                        if (s > best_score)
                        {
                            best_score = s;
                            best = {r, c};
                        }
                    }
                }
            }
            return best;
        }
    }  // namespace

    int minimax_score(const Game &game, const player_type for_player, const player_type to_move)
    {
        board_t b{};
        load_board(game, b);
        return minimax(b, for_player, to_move);
    }

    ai_move choose_move(const Game &game, const player_type computer, const difficulty d)
    {
        if (d == difficulty::easy)
        {
            ai_move candidates[9];
            int n = 0;
            for (int r = 0; r < size; ++r)
            {
                for (int c = 0; c < size; ++c)
                {
                    if (game.get_mark(r, c) == player_type::none)
                    {
                        candidates[n++] = {r, c};
                    }
                }
            }
            return (n > 0) ? candidates[std::rand() % n] : ai_move{-1, -1};
        }

        if (d == difficulty::normal)
        {
            const auto human = game.get_human();
            if (auto m = find_completing(game, computer); m.row >= 0)
            {
                return m;
            }
            if (auto m = find_completing(game, human); m.row >= 0)
            {
                return m;
            }
            if (game.get_mark(1, 1) == player_type::none)
            {
                return {1, 1};
            }
            static const ai_move corners[4] = {{0, 0}, {0, 2}, {2, 0}, {2, 2}};
            for (const auto &c : corners)
            {
                if (game.get_mark(c.row, c.col) == player_type::none)
                {
                    return c;
                }
            }
            return find_first_empty(game);
        }

        return choose_hard(game, computer);
    }
}  // namespace zb::app::tictactoe
