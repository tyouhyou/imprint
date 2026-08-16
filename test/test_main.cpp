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
int test_graphics();
int test_event();
int test_text();
int test_flex();
int test_ptr();
int test_codec();
int test_checkbox();
int test_radio();
int test_slider();
int test_list_box();
int test_text_input();
int test_builder();
int test_measure();
int test_dirty();
int test_ui_file();

int test_widget_size();
int test_remove();

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
    total += test_graphics();
    total += test_event();
    total += test_text();
    total += test_flex();
    total += test_ptr();
    total += test_codec();
    total += test_checkbox();
    total += test_radio();
    total += test_slider();
    total += test_list_box();
    total += test_text_input();
    total += test_builder();
    total += test_measure();
    total += test_dirty();
    total += test_ui_file();
    total += test_widget_size();
    total += test_remove();

    if (total)
    {
        std::printf("TOTAL: %d failure(s)\n", total);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
