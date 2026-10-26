#include "windloom.hpp"

#include <cstdio>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "windloom_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::windloom;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // dusk garden palette: deep green-blue sky into warm meadow
    const core::Color kSky1 = core::Color::from(0x16, 0x22, 0x2e);
    const core::Color kSky2 = core::Color::from(0x3c, 0x52, 0x38);
    const core::Color kSoil = core::Color::from(0x5a, 0x46, 0x32);
    const core::Color kSoilRim = core::Color::from(0x7a, 0x60, 0x46);
    const core::Color kWind = core::Color::from(0xd9, 0xc2, 0x5a);
    const core::Color kSeed = core::Color::from(0xf2, 0xe0, 0xa8);
    const core::Color kPetal = core::Color::from(0xe8, 0xa0, 0xb8);
    const core::Color kPetalIn = core::Color::from(0xff, 0xe9, 0x8a);
    const core::Color kStem = core::Color::from(0x6a, 0x9a, 0x4a);
    const core::Color kThorn = core::Color::from(0x4a, 0x2a, 0x2a);
    const core::Color kThornHi = core::Color::from(0x8a, 0x4a, 0x4a);
    const core::Color kGrass = core::Color::from(0x4e, 0x72, 0x42);
    const core::Color kDim = core::Color::from(0x9a, 0xb0, 0x92);

    constexpr int kThorns[2][3] = {{180, 178, 10}, {312, 118, 9}};
}  // namespace

unsigned WindCanvas::hash(const int a, const int b) const
{
    unsigned h = static_cast<unsigned>(a) * 73856093u ^
                 static_cast<unsigned>(b) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

void WindCanvas::set_geometry(const int ox, const int oy, const int scale,
                              const int font_px)
{
    ox_ = ox;
    oy_ = oy;
    s_ = scale;
    set_font_size(font_px);
    mark_dirty();
}

void WindCanvas::reset()
{
    for (int i = 0; i < 3; ++i)
    {
        pockets_[i].got = 0;
    }
    for (int i = 0; i < kStrokes; ++i)
    {
        sc_[i] = 0;
    }
    stroke_next_ = 0;
    drawing_ = false;
    for (int i = 0; i < kSeeds; ++i)
    {
        release_at_[i] = 30 + i * 80;
        landed_[i] = 0;
        px_[i] = 30.f;
        py_[i] = 26.f;
        vx_[i] = 0.f;
        vy_[i] = 0.f;
    }
    delivered_ = 0;
    frame_ = 0;
    mark_dirty();
}

void WindCanvas::wind_at(const float x, const float y, float &ax, float &ay) const
{
    // every segment of every stroke within reach pushes the seed along
    // the stroke tangent and slightly toward the line (a current, not
    // a wall)
    for (int st = 0; st < kStrokes; ++st)
    {
        for (int i = 1; i < sc_[st]; ++i)
        {
            const int ax0 = sx_[st][i - 1], ay0 = sy_[st][i - 1];
            const int bx = sx_[st][i], by = sy_[st][i];
            const float ex = static_cast<float>(bx - ax0);
            const float ey = static_cast<float>(by - ay0);
            const float len2 = ex * ex + ey * ey;
            if (len2 < 1.f)
            {
                continue;
            }
            const float fx0 = static_cast<float>(ax0);
            const float fy0 = static_cast<float>(ay0);
            const float dx = x - fx0, dy = y - fy0;
            const float t = (dx * ex + dy * ey) / len2;
            if (t < 0.f || t > 1.f)
            {
                continue;
            }
            const float cx = fx0 + ex * t, cy = fy0 + ey * t;
            const float nx = x - cx, ny = y - cy;
            const float d2 = nx * nx + ny * ny;
            if (d2 > 144.f)
            {
                continue;
            }
            // per-segment push: `ex * inv` is the unit tangent scaled by
            // 1/length, so the 0.22 gain keeps a full stroke's worth of
            // segments in reach at the same order as gravity (0.035) --
            // a current the player steers with, not a slingshot. The
            // earlier 9.f was ~39x gravity and threw seeds across the
            // whole field in one frame (terminal 175 px/frame).
            const float inv = 1.f / (len2 + 1.f);
            const float pull = 0.03f * (1.f - d2 / 144.f);
            ax += ex * inv * 0.22f + nx * pull;
            ay += ey * inv * 0.22f + ny * pull;
        }
    }
}

void WindCanvas::tick()
{
    ++frame_;
    pocket *p = pockets_;
    for (int i = 0; i < kSeeds; ++i)
    {
        if (landed_[i] != 0)
        {
            continue;
        }
        if (release_at_[i] > 0)
        {
            if (static_cast<int>(frame_) < release_at_[i])
            {
                continue;
            }
            release_at_[i] = 0;
            // launch wobble is hashed, not random
            const unsigned h = hash(i, 5);
            vx_[i] = 0.25f + static_cast<float>(h & 7u) * 0.05f;
            vy_[i] = 0.1f;
        }
        float ax = 0.f;
        float ay = 0.035f;  // gravity pulls the seed down into the soil
        const unsigned h = hash(i, static_cast<int>(frame_));
        ax += (static_cast<float>(h & 3u) - 1.5f) * 0.012f;  // drift wobble
        wind_at(px_[i], py_[i], ax, ay);

        vx_[i] = (vx_[i] + ax) * 0.992f;
        vy_[i] = (vy_[i] + ay) * 0.992f;
        px_[i] += vx_[i];
        py_[i] += vy_[i];

        if (px_[i] < 2.f)
        {
            px_[i] = 2.f;
            vx_[i] = -vx_[i] * 0.5f;
        }
        if (px_[i] > kVW - 2.f)
        {
            px_[i] = static_cast<float>(kVW - 2);
            vx_[i] = -vx_[i] * 0.5f;
        }

        // thorns kill on touch
        bool dead = false;
        for (const auto &t : kThorns)
        {
            const float tx = static_cast<float>(t[0]);
            const float ty = static_cast<float>(t[1]);
            const float tr = static_cast<float>(t[2]);
            const float dx = px_[i] - tx, dy = py_[i] - ty;
            if (dx * dx + dy * dy < tr * tr)
            {
                land_x_[i] = t[0];
                land_y_[i] = t[1];
                landed_[i] = 3;
                dead = true;
                break;
            }
        }
        if (dead)
        {
            continue;
        }

        // soil pockets: land when drifting down into the ellipse
        if (vy_[i] > 0.f)
        {
            for (int k = 0; k < 3; ++k)
            {
                const float pxc = static_cast<float>(p[k].x);
                const float pyc = static_cast<float>(p[k].y);
                const float prx = static_cast<float>(p[k].rx);
                const float pry = static_cast<float>(p[k].ry);
                const float dx = (px_[i] - pxc) / prx;
                const float dy = (py_[i] - pyc) / pry;
                if (dx * dx + dy * dy < 1.f && p[k].got < p[k].need)
                {
                    ++p[k].got;
                    ++delivered_;
                    land_x_[i] = static_cast<int>(px_[i]);
                    land_y_[i] = static_cast<int>(p[k].y);
                    landed_[i] = 10 + k;
                    break;
                }
            }
            if (landed_[i] != 0)
            {
                continue;
            }
        }

        // open ground: a grass tuft, pretty but not a delivery
        if (py_[i] > kVH - 6)
        {
            land_x_[i] = static_cast<int>(px_[i]);
            land_y_[i] = kVH - 8;
            landed_[i] = 2;
        }
    }
}

bool WindCanvas::on_input(const zb::input::input_event &ev)
{
    const core::impoint_t p = get_position();
    const int vx = clamp_i((ev.x - p.x - ox_) / s_, 0, kVW - 1);
    const int vy = clamp_i((ev.y - p.y - oy_) / s_, 0, kVH - 1);
    switch (ev.type)
    {
    case zb::input::input_type::touch_down:
        // a new stroke begins (the oldest of three rolls out)
        stroke_next_ = (stroke_next_ + 1) % kStrokes;
        sc_[stroke_next_] = 1;
        sx_[stroke_next_][0] = vx;
        sy_[stroke_next_][0] = vy;
        drawing_ = true;
        draw_last_x_ = vx;
        draw_last_y_ = vy;
        mark_dirty();
        return true;
    case zb::input::input_type::touch_move:
        if (!drawing_)
        {
            return false;
        }
        if (sc_[stroke_next_] < kPts &&
            (vx - draw_last_x_) * (vx - draw_last_x_) +
                    (vy - draw_last_y_) * (vy - draw_last_y_) >=
                16)
        {
            sx_[stroke_next_][sc_[stroke_next_]] = vx;
            sy_[stroke_next_][sc_[stroke_next_]] = vy;
            ++sc_[stroke_next_];
            draw_last_x_ = vx;
            draw_last_y_ = vy;
            mark_dirty();
        }
        return true;
    case zb::input::input_type::touch_up:
        drawing_ = false;
        return true;
    default:
        return false;
    }
}

void WindCanvas::draw_at(core::Graphics &area) const
{
    const core::imsize_t sz = get_size();
    const int W = sz.width;
    const int H = sz.height;
    const auto X = [&](const int v)
    { return ox_ + v * s_; };
    const auto Y = [&](const int v)
    { return oy_ + v * s_; };
    const pocket *p = pockets_;

    area.fill_gradient(0, 0, W - 1, H - 1, kSky1, kSky2, false);

    // faint stars in the upper dusk
    for (int i = 0; i < 30; ++i)
    {
        const unsigned h = hash(i, 3);
        area.fill_rect(X(static_cast<int>(h % 400u)), Y(static_cast<int>((h >> 8) % 120u)),
                       X(static_cast<int>(h % 400u)), Y(static_cast<int>((h >> 8) % 120u)),
                       core::Color::from(0x4a, 0x5c, 0x6a));
    }

    // the mother plant at the spout
    area.draw_line(X(30), Y(26), X(30), Y(10), kStem);
    area.draw_line(X(30), Y(20), X(24), Y(14), kStem);
    area.draw_line(X(30), Y(18), X(36), Y(12), kStem);
    area.fill_circle(X(30), Y(8), 3 * s_, kPetalIn);

    // wind strokes: flowing dashes (the dash phase is the frame)
    for (int st = 0; st < kStrokes; ++st)
    {
        for (int i = 1; i < sc_[st]; ++i)
        {
            const int ax0 = sx_[st][i - 1], ay0 = sy_[st][i - 1];
            const int bx = sx_[st][i], by = sy_[st][i];
            const unsigned h = hash(st * 31 + i, static_cast<int>(frame_));
            if ((h & 7u) < 5u)
            {
                area.draw_line(X(ax0), Y(ay0), X(bx), Y(by), kWind);
            }
        }
    }

    // soil pockets with need/got and the bloomed flowers
    char buf[24];
    for (int k = 0; k < 3; ++k)
    {
        area.fill_ellipse(X(p[k].x), Y(p[k].y), p[k].rx * s_, p[k].ry * s_, kSoil);
        area.draw_ellipse(X(p[k].x), Y(p[k].y), p[k].rx * s_, p[k].ry * s_, kSoilRim);
        std::snprintf(buf, sizeof(buf), "%d/%d", p[k].got, p[k].need);
        char16_t ub[12];
        int n = 0;
        for (; buf[n] != 0 && n < 11; ++n)
        {
            ub[n] = static_cast<char16_t>(buf[n]);
        }
        draw_text_at(area, ub, n, X(p[k].x + p[k].rx - 4), Y(p[k].y - 3), kSoilRim);
    }

    // thorn rosettes
    for (const auto &t : kThorns)
    {
        for (int k = 0; k < 8; ++k)
        {
            int px = 0, py = 0;
            core::point_on_circle(t[0], t[1], t[2], k * 45, &px, &py);
            area.draw_line(X(t[0]), Y(t[1]), X(px), Y(py), kThorn);
        }
        area.fill_circle(X(t[0]), Y(t[1]), 2 * s_, kThornHi);
    }

    // landed things first, flying seeds on top
    for (int i = 0; i < kSeeds; ++i)
    {
        if (landed_[i] == 2)
        {
            // grass tuft
            area.draw_line(X(land_x_[i]), Y(land_y_[i]), X(land_x_[i] - 2), Y(land_y_[i] - 5), kGrass);
            area.draw_line(X(land_x_[i]), Y(land_y_[i]), X(land_x_[i] + 2), Y(land_y_[i] - 6), kGrass);
        }
        else if (landed_[i] == 3)
        {
            // thorn kill mark
            area.draw_line(X(land_x_[i] - 3), Y(land_y_[i] - 3), X(land_x_[i] + 3), Y(land_y_[i] + 3), kThornHi);
            area.draw_line(X(land_x_[i] - 3), Y(land_y_[i] + 3), X(land_x_[i] + 3), Y(land_y_[i] - 3), kThornHi);
        }
        else if (landed_[i] >= 10)
        {
            // a flower in its pocket
            const int k = landed_[i] - 10;
            for (int q = 0; q < 5; ++q)
            {
                int px = 0, py = 0;
                core::point_on_circle(land_x_[i], land_y_[i] - 6, 4,
                                      q * 72 + static_cast<int>(hash(i, 1) % 30u), &px, &py);
                area.fill_circle(X(px), Y(py), 2 * s_, kPetal);
            }
            area.fill_circle(X(land_x_[i]), Y(land_y_[i] - 6), s_, kPetalIn);
            area.draw_line(X(land_x_[i]), Y(land_y_[i]), X(land_x_[i]), Y(land_y_[i] - 4), kStem);
            (void)k;
        }
    }
    for (int i = 0; i < kSeeds; ++i)
    {
        if (landed_[i] == 0 && release_at_[i] == 0)
        {
            const int x = static_cast<int>(px_[i]);
            const int y = static_cast<int>(py_[i]);
            // trail first, seed on top: the line includes both endpoints,
            // so drawing the seed before it left nothing but a dim pixel
            area.draw_line(X(x), Y(y), X(x - static_cast<int>(vx_[i] * 4)),
                           Y(y - static_cast<int>(vy_[i] * 4)), kDim);
            area.fill_circle(X(x), Y(y), s_ + s_ / 2, kSeed);
        }
    }

    if (delivered_ >= kGoal)
    {
        draw_text_at(area, u"GARDEN COMPLETE", 15, X(120), Y(40), kPetalIn);
    }
    else
    {
        draw_text_at(area, u"DRAW WIND - DELIVER SEEDS", 25, X(96), Y(6), kDim);
    }
}

void Windloom::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(windloom_font, windloom_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Windloom::sync_status()
{
    if (garden_->delivered() != last_status_)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "DELIVERED %d/%d", garden_->delivered(),
                      garden_->goal());
        status_->set_text(buf);
        last_status_ = garden_->delivered();
    }
}

void Windloom::create_window(const uint32_t max_client_width,
                             const uint32_t max_client_height, void *buffer)
{
    install_font();

    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    const int w = static_cast<int>(max_client_width);
    const int h = static_cast<int>(max_client_height);
    const int u = clamp_i(h / 240, 1, 6);
    const int hud_h = 34 * u;

    auto &root = window_->root();
    root.set_background_color(core::Color::from(0x16, 0x22, 0x2e));

    auto title = std::make_unique<Label>();
    title->set_text("WINDLOOM");
    title->set_text_color(core::Color::from(0xd9, 0xc2, 0x5a));
    title->set_bold(true);
    title->set_size(110 * u, 24 * u);
    title->set_position(10 * u, 6 * u);
    title->set_font_size(9 * u);
    root.add_child(std::move(title));

    auto stat = std::make_unique<Label>();
    stat->set_text("DELIVERED 0/11");
    stat->set_text_color(core::Color::from(0xcf, 0xe6, 0xd0));
    stat->set_size(130 * u, 20 * u);
    stat->set_position(130 * u, 8 * u);
    stat->set_font_size(8 * u);
    status_ = stat.get();
    root.add_child(std::move(stat));

    auto reset = std::make_unique<Button>();
    reset->set_text("RESET");
    reset->set_size(56 * u, 24 * u);
    reset->set_position(w - 68 * u, 6 * u);
    reset->set_font_size(7 * u);
    reset->set_text_color(core::Color::from(0x24, 0x1b, 0x15));
    reset->set_background_color(core::Color::from(0xd9, 0xc2, 0x5a));
    sub_reset_ = reset->clicked.subscribe([this]
                                          { garden_->reset(); });
    root.add_child(std::move(reset));

    auto garden = std::make_unique<WindCanvas>();
    const int ah = h - hud_h;
    const int s = clamp_i(w / WindCanvas::kVW < ah / WindCanvas::kVH
                              ? w / WindCanvas::kVW
                              : ah / WindCanvas::kVH,
                          1, 6);
    garden->set_size(w, ah);
    garden->set_position(0, hud_h);
    garden->set_geometry((w - WindCanvas::kVW * s) / 2,
                         (ah - WindCanvas::kVH * s) / 2, s, clamp_i(5 * s, 8, 30));
    garden_ = garden.get();
    root.add_child(std::move(garden));

    garden_->reset();
}
