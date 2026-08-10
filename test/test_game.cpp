#include "test.hpp"

#include "game.hpp"

using namespace zb::app::tictactoe;

int test_game()
{
    // turn alternation: place_mark places the current player and toggles
    {
        Game g;
        g.set_human(player_type::x);
        g.start_round(player_type::o);
        EXPECT(g.get_current_player() == player_type::o);
        EXPECT(g.place_mark(1, 1));  // O center
        EXPECT(g.get_current_player() == player_type::x);
        EXPECT(g.place_mark(0, 0));  // X corner
        EXPECT(g.get_current_player() == player_type::o);
        // a rejected placement does not consume the turn
        EXPECT(!g.place_mark(1, 1));  // occupied
        EXPECT(g.get_current_player() == player_type::o);
        EXPECT(!g.place_mark(3, 0));  // out of range
        EXPECT(g.get_current_player() == player_type::o);
    }

    // human/computer sides
    {
        Game g;
        g.set_human(player_type::o);
        EXPECT(g.get_human() == player_type::o);
        EXPECT(g.get_computer() == player_type::x);
        g.set_human(player_type::x);
        EXPECT(g.get_human() == player_type::x);
        EXPECT(g.get_computer() == player_type::o);
    }

    // win by row
    {
        Game g;
        g.set_mark(0, 0, player_type::x);
        g.set_mark(0, 1, player_type::x);
        g.set_mark(0, 2, player_type::x);
        EXPECT(g.check_winner() == player_type::x);
    }

    // win by column
    {
        Game g;
        g.set_mark(0, 1, player_type::o);
        g.set_mark(1, 1, player_type::o);
        g.set_mark(2, 1, player_type::o);
        EXPECT(g.check_winner() == player_type::o);
    }

    // win by main diagonal
    {
        Game g;
        g.set_mark(0, 0, player_type::x);
        g.set_mark(1, 1, player_type::x);
        g.set_mark(2, 2, player_type::x);
        EXPECT(g.check_winner() == player_type::x);
    }

    // win by anti diagonal
    {
        Game g;
        g.set_mark(0, 2, player_type::o);
        g.set_mark(1, 1, player_type::o);
        g.set_mark(2, 0, player_type::o);
        EXPECT(g.check_winner() == player_type::o);
    }

    // no winner yet
    {
        Game g;
        g.set_mark(0, 0, player_type::x);
        g.set_mark(1, 1, player_type::o);
        EXPECT(g.check_winner() == player_type::none);
        EXPECT(!g.is_full());
    }

    // draw: full board without a line
    {
        Game g;
        g.set_mark(0, 0, player_type::x);
        g.set_mark(0, 1, player_type::o);
        g.set_mark(0, 2, player_type::x);
        g.set_mark(1, 0, player_type::o);
        g.set_mark(1, 1, player_type::x);
        g.set_mark(1, 2, player_type::o);
        g.set_mark(2, 0, player_type::o);
        g.set_mark(2, 1, player_type::x);
        g.set_mark(2, 2, player_type::o);
        EXPECT(g.is_full());
        EXPECT(g.check_winner() == player_type::none);
    }

    // a full line on a full board still reports the winner
    {
        Game g;
        g.set_mark(0, 0, player_type::o);
        g.set_mark(0, 1, player_type::o);
        g.set_mark(0, 2, player_type::o);
        g.set_mark(1, 0, player_type::x);
        g.set_mark(1, 1, player_type::x);
        g.set_mark(2, 0, player_type::x);
        EXPECT(g.is_full() == false);
        EXPECT(g.check_winner() == player_type::o);
    }

    // start_round resets the board and sets the first player
    {
        Game g;
        g.set_human(player_type::o);
        g.start_round(player_type::x);
        g.place_mark(0, 0);  // X
        g.place_mark(1, 1);  // O
        EXPECT(g.get_current_player() == player_type::x);
        g.start_round(player_type::x);
        EXPECT(g.get_mark(0, 0) == player_type::none);
        EXPECT(g.get_mark(1, 1) == player_type::none);
        EXPECT(g.get_current_player() == player_type::x);
    }

    // set_mark rejects none / out of range / occupied
    {
        Game g;
        EXPECT(!g.set_mark(0, 0, player_type::none));
        EXPECT(!g.set_mark(-1, 0, player_type::x));
        EXPECT(!g.set_mark(3, 0, player_type::x));
        EXPECT(g.set_mark(0, 0, player_type::x));
        EXPECT(!g.set_mark(0, 0, player_type::o));
    }

    return test::report("game");
}
