#include "test.hpp"

#include "ai.hpp"
#include "game.hpp"

using namespace zb::app::tictactoe;

int test_ai()
{
    // easy: always a legal (empty) move
    {
        Game g;
        g.set_human(player_type::x);
        g.start_round(player_type::x);
        g.set_mark(0, 0, player_type::x);
        g.set_mark(1, 1, player_type::o);
        for (int i = 0; i < 5; ++i)
        {
            const auto m = choose_move(g, player_type::o, difficulty::easy);
            EXPECT(m.row >= 0 && m.col >= 0);
            EXPECT(g.get_mark(m.row, m.col) == player_type::none);
        }
    }

    // normal: takes the winning cell
    {
        Game g;
        g.set_human(player_type::x);
        g.start_round(player_type::x);
        g.set_mark(0, 0, player_type::o);
        g.set_mark(0, 1, player_type::o);
        g.set_mark(1, 1, player_type::x);
        const auto m = choose_move(g, player_type::o, difficulty::normal);
        EXPECT(m.row == 0 && m.col == 2);
    }

    // normal: blocks the human's winning cell
    {
        Game g;
        g.set_human(player_type::x);
        g.start_round(player_type::x);
        g.set_mark(2, 0, player_type::x);
        g.set_mark(2, 1, player_type::x);
        g.set_mark(0, 0, player_type::o);
        const auto m = choose_move(g, player_type::o, difficulty::normal);
        EXPECT(m.row == 2 && m.col == 2);
    }

    // normal: takes the center when nothing is threatened
    {
        Game g;
        g.set_human(player_type::x);
        g.start_round(player_type::x);
        g.set_mark(2, 2, player_type::x);
        g.set_mark(0, 0, player_type::o);
        const auto m = choose_move(g, player_type::o, difficulty::normal);
        EXPECT(m.row == 1 && m.col == 1);
    }

    // normal: takes a corner when the center is taken
    {
        Game g;
        g.set_human(player_type::x);
        g.start_round(player_type::x);
        g.set_mark(1, 1, player_type::x);
        g.set_mark(0, 2, player_type::o);
        const auto m = choose_move(g, player_type::o, difficulty::normal);
        EXPECT(m.row == 0 && m.col == 0);
    }

    // hard: opening move is a corner (first optimal cell in scan order)
    {
        Game g;
        g.set_human(player_type::x);
        g.start_round(player_type::x);
        const auto m = choose_move(g, player_type::o, difficulty::hard);
        EXPECT(m.row == 0 && m.col == 0);
    }

    // hard: never loses from positions that are not already lost
    {
        Game g;
        g.set_human(player_type::x);
        g.start_round(player_type::x);
        // (a) empty board, (b) one human move, (c) a few moves
        for (int trial = 0; trial < 3; ++trial)
        {
            g.start_round(player_type::x);
            if (trial >= 1)
            {
                g.set_mark(0, 0, player_type::x);
            }
            if (trial >= 2)
            {
                g.set_mark(1, 1, player_type::o);
                g.set_mark(2, 2, player_type::x);
            }
            const auto m = choose_move(g, player_type::o, difficulty::hard);
            EXPECT(m.row >= 0 && m.col >= 0);
            g.set_mark(m.row, m.col, player_type::o);
            EXPECT(minimax_score(g, player_type::o, player_type::x) >= 0);
        }
    }

    // minimax_score: forced win, forced loss, draw
    {
        Game g;
        g.set_human(player_type::x);
        g.start_round(player_type::x);
        // o has two in a row and is to move: forced win
        g.set_mark(0, 0, player_type::o);
        g.set_mark(0, 1, player_type::o);
        g.set_mark(1, 0, player_type::x);
        g.set_mark(1, 1, player_type::x);
        EXPECT(minimax_score(g, player_type::o, player_type::o) == 1);
        EXPECT(minimax_score(g, player_type::x, player_type::o) == -1);
    }

    // minimax_score: a finished win for o scores -1 for x
    {
        Game g;
        g.set_mark(0, 0, player_type::o);
        g.set_mark(0, 1, player_type::o);
        g.set_mark(0, 2, player_type::o);
        EXPECT(minimax_score(g, player_type::x, player_type::x) == -1);
        EXPECT(minimax_score(g, player_type::o, player_type::x) == 1);
    }

    // full board: no move available for any difficulty
    {
        Game g;
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
            {
                g.set_mark(r, c, ((r + c) % 2 == 0) ? player_type::x : player_type::o);
            }
        }
        EXPECT(choose_move(g, player_type::x, difficulty::easy).row == -1);
        EXPECT(choose_move(g, player_type::x, difficulty::normal).row == -1);
        EXPECT(choose_move(g, player_type::x, difficulty::hard).row == -1);
    }

    return test::report("ai");
}
