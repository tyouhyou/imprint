/*
 * S4 frame recorder: drives the showcase app through its public API
 * (IApp::input + CanvasWindow paint) and writes the frames as a GIF.
 * Shell-less by design -- CanvasWindow owns the framebuffer directly,
 * the same host-drives-everything contract the automation suite uses.
 *
 * Usage: showcase_gif [out.gif]   (default showcase.gif, 320x240)
 */

#include "canvas_window.hpp"
#include "codec/gif.hpp"
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

    std::unique_ptr<zb::ui::GifWriter> g_gif;

    /* one captured frame = one paint of the current app state */
    void frame(Showcase &app)
    {
        app.paint();
        g_gif->add_frame(static_cast<const zb::ui::core::Color *>(app.window()->data()));
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
    // V-2: the showcase boots dark and the recorder keeps that -- the
    // old light-start override would desync the theme button's flag.
    // The first frame is a full one anyway (CanvasWindow starts owing a
    // repaint)
    auto &win = *static_cast<zb::app::CanvasWindow *>(app.window().get());
    std::printf("gif_record: %ux%u buffer\n",
                static_cast<unsigned>(win.width()), static_cast<unsigned>(win.height()));

    g_gif = std::make_unique<zb::ui::GifWriter>(out_path, 320, 240, 5);

    // 1: dark boot. The chart reveal auto-starts and advances one step
    // per captured frame, so the opening is the animation itself
    frame(app);
    for (int i = 0; i < 36; ++i)
    {
        frame(app);
    }

    // 2: START fills the bars (one deterministic step per input event);
    // a full run is 100 events, capture every other one so the closing
    // frames show the DONE state
    click(app, win, "start_btn");
    for (int i = 0; i < 100; ++i)
    {
        app.input(key_ev(zb::input::key_code::right));  // advances + feeds the window
        if (i % 2 == 0)
        {
            frame(app);
        }
    }

    // 3: REPLAY re-runs the chart reveal over the finished bars
    click(app, win, "replay_btn");
    for (int i = 0; i < 36; ++i)
    {
        frame(app);
    }

    // 4: light hero, then the gallery with its asset row
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

    // 5: back to the hero, dark again -- the closing money shot
    click(app, win, "back_btn");
    click(app, win, "theme_btn");
    frame(app);

    const std::size_t n = g_gif->frames();
    g_gif->close();
    std::printf("gif_record: %zu frames -> %s\n", n, out_path);
    return 0;
}
