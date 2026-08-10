#ifndef TICTACTOE_AI_HPP
#define TICTACTOE_AI_HPP

#include "game.hpp"
#include "tictactoe_defines.hpp"

namespace zb::app::tictactoe
{
    struct ai_move
    {
        int row = -1;  // -1 = no move available (board full)
        int col = -1;
    };

    /*
     * The computer opponent. choose_move() returns a legal empty cell for
     * the given difficulty:
     *   - easy:   uniform random among the empty cells
     *   - normal: win if possible, else block the human's win, else
     *             center, else a corner, else any empty cell
     *   - hard:   minimax, optimal play (never loses)
     * normal and hard are deterministic (ties broken by scan order).
     */

    /*
     * Score of the current board for `for_player` assuming optimal play on
     * both sides from the player who is `to_move`: +1 = forced win, 0 =
     * draw, -1 = forced loss. Exposed for tests.
     */
    int minimax_score(const Game &game, const player_type for_player, const player_type to_move);

    ai_move choose_move(const Game &game, const player_type computer, const difficulty d);
}

#endif // TICTACTOE_AI_HPP
