#include "test.hpp"

#include "board.hpp"
#include "imui.hpp"

using namespace zb::app::tictactoe;
using namespace zb::ui;

int test_board()
{
    // click on a cell emits row/col
    {
        Panel root;
        root.set_size(100, 100);

        auto b = std::make_unique<Board>();
        b->set_size(60, 60);
        b->set_position(20, 20);
        auto *pboard = b.get();
        int rr = -1;
        int cc = -1;
        pboard->cell_clicked += [&rr, &cc](const int row, const int col)
        {
            rr = row;
            cc = col;
        };
        root.add_child(std::move(b));

        InputDispatcher d;
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;

        // board spans (20,20)..(79,79), cell = 60/3 = 20.
        // cell (1,2): local (40..59, 20..39) -> root (60..79, 40..59)
        ev.x = 65;
        ev.y = 45;
        d.dispatch(root, ev);
        EXPECT(rr == 1 && cc == 2);
    }

    // click outside the board does not emit
    {
        Panel root;
        root.set_size(100, 100);

        auto b = std::make_unique<Board>();
        b->set_size(60, 60);
        b->set_position(20, 20);
        auto *pboard = b.get();
        int calls = 0;
        pboard->cell_clicked += [&calls](const int, const int) { ++calls; };
        root.add_child(std::move(b));

        InputDispatcher d;
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = 5;
        ev.y = 5;
        d.dispatch(root, ev);
        EXPECT(calls == 0);
    }

    // set/get/clear marks
    {
        Board b;
        b.set_mark(0, 0, player_type::x);
        b.set_mark(2, 2, player_type::o);
        EXPECT(b.get_mark(0, 0) == player_type::x);
        EXPECT(b.get_mark(1, 1) == player_type::none);
        b.set_mark(5, 0, player_type::o);  // out of range ignored
        EXPECT(b.get_mark(5, 0) == player_type::none);
        b.clear_marks();
        EXPECT(b.get_mark(0, 0) == player_type::none);
    }

    // drawing: grid lines, X and O marks
    {
        auto g = core::Graphics::make_ptr(30, 30);
        Board b;
        b.set_size(30, 30);
        b.set_background_color(core::colors::White);
        b.set_grid_color(core::colors::Black);
        b.set_mark_color(core::colors::Red);
        b.set_mark_padding(2);
        b.set_mark(0, 0, player_type::x);
        b.set_mark(0, 2, player_type::o);
        b.draw(*g);

        // X in cell (0,0): diagonals from (2,2)-(8,8); center is on it
        EXPECT(test::pixel_at(*g, 5, 5) == core::colors::Red.pixel);
        // O in cell (0,2): center (25,5), radius 3; top point (25,2)
        EXPECT(test::pixel_at(*g, 25, 2) == core::colors::Red.pixel);
        // grid lines at x=10 and y=10
        EXPECT(test::pixel_at(*g, 10, 5) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(*g, 5, 10) == core::colors::Black.pixel);
        // background in a free cell
        EXPECT(test::pixel_at(*g, 15, 15) == core::colors::White.pixel);
    }

    return test::report("board");
}
