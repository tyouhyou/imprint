#include "test.hpp"

#include <cstring>

int test_panel();
int test_button();
int test_label();
int test_dialog();
int test_dispatch();
int test_hit_override();
int test_on_input_custom();
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
int test_render_mode();
int test_event();
int test_text();
int test_flex();
int test_percent();
int test_ptr();
int test_codec();
int test_gif();
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
int test_html();
int test_preview();

int test_widget_size();
int test_remove();
int test_layout_dirty();
int test_alloc_guard();
int test_perf_walk();
int test_raster_damage();
int test_draw_surface();
int test_depth_fill();
int test_scroll_panel();
int test_pixel_convert();
int test_pixel_traits();
int test_shell_presenter();
int test_snapshot();
int test_sixel();
int test_term_input();
int test_theme();
int test_showcase();
int test_toggle_switch();
int test_gauge_dial();
int test_knob();
int test_trend_line();
int test_svg();

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
#if defined(IMCORE_HAS_TTF_RUNTIME)
int test_runtime_ttf();
#endif

struct suite
{
    const char* name;
    int (*fn)();
};

static const suite g_suites[] = {
    {"panel", test_panel},
    {"button", test_button},
    {"label", test_label},
    {"dialog", test_dialog},
    {"dispatch", test_dispatch},
    {"hit_override", test_hit_override},
    {"on_input_custom", test_on_input_custom},
    {"focus", test_focus},
    {"canvas_window", test_canvas_window},
    {"board", test_board},
    {"game", test_game},
    {"ai", test_ai},
    {"blit_font", test_blit_font},
    {"app_flow", test_app_flow},
    {"automation", test_automation},
    {"quit", test_quit},
    {"graphics", test_graphics},
    {"render_mode", test_render_mode},
    {"event", test_event},
    {"text", test_text},
    {"flex", test_flex},
    {"percent", test_percent},
    {"ptr", test_ptr},
    {"codec", test_codec},
    {"gif", test_gif},
    {"checkbox", test_checkbox},
    {"radio", test_radio},
    {"slider", test_slider},
    {"progress_bar", test_progress_bar},
    {"list_box", test_list_box},
    {"text_input", test_text_input},
    {"builder", test_builder},
    {"measure", test_measure},
    {"dirty", test_dirty},
    {"ui_file", test_ui_file},
    {"html", test_html},
    {"preview", test_preview},
    {"widget_size", test_widget_size},
    {"remove", test_remove},
    {"layout_dirty", test_layout_dirty},
    {"alloc_guard", test_alloc_guard},
    {"perf_walk", test_perf_walk},
    {"raster_damage", test_raster_damage},
    {"draw_surface", test_draw_surface},
    {"depth_fill", test_depth_fill},
    {"scroll_panel", test_scroll_panel},
    {"pixel_convert", test_pixel_convert},
    {"pixel_traits", test_pixel_traits},
    {"shell_presenter", test_shell_presenter},
    {"snapshot", test_snapshot},
    {"sixel", test_sixel},
    {"term_input", test_term_input},
    {"theme", test_theme},
    {"showcase", test_showcase},
    {"toggle_switch", test_toggle_switch},
    {"gauge_dial", test_gauge_dial},
    {"knob", test_knob},
    {"trend_line", test_trend_line},
    {"svg", test_svg},
#if defined(_WIN32)
    {"win_input", test_win_input},
#endif
#if defined(IM_TEST_X11_INPUT)
    {"x11_input", test_x11_input},
#endif
#if defined(IMCORE_HAS_TTF_SUBSET)
    {"ttf_subset", test_ttf_subset},
    {"font_size", test_font_size},
#endif
#if defined(IMCORE_HAS_TTF_RUNTIME)
    {"runtime_ttf", test_runtime_ttf},
#endif
};

static constexpr int kMaxSelected = 128;
static_assert(sizeof(g_suites) / sizeof(g_suites[0]) <= kMaxSelected,
              "raise kMaxSelected");

// usage: test_imui [--list] [filter ...]
//   --list        print matching suite names (all suites when no filter given)
//   filter        substring match against suite names; a suite runs when it
//                 matches any filter; no filter means run everything
int main(int argc, char** argv)
{
    bool list_only = false;
    const char* filters[16] = {};
    int filter_count = 0;
    for (int i = 1; i < argc && filter_count < 16; ++i)
    {
        if (std::strcmp(argv[i], "--list") == 0)
            list_only = true;
        else
            filters[filter_count++] = argv[i];
    }

    constexpr int total_suites =
        static_cast<int>(sizeof(g_suites) / sizeof(g_suites[0]));

    const suite* selected[kMaxSelected];
    int selected_count = 0;
    for (const suite& s : g_suites)
    {
        if (filter_count == 0)
        {
            selected[selected_count++] = &s;
            continue;
        }
        for (int i = 0; i < filter_count; ++i)
        {
            if (std::strstr(s.name, filters[i]) != nullptr)
            {
                selected[selected_count++] = &s;
                break;
            }
        }
    }

    if (list_only)
    {
        for (int i = 0; i < selected_count; ++i)
            std::printf("%s\n", selected[i]->name);
        return 0;
    }

    if (selected_count == 0)
    {
        std::printf("no suite matches filter\n");
        return 2;
    }

    std::printf("suites: run=%d total=%d\n", selected_count, total_suites);

    int failures = 0;
    for (int i = 0; i < selected_count; ++i)
        failures += selected[i]->fn();

    if (failures)
    {
        std::printf("TOTAL: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
