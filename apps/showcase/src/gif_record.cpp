/*
 * S4 frame recorder, ORION NX-07 command-deck tour:
 * drives the showcase app through its public API (IApp::input +
 * CanvasWindow paint) and writes the frames as a GIF. Shell-less by
 * design -- CanvasWindow owns the framebuffer directly, the same
 * host-drives-everything contract the automation suite uses.
 * Deterministic: one step per input event, the tour is a fixed script,
 * the GIF is byte-identical across platforms.
 *
 * Usage: showcase_gif [out.gif] [--png out.png]   (800x600)
 * --png also dumps the FINAL frame as PNG (USE_PNG builds).
 */

#include "canvas_window.hpp"
#include "codec/gif.hpp"
#include "input.hpp"
#include "showcase.hpp"
#include "widget.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace
{
    // the ORION deck is a dense desktop-console design: it needs the
    // wide tier (the browser sizes the buffer from the viewport; the
    // 320x240 embedded tier clips the side columns by design)
    constexpr int kW = 800;
    constexpr int kH = 600;

    using namespace zb::app::showcase;
    using zb::input::input_event;
    using zb::input::input_type;

    // two-pass flow (P-2): the tour's frames are buffered, the optimal
    // median-cut palette comes from the whole set, then the writer maps
    // every pixel — smooth ramps and antialiased text survive, the
    // fixed 216-cube bands them
    std::vector<std::vector<zb::ui::core::Color>> g_frames;

    void frame(Showcase &app)
    {
        app.paint();
        const auto *pixels = static_cast<const zb::ui::core::Color *>(
            app.window()->data());
        g_frames.emplace_back(pixels, pixels + kW * kH);
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

    /* a real drag round-trip across `steps` positions; each position
     * captures a frame (the knob moves, the readouts follow) */
    void drag(Showcase &app, zb::app::CanvasWindow &win, const char *id,
              const int dx, const int dy, const int steps)
    {
        const auto [x, y] = center(win, id);
        app.input(touch_ev(input_type::mouse_left_down, x, y));
        frame(app);
        for (int i = 1; i <= steps; ++i)
        {
            app.input(touch_ev(input_type::mouse_move, x + dx * i / steps,
                               y + dy * i / steps));
            frame(app);
        }
        app.input(touch_ev(input_type::mouse_left_up, x + dx, y + dy));
        frame(app);
    }
}

int main(int argc, char **argv)
{
    const char *out_path = "showcase.gif";
    const char *png_path = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc)
        {
            png_path = argv[++i];
        }
        else
        {
            out_path = argv[i];
        }
    }

    Showcase app;
    app.create_window(kW, kH);
    auto &win = *static_cast<zb::app::CanvasWindow *>(app.window().get());
    std::printf("gif_record: %ux%u buffer\n",
                static_cast<unsigned>(win.width()), static_cast<unsigned>(win.height()));

    // 1: boot -- the sweep needle, mission clock and event stream step
    // on their own (pure in the frame counter)
    for (int i = 0; i < 24; ++i)
    {
        frame(app);
    }

    // 2: a real drag across the ENGINES slider -- velocity, core temp,
    // fuel and the propulsion gauges all follow (the sync() linkage)
    drag(app, win, "engine", 40, 0, 12);
    for (int i = 0; i < 8; ++i)
    {
        frame(app);
    }

    // 3: SHIELD SURGE pushes the shield bus +18; a YAW and a PITCH
    // nudge the helm (each toasts)
    click(app, win, "boost");
    frame(app);
    click(app, win, "mv_yaw_r");
    for (int i = 0; i < 8; ++i)
    {
        frame(app);
    }
    click(app, win, "mv_pitch_u");
    for (int i = 0; i < 10; ++i)
    {
        frame(app);
    }

    // 4: FAULT INJECT degrades the hull / core / segment labels
    click(app, win, "fault");
    for (int i = 0; i < 12; ++i)
    {
        frame(app);
    }
    click(app, win, "fault");
    frame(app);

    // 5: STAR MAP opens the galactic navigator modal; CLOSE dismisses
    click(app, win, "ship_map");
    for (int i = 0; i < 16; ++i)
    {
        frame(app);  // hold on the map
    }
    click(app, win, "close_btn");
    for (int i = 0; i < 6; ++i)
    {
        frame(app);
    }

    // 2: write with the optimal palette over the whole tour
    zb::ui::GifPaletteBuilder builder;
    for (const auto &f : g_frames)
    {
        builder.add_frame(f.data(), f.size());
    }
    const zb::ui::GifPalette pal = builder.palette(256);
    std::printf("gif_record: palette %zu colors\n", pal.count);

    zb::ui::GifWriter writer(out_path, kW, kH, 5, pal);
    for (const auto &f : g_frames)
    {
        writer.add_frame(f.data());
    }
    const std::size_t n = writer.frames();
    writer.close();
    std::printf("gif_record: %zu frames -> %s\n", n, out_path);

#if defined(USE_PNG)
    if (png_path != nullptr)
    {
        // final frame as PNG (USE_PNG builds): the capture path for the
        // README stills
        zb::ui::Image img;
        const int w = win.width();
        const int h = win.height();
        auto *pixels = static_cast<const zb::ui::core::Color *>(win.data());
        zb::ui::image_info info{static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                static_cast<uint32_t>(w) * 4u, 4u};
        std::size_t row = 0;
        img.write_png_file(png_path, info,
                           [&](std::vector<unsigned char> &out)
                           {
                               out.resize(static_cast<std::size_t>(w) * 4u);
                               for (int x = 0; x < w; ++x)
                               {
                                   const auto c = pixels[row * w + x];
                                   out[x * 4 + 0] = c.b();
                                   out[x * 4 + 1] = c.g();
                                   out[x * 4 + 2] = c.r();
                                   out[x * 4 + 3] = 255;
                               }
                               ++row;
                               return true;
                           });
        std::printf("gif_record: final frame -> %s\n", png_path);
    }
#endif
    return 0;
}
