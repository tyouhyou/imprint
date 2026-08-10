#include "test.hpp"

#include <memory>

#include "tictactoe.hpp"

using namespace zb::app;
using namespace zb::app::tictactoe;
using namespace zb::ui;

namespace
{
    // 320x240 window: the board is 216x216 at (52, 12), cell = 72
    constexpr int window_w = 320;
    constexpr int window_h = 240;

    // absolute centers of the board cells
    int cell_x(const int col) { return 52 + col * 72 + 36; }
    int cell_y(const int row) { return 12 + row * 72 + 36; }

    // inside the board, outside any dialog frame: (56, 120) is a board cell
    // background pixel that stays unmasked while no dialog is open
    constexpr int mask_probe_x = 56;
    constexpr int mask_probe_y = 120;

    // step dialogs (frame 200x64 centered) have their title at (70, 98);
    // the row just below the 7px-tall text band is always frame-colored
    constexpr int step_probe_x = 160;
    constexpr int step_probe_y = 99;
    // the result dialog (frame 200x84 centered) title sits at (70, 88)
    constexpr int result_probe_x = 160;
    constexpr int result_probe_y = 89;

    // step 1 (difficulty) buttons: EASY/NORMAL/HARD at y 118..142
    constexpr int normal_btn_x = 158;
    constexpr int normal_btn_y = 130;
    // step 2 (side) buttons: X FIRST / O SECOND at y 118..142
    constexpr int first_btn_x = 113;
    constexpr int first_btn_y = 130;
    constexpr int second_btn_x = 203;
    constexpr int second_btn_y = 130;

    // the result dialog AGAIN button
    constexpr int again_btn_x = 110;
    constexpr int again_btn_y = 142;

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
        click(app, normal_btn_x, normal_btn_y);
        EXPECT(pixel_at_window(app->window(), step_probe_x, step_probe_y) == frame_bg.pixel);
        // picking O SECOND starts the round: the computer (X) opens center
        click(app, second_btn_x, second_btn_y);
        const auto w = app->window();
        EXPECT(pixel_at_window(w, mask_probe_x, mask_probe_y) == board_bg.pixel);
        EXPECT(pixel_at_window(w, cell_x(1), cell_y(1)) == core::colors::White.pixel);
        EXPECT(pixel_at_window(w, cell_x(1) - 34, cell_y(1) - 34) != core::colors::White.pixel);
    }

    // one exchange: human X at (0,0), the computer answers at the center
    {
        auto app = make_app();
        click(app, normal_btn_x, normal_btn_y);
        click(app, first_btn_x, first_btn_y);
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
        click(app, normal_btn_x, normal_btn_y);
        click(app, first_btn_x, first_btn_y);
        play_draw_round(app);
        const auto w = app->window();
        EXPECT(pixel_at_window(w, mask_probe_x, mask_probe_y) != board_bg.pixel);
        EXPECT(pixel_at_window(w, result_probe_x, result_probe_y) == frame_bg.pixel);
    }

    // AGAIN reopens the round setup with a cleared board
    {
        auto app = make_app();
        click(app, normal_btn_x, normal_btn_y);
        click(app, first_btn_x, first_btn_y);
        play_draw_round(app);
        click(app, again_btn_x, again_btn_y);
        const auto w = app->window();
        EXPECT(pixel_at_window(w, step_probe_x, step_probe_y) == frame_bg.pixel);
        // the setup dialog masks the (now cleared) board
        EXPECT(pixel_at_window(w, mask_probe_x, mask_probe_y) != board_bg.pixel);
    }

    return test::report("app_flow");
}
