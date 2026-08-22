#include "test.hpp"

#include <memory>

#include "tictactoe.hpp"
#include "tictactoe_layout.hpp"

using namespace zb::app;
using namespace zb::app::tictactoe;
using namespace zb::ui;

namespace
{
    // geometry (and the probe/button coordinates derived from it) comes
    // from tictactoe_layout.hpp -- the single source the view also uses
    using namespace zb::app::tictactoe::layout;

    // absolute centers of the board cells
    int cell_x(const int col) { return board_x + col * board_cell + board_cell / 2; }
    int cell_y(const int row) { return board_y + row * board_cell + board_cell / 2; }

    const core::Color board_bg = core::Color::from(100, 190, 70);
    const core::Color frame_bg = core::Color::from(240, 240, 240);

    void click(const zb::SharedPtr<IApp> &app, const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = x;
        ev.y = y;
        app->input(ev);
        ev.type = zb::input::input_type::mouse_left_up;
        app->input(ev);
    }

    void play(const zb::SharedPtr<IApp> &app, const int row, const int col)
    {
        click(app, cell_x(col), cell_y(row));
    }

    uint32_t pixel_at_window(const zb::SharedPtr<IWindow> &w, const int x, const int y)
    {
        core::Graphics view(
            static_cast<uint32_t>(w->width()),
            static_cast<uint32_t>(w->height()),
            w->data());
        return test::pixel_at(view, x, y);
    }

    zb::SharedPtr<IApp> make_app()
    {
        auto app = zb::make_shared<Tictactoe>();
        app->create_window(window_w, window_h);
        app->paint();
        return app;
    }

    /*
     * Plays a full round: X takes the corners first, then the sides, while
     * the deterministic normal AI blocks every threat. X fills the last
     * empty cell: no line is completed, so the game ends in a DRAW with
     * the game-over dialog open.
     */
    void play_draw_round(const zb::SharedPtr<IApp> &app)
    {
        play(app, 0, 0);  // x(0,0)  -> o(1,1) center
        play(app, 2, 0);  // x(2,0)  -> o(1,0) block column 0
        play(app, 0, 2);  // x(0,2)  -> o(0,1) block row 0
        play(app, 2, 1);  // x(2,1)  -> o(2,2) block row 2
        play(app, 1, 2);  // x(1,2)  -> board full, DRAW
    }
}

int test_app_flow()
{
    // the round setup is a two-step flow: difficulty first, then the side
    {
        auto app = make_app();
        // step 1: the difficulty dialog is visible
        EXPECT(pixel_at_window(app->window(), step_probe_x, step_probe_y) == frame_bg.pixel);
        EXPECT(pixel_at_window(app->window(), mask_probe_x, mask_probe_y) != board_bg.pixel);
        // step 2: picking a difficulty opens the side dialog
        click(app, normal_btn_x, btn_y);
        EXPECT(pixel_at_window(app->window(), step_probe_x, step_probe_y) == frame_bg.pixel);
        // picking O SECOND starts the round: the computer (X) opens center
        click(app, second_btn_x, btn_y);
        const auto w = app->window();
        EXPECT(pixel_at_window(w, mask_probe_x, mask_probe_y) == board_bg.pixel);
        EXPECT(pixel_at_window(w, cell_x(1), cell_y(1)) == core::colors::White.pixel);
        EXPECT(pixel_at_window(w, cell_x(1) - 34, cell_y(1) - 34) != core::colors::White.pixel);
    }

    // one exchange: human X at (0,0), the computer answers at the center
    {
        auto app = make_app();
        click(app, normal_btn_x, btn_y);
        click(app, first_btn_x, btn_y);
        play(app, 0, 0);
        const auto w = app->window();
        EXPECT(pixel_at_window(w, cell_x(0), cell_y(0)) == core::colors::White.pixel);
        // the O ring of the center cell: the point 26px above the center
        EXPECT(pixel_at_window(w, cell_x(1), cell_y(1) - 26) == core::colors::White.pixel);
        EXPECT(pixel_at_window(w, mask_probe_x, mask_probe_y) == board_bg.pixel);
    }

    // a full round ends with the game-over dialog (mask dims the board)
    {
        auto app = make_app();
        click(app, normal_btn_x, btn_y);
        click(app, first_btn_x, btn_y);
        play_draw_round(app);
        const auto w = app->window();
        EXPECT(pixel_at_window(w, mask_probe_x, mask_probe_y) != board_bg.pixel);
        EXPECT(pixel_at_window(w, result_probe_x, result_probe_y) == frame_bg.pixel);
    }

    // AGAIN reopens the round setup with a cleared board
    {
        auto app = make_app();
        click(app, normal_btn_x, btn_y);
        click(app, first_btn_x, btn_y);
        play_draw_round(app);
        click(app, again_btn_x, again_btn_y);
        const auto w = app->window();
        EXPECT(pixel_at_window(w, step_probe_x, step_probe_y) == frame_bg.pixel);
        // the setup dialog masks the (now cleared) board
        EXPECT(pixel_at_window(w, mask_probe_x, mask_probe_y) != board_bg.pixel);
    }

    return test::report("app_flow");
}
