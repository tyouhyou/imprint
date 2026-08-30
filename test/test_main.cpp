#include "test.hpp"

int test_panel();
int test_button();
int test_label();
int test_dialog();
int test_dispatch();
int test_focus();
int test_canvas_window();
int test_board();
int test_game();
int test_ai();
int test_blit_font();
int test_app_flow();
int test_automation();
int test_quit();
int test_graphics();
int test_event();
int test_text();
int test_flex();
int test_ptr();
int test_codec();
int test_checkbox();
int test_radio();
int test_slider();
int test_progress_bar();
int test_list_box();
int test_text_input();
int test_builder();
int test_measure();
int test_dirty();
int test_ui_file();

int test_widget_size();
int test_remove();
int test_layout_dirty();
int test_alloc_guard();
int test_perf_walk();
int test_raster_damage();
int test_pixel_convert();
int test_pixel_traits();
int test_shell_presenter();
int test_theme();
int test_showcase();

#if defined(_WIN32)
int test_win_input();
#endif
#if defined(IM_TEST_X11_INPUT)
int test_x11_input();
#endif
#if defined(IMCORE_HAS_TTF_SUBSET)
int test_ttf_subset();
int test_font_size();
#endif

int main()
{
    int total = 0;
    total += test_panel();
    total += test_button();
    total += test_label();
    total += test_dialog();
    total += test_dispatch();
    total += test_focus();
    total += test_canvas_window();
    total += test_board();
    total += test_game();
    total += test_ai();
    total += test_blit_font();
    total += test_app_flow();
    total += test_automation();
    total += test_quit();
    total += test_graphics();
    total += test_event();
    total += test_text();
    total += test_flex();
    total += test_ptr();
    total += test_codec();
    total += test_checkbox();
    total += test_radio();
    total += test_slider();
    total += test_progress_bar();
    total += test_list_box();
    total += test_text_input();
    total += test_builder();
    total += test_measure();
    total += test_dirty();
    total += test_ui_file();
    total += test_widget_size();
    total += test_remove();
    total += test_layout_dirty();
    total += test_alloc_guard();
    total += test_perf_walk();
    total += test_raster_damage();
    total += test_pixel_convert();
    total += test_pixel_traits();
    total += test_shell_presenter();
    total += test_theme();
    total += test_showcase();

#if defined(_WIN32)
    total += test_win_input();
#endif
#if defined(IM_TEST_X11_INPUT)
    total += test_x11_input();
#endif
#if defined(IMCORE_HAS_TTF_SUBSET)
    total += test_ttf_subset();
    total += test_font_size();
#endif

    if (total)
    {
        std::printf("TOTAL: %d failure(s)\n", total);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
