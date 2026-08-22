#pragma once

/*
 * Shared demo geometry (Z35): the view consumes the frame/button/board
 * constants and the end-to-end test derives its click and probe pixels
 * from the same numbers -- a geometry change and its test update now
 * travel in one file. The button CENTER constants are pinned by
 * Dialog::layout's button-row packing: when the sizes above change,
 * update them together.
 */
namespace zb::app::tictactoe::layout
{
    // the window the demo targets
    constexpr int window_w = 320;
    constexpr int window_h = 240;

    // board: a square on the shorter window edge with a 1/20 margin,
    // centered -- 216x216 at (52,12) in the default window, 72px cells
    constexpr int board_margin_den = 20;

    // step dialogs (difficulty / side): frame 200x64 centered; the
    // result dialog frame is 200x84
    constexpr int step_frame_w = 200;
    constexpr int step_frame_h = 64;
    constexpr int result_frame_w = 200;
    constexpr int result_frame_h = 84;
    constexpr int dialog_padding = 10;
    constexpr int title_height = 16;
    constexpr int content_width = step_frame_w - 2 * dialog_padding;  // 180

    constexpr int diff_button_w = 56;
    constexpr int diff_button_h = 24;
    constexpr int side_button_w = 86;
    constexpr int side_button_h = 24;
    constexpr int result_button_w = 88;
    constexpr int result_button_h = 20;

    // ---- derived for the default window ----
    constexpr int board_margin = window_h / board_margin_den;         // 12
    constexpr int board_size = window_h - 2 * board_margin;           // 216
    constexpr int board_x = (window_w - board_size) / 2;              // 52
    constexpr int board_y = (window_h - board_size) / 2;              // 12
    constexpr int board_cell = board_size / 3;                        // 72

    constexpr int step_frame_x = (window_w - step_frame_w) / 2;       // 60
    constexpr int step_frame_y = (window_h - step_frame_h) / 2;       // 88
    constexpr int result_frame_x = (window_w - result_frame_w) / 2;   // 60
    constexpr int result_frame_y = (window_h - result_frame_h) / 2;   // 78

    // ---- button centers (Dialog::layout's row packing, see above) ----
    constexpr int normal_btn_x = 158;
    constexpr int btn_y = 130;
    constexpr int first_btn_x = 113;
    constexpr int second_btn_x = 203;
    constexpr int again_btn_x = 110;
    constexpr int again_btn_y = 142;
    constexpr int quit_btn_x = 206;

    // ---- probe pixels (frame-colored rows just under the titles) ----
    constexpr int step_probe_x = 160;
    constexpr int step_probe_y = 99;
    constexpr int result_probe_x = 160;
    constexpr int result_probe_y = 89;
    constexpr int mask_probe_x = 56;
    constexpr int mask_probe_y = 120;
}  // namespace zb::app::tictactoe::layout
