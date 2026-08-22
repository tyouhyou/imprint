// QUIT regression: a partial repaint must not smear over pruned neighbors.
//
// Clicking the result dialog's QUIT button damages only the QUIT rect. The
// frame panel (an ancestor spanning the whole dialog) intersects that rect
// and repaints its full background; its other children (AGAIN, title) are
// damage-culled. Without a region-exact rasterizer those pruned widgets'
// pixels were wiped by the frame's background fill -- AGAIN vanished under
// f0f0f0 while QUIT redrew on top.
#include "test.hpp"

#include "tictactoe.hpp"
#include "tictactoe_layout.hpp"

using namespace zb::app;
using namespace zb::app::tictactoe;
using namespace zb::ui;
using namespace zb::app::tictactoe::layout;

namespace
{
    // geometry (and the probe/button coordinates derived from it) comes
    // from tictactoe_layout.hpp -- the single source the view also uses

    int cell_x(const int col) { return board_x + col * board_cell + board_cell / 2; }
    int cell_y(const int row) { return board_y + row * board_cell + board_cell / 2; }

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

    /*
     * Plays a deterministic round ending in DRAW with the game-over dialog
     * open (same script as test_app_flow).
     */
    void play_draw_round(const zb::SharedPtr<IApp> &app)
    {
        play(app, 0, 0);
        play(app, 2, 0);
        play(app, 0, 2);
        play(app, 2, 1);
        play(app, 1, 2);
    }
}

int test_quit()
{
    auto app = zb::make_shared<Tictactoe>();
    app->create_window(window_w, window_h);
    app->paint();

    click(app, normal_btn_x, btn_y);
    click(app, first_btn_x, btn_y);
    play_draw_round(app);

    const auto w = app->window();

    // sane start: both buttons of the result dialog are on screen
    // (AGAIN center = white interior; left edge = button border)
    EXPECT(pixel_at_window(w, again_btn_x, again_btn_y) == core::colors::White.pixel);
    EXPECT(pixel_at_window(w, again_btn_x - 40, again_btn_y) == core::colors::Black.pixel);

    // pressing QUIT repaints the press visual; AGAIN must survive it
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = quit_btn_x;
        ev.y = again_btn_y;
        app->input(ev);
        EXPECT(pixel_at_window(w, again_btn_x, again_btn_y) == core::colors::White.pixel);
        ev.type = zb::input::input_type::mouse_left_up;
        app->input(ev);
    }

    // after the release (the actual close request) AGAIN is untouched:
    // its pixels lie outside the repainted region and nothing may wipe them
    EXPECT(pixel_at_window(w, again_btn_x, again_btn_y) == core::colors::White.pixel);
    EXPECT(pixel_at_window(w, again_btn_x - 40, again_btn_y) == core::colors::Black.pixel);

    return test::report("quit");
}
