/*
 * S4 frame recorder: drives the showcase app through its public API
 * (IApp::input + CanvasWindow paint) and writes the frames as a GIF.
 * Shell-less by design -- CanvasWindow owns the framebuffer directly,
 * the same host-drives-everything contract the automation suite uses.
 *
 * Usage: showcase_gif [out.gif]   (default showcase.gif, 320x240)
 */

#include "gif_encoder.hpp"

#include "canvas_window.hpp"
#include "input.hpp"
#include "showcase.hpp"
#include "theme.hpp"
#include "widget.hpp"

#if defined(IMCORE_USE_FONT_SIZE) && defined(IMCORE_HAS_TTF_SUBSET)
#include "text/ttf_provider.hpp"
#endif

#include <cstdio>
#include <memory>
#include <utility>

namespace
{
    using namespace zb::app::showcase;
    using zb::input::input_event;
    using zb::input::input_type;

    std::unique_ptr<GifWriter> g_gif;

    /* one captured frame = one paint of the current app state */
    void frame(Showcase &app)
    {
        app.paint();
        g_gif->add_frame(static_cast<const uint8_t *>(app.window()->data()));
    }

    input_event touch_ev(const input_type type, const int x, const int y)
    {
        input_event ev{};
        ev.type = type;
        ev.x = x;
        ev.y = y;
        ev.touch_id = 0;
        return ev;
    }

    input_event key_ev(const zb::input::key_code key)
    {
        input_event ev{};
        ev.type = input_type::key_down;
        ev.key = static_cast<int>(key);
        return ev;
    }

    std::pair<int, int> center(zb::app::CanvasWindow &win, const char *id)
    {
        const auto *w = win.root().find_by_id(id);
        if (w == nullptr)
        {
            std::fprintf(stderr, "gif_record: widget '%s' not found\n", id);
            return {0, 0};
        }
        // input events are in window coordinates: absolute, not relative
        // to the parent (the same helper the smoke suite uses)
        const auto p = w->get_absolute_position();
        const auto s = w->get_size();
        return {p.x + s.width / 2, p.y + s.height / 2};
    }

    void click(Showcase &app, zb::app::CanvasWindow &win, const char *id)
    {
        const auto [x, y] = center(win, id);
        app.input(touch_ev(input_type::mouse_left_down, x, y));
        frame(app);  // pressed visual
        app.input(touch_ev(input_type::mouse_left_up, x, y));
        frame(app);
    }
}

int main(int argc, char **argv)
{
    const char *out_path = argc > 1 ? argv[1] : "showcase.gif";

#if defined(IMCORE_USE_FONT_SIZE) && defined(IMCORE_HAS_TTF_SUBSET)
    // shell-less recorder: no shell install point, mirror it here so
    // USE_FONT_SIZE builds record the README asset with the platform font
    zb::ui::set_default_glyph_provider(zb::ui::ttf_subset_provider());
#endif

    Showcase app;
    app.create_window(320, 240);
    // bump the theme generation once: the first frame must present the
    // whole buffer (CanvasWindow forces a full-frame paint when the theme
    // generation moved); a shell-less host has no other full-invalidate
    // path, and the initial per-widget damage region is not the full page
    zb::ui::set_theme(zb::ui::light_theme());
    auto &win = *static_cast<zb::app::CanvasWindow *>(app.window().get());
    std::printf("gif_record: %ux%u buffer\n",
                static_cast<unsigned>(win.width()), static_cast<unsigned>(win.height()));

    g_gif = std::make_unique<GifWriter>(out_path, 320, 240, 5);

    // initial page, laid out by the first paint
    frame(app);


    // START: the three bars advance one step per input event while
    // running, so the fill animation is exactly N scripted events
    click(app, win, "start_btn");
    for (int i = 0; i < 100; ++i)
    {
        app.input(key_ev(zb::input::key_code::right));  // advances + feeds the window
        frame(app);
    }

    // dark theme, gallery page, drag the demo slider across, back, light
    click(app, win, "theme_btn");
    click(app, win, "gallery_btn");
    if (auto *slider = win.root().find_by_id("demo_slider"))
    {
        const auto p = slider->get_absolute_position();
        const auto s = slider->get_size();
        const int y = p.y + s.height / 2;
        app.input(touch_ev(input_type::mouse_left_down, p.x + 2, y));
        frame(app);
        for (int i = 1; i <= 8; ++i)
        {
            app.input(touch_ev(input_type::mouse_move,
                               p.x + 2 + (s.width - 4) * i / 8, y));
            frame(app);
        }
        app.input(touch_ev(input_type::mouse_left_up, p.x + 2 + (s.width - 4), y));
        frame(app);
    }
    click(app, win, "back_btn");
    click(app, win, "theme_btn");  // back to light for a tidy last frame

    const std::size_t n = g_gif->frames();
    g_gif->close();
    std::printf("gif_record: %zu frames -> %s\n", n, out_path);
    return 0;
}
