#ifndef APPGAME_HPP
#define APPGAME_HPP

#include "tictactoe_defines.hpp"

namespace zb::app::tictactoe
{
    /*
     * Tic-tac-toe rules engine: sides, board, turn alternation, win/draw
     * detection. The human plays against the computer; the computer side is
     * derived from the human side.
     */
    class Game
    {
    public:
        Game() = default;
        ~Game() = default;

        // sets the human side; the computer takes the other
        void set_human(const player_type &human);
        [[nodiscard]] player_type get_human() const;
        [[nodiscard]] player_type get_computer() const;

        // clears the board and sets the player who moves first
        void start_round(const player_type &first);

        /*
         * Places the current player's mark and advances the turn. False if
         * the cell is taken or out of range (the turn is not consumed).
         */
        bool place_mark(const int row, const int col);
        // raw placement for a given player (used by the AI); same rules
        bool set_mark(const int row, const int col, const player_type p);
        [[nodiscard]] player_type get_mark(const int row, const int col) const;

        // winner of the current board, or player_type::none
        [[nodiscard]] player_type check_winner() const;
        [[nodiscard]] bool is_full() const;

        [[nodiscard]] int get_rows() const;  // clos = rows
        [[nodiscard]] player_type get_current_player() const;

    private:
        void toggle_current();

        static constexpr int rows = 3;  // clos = rows

        player_type human = player_type::none;
        player_type computer = player_type::none;
        player_type current_player = player_type::none;

        player_type board[rows][rows]{};
    };

}

#endif // !APPGAME_HPP
