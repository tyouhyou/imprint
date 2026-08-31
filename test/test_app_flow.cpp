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

    // the NDS shell delta: a 256x192 window driven exclusively by touch
    // events (touch_id 0). Locks the ROM regression where dialog buttons
    // claimed the press but never activated (the release arrived with no
    // pressed target); geometry is derived like the view does for NDS
    {
        auto app = zb::make_shared<Tictactoe>();
        app->create_window(256, 192);
        app->paint();

        const int bsize = 192 - 2 * (192 / board_margin_den);   // board 174
        const int bx = (256 - bsize) / 2;
        const int by = (192 - bsize) / 2;
        const int bcell = bsize / 3;
        const int fx = (256 - step_frame_w) / 2;
        const int fy = (192 - step_frame_h) / 2;
        // dialog button-row packing (Dialog::layout, spacing 4)
        const int btn_cy = fy + step_frame_h - dialog_padding - diff_button_h / 2;
        const int normal_cx = fx + dialog_padding + diff_button_w + 4 + diff_button_w / 2;
        const int first_cx = fx + dialog_padding + side_button_w / 2;

        // down, then the shell's idle-loop frames while held, then up
        // (the NDS shell paints owed frames between the two events)
        auto touch = [&app](const int x, const int y, const int hold_frames = 0)
        {
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::touch_down;
            ev.x = x;
            ev.y = y;
            ev.touch_id = 0;
            app->input(ev);
            for (int i = 0; i < hold_frames; ++i)
            {
                if (app->is_dirty())
                {
                    app->paint();
                }
            }
            ev.type = zb::input::input_type::touch_up;
            app->input(ev);
        };

        touch(normal_cx, btn_cy, 8);  // NORMAL -> side dialog
        touch(first_cx, btn_cy);      // X FIRST -> round starts, the mask clears
        const auto w = app->window();
        // left of the board the dialog mask is gone: root background again
        EXPECT(pixel_at_window(w, 20, 100) == core::Color::from(28, 148, 64).pixel);
        touch(bx + bcell / 2, by + bcell / 2);  // human X at cell (0,0)
        EXPECT(pixel_at_window(w, bx + bcell / 2, by + bcell / 2) == core::colors::White.pixel);

        // touch jitter while held (the panel drifts a pixel or two) must
        // not eat the click: the O SECOND path here ends the round setup
        // with the computer (X) opening center
        auto touch_with_moves = [](const zb::SharedPtr<IApp> &target, const int x, const int y,
                                   const std::initializer_list<std::pair<int, int>> &moves)
        {
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::touch_down;
            ev.x = x;
            ev.y = y;
            ev.touch_id = 0;
            target->input(ev);
            ev.type = zb::input::input_type::touch_move;
            for (const auto &m : moves)
            {
                ev.x = x + m.first;
                ev.y = y + m.second;
                target->input(ev);
            }
            ev.type = zb::input::input_type::touch_up;
            ev.x = x;
            ev.y = y;
            target->input(ev);
        };
        auto plain_touch = [](const zb::SharedPtr<IApp> &target, const int x, const int y)
        {
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::touch_down;
            ev.x = x;
            ev.y = y;
            ev.touch_id = 0;
            target->input(ev);
            ev.type = zb::input::input_type::touch_up;
            target->input(ev);
        };

        {
            auto app2 = zb::make_shared<Tictactoe>();
            app2->create_window(256, 192);
            app2->paint();
            touch_with_moves(app2, normal_cx, btn_cy, {{1, 0}, {-1, 1}, {0, -1}});
            plain_touch(app2, first_cx, btn_cy);
            const auto w2 = app2->window();
            EXPECT(pixel_at_window(w2, 20, 100) == core::Color::from(28, 148, 64).pixel);
        }

        // a single glitch reading far away (touch-panel spike) must not
        // eat the click either
        {
            auto app3 = zb::make_shared<Tictactoe>();
            app3->create_window(256, 192);
            app3->paint();
            touch_with_moves(app3, normal_cx, btn_cy, {{-normal_cx, -btn_cy}});  // spike to (0,0)
            plain_touch(app3, first_cx, btn_cy);
            const auto w3 = app3->window();
            EXPECT(pixel_at_window(w3, 20, 100) == core::Color::from(28, 148, 64).pixel);
        }
    }

    return test::report("app_flow");
}
