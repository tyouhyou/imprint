#include "comet.hpp"

#include <cmath>
#include <cstdio>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "comet_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::comet;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // neon deep-space palette
    const core::Color kSky1 = core::Color::from(0x06, 0x08, 0x18);
    const core::Color kSky2 = core::Color::from(0x10, 0x18, 0x34);
    const core::Color kStar = core::Color::from(0x8f, 0xa8, 0xd0);
    const core::Color kPlanetA = core::Color::from(0x2a, 0x5a, 0x7a);
    const core::Color kPlanetB = core::Color::from(0x1d, 0x40, 0x5e);
    const core::Color kPlanetHi = core::Color::from(0x7f, 0xc8, 0xe8);
    const core::Color kGate = core::Color::from(0x6a, 0x76, 0x8e);
    const core::Color kGateOn = core::Color::from(0xff, 0xb4, 0x54);
    const core::Color kDepot = core::Color::from(0x57, 0xe3, 0x89);
    const core::Color kCometHot = core::Color::from(0xff, 0xf3, 0xd0);
    const core::Color kComet = core::Color::from(0xff, 0xb4, 0x54);
    const core::Color kAim = core::Color::from(0x6e, 0xe7, 0xff);
    const core::Color kAimDim = core::Color::from(0x2e, 0x5a, 0x70);
    const core::Color kDim = core::Color::from(0x9a, 0xa8, 0xc0);

    constexpr int kMaxFlight = 2400;  // stranded-orbit fuse
}  // namespace

unsigned SkyCanvas::hash(const int a, const int b) const
{
    unsigned h = static_cast<unsigned>(a) * 73856093u ^
                 static_cast<unsigned>(b) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

int SkyCanvas::gates_passed() const
{
    int n = 0;
    for (int i = 0; i < kGates; ++i)
    {
        if (gate_[i])
        {
            ++n;
        }
    }
    return n;
}

void SkyCanvas::gravity(const float x, const float y, float &ax, float &ay) const
{
    for (int i = 0; i < 2; ++i)
    {
        const float dx = static_cast<float>(kWellX[i]) - x;
        const float dy = static_cast<float>(kWellY[i]) - y;
        float d2 = dx * dx + dy * dy;
        if (d2 < 100.f)
        {
            d2 = 100.f;
        }
        float a = static_cast<float>(kWellM[i]) / d2;
        if (a > 0.5f)
        {
            a = 0.5f;
        }
        const float d = 0.001f + std::sqrt(d2);
        ax += dx / d * a;
        ay += dy / d * a;
    }
}

void SkyCanvas::set_geometry(const int ox, const int oy, const int scale,
                             const int font_px)
{
    ox_ = ox;
    oy_ = oy;
    s_ = scale;
    set_font_size(font_px);
    mark_dirty();
}

void SkyCanvas::reset()
{
    frame_ = 0;
    attempts_ = 1;
    cleared_ = false;
    flying_ = false;
    flight_ = 0;
    px_ = static_cast<float>(kDepotX);
    py_ = static_cast<float>(kDepotY);
    vx_ = 0.f;
    vy_ = 0.f;
    for (int i = 0; i < kGates; ++i)
    {
        gate_[i] = false;
    }
    trail_n_ = 0;
    aiming_ = false;
    mark_dirty();
}

void SkyCanvas::relaunch()
{
    flying_ = false;
    flight_ = 0;
    px_ = static_cast<float>(kDepotX);
    py_ = static_cast<float>(kDepotY);
    vx_ = 0.f;
    vy_ = 0.f;
    ++attempts_;
    trail_n_ = 0;
    mark_dirty();
}

void SkyCanvas::tick()
{
    ++frame_;
    if (!flying_ || cleared_)
    {
        return;
    }
    ++flight_;

    float ax = 0.f, ay = 0.f;
    gravity(px_, py_, ax, ay);
    vx_ += ax;
    vy_ += ay;
    px_ += vx_;
    py_ += vy_;

    trail_x_[trail_n_ % kTrail] = static_cast<int>(px_);
    trail_y_[trail_n_ % kTrail] = static_cast<int>(py_);
    ++trail_n_;

    // crash into a well, drift off the chart or strand in orbit: retry
    for (int i = 0; i < 2; ++i)
    {
        const float dx = px_ - static_cast<float>(kWellX[i]);
        const float dy = py_ - static_cast<float>(kWellY[i]);
        if (dx * dx + dy * dy < 144.f)
        {
            relaunch();
            return;
        }
    }
    if (px_ < -30.f || px_ > kVW + 30.f || py_ < -30.f || py_ > kVH + 30.f ||
        flight_ > kMaxFlight)
    {
        relaunch();
        return;
    }

    // gates thread in any order and stay lit across attempts
    for (int i = 0; i < kGates; ++i)
    {
        if (gate_[i])
        {
            continue;
        }
        const float dx = px_ - static_cast<float>(kGateX[i]);
        const float dy = py_ - static_cast<float>(kGateY[i]);
        if (dx * dx + dy * dy < 64.f)
        {
            gate_[i] = true;
            mark_dirty();
        }
    }

    // all gates lit: swing back into the depot
    if (gates_passed() == kGates)
    {
        const float dx = px_ - kDepotX, dy = py_ - kDepotY;
        if (dx * dx + dy * dy < 100.f)
        {
            cleared_ = true;
            flying_ = false;
            mark_dirty();
        }
    }
}

bool SkyCanvas::on_input(const zb::input::input_event &ev)
{
    const core::impoint_t p = get_position();
    const int vx = clamp_i((ev.x - p.x - ox_) / s_, -40, kVW + 40);
    const int vy = clamp_i((ev.y - p.y - oy_) / s_, -40, kVH + 40);
    switch (ev.type)
    {
    case zb::input::input_type::touch_down:
        if (cleared_ || flying_)
        {
            return false;
        }
        aiming_ = true;
        aim_x0_ = vx;
        aim_y0_ = vy;
        aim_x1_ = vx;
        aim_y1_ = vy;
        mark_dirty();
        return true;
    case zb::input::input_type::touch_move:
        if (!aiming_)
        {
            return false;
        }
        aim_x1_ = vx;
        aim_y1_ = vy;
        mark_dirty();
        return true;
    case zb::input::input_type::touch_up:
        if (!aiming_)
        {
            return false;
        }
        aiming_ = false;
        // slingshot: the launch vector points opposite the pull
        {
            float lx = static_cast<float>(aim_x0_ - aim_x1_);
            float ly = static_cast<float>(aim_y0_ - aim_y1_);
            const float len = (lx * lx + ly * ly) * 0.0036f;  // 0.06^2
            if (len > 0.05f)
            {
                const float mag = 0.06f * std::sqrt(lx * lx + ly * ly);
                const float cap = mag > 3.2f ? 3.2f / mag : 1.f;
                vx_ = lx * 0.06f * cap;
                vy_ = ly * 0.06f * cap;
                flying_ = true;
                flight_ = 0;
                trail_n_ = 0;
            }
        }
        mark_dirty();
        return true;
    default:
        return false;
    }
}

void SkyCanvas::draw_at(core::Graphics &area) const
{
    const core::imsize_t sz = get_size();
    const int W = sz.width;
    const int H = sz.height;
    const auto X = [&](const int v)
    { return ox_ + v * s_; };
    const auto Y = [&](const int v)
    { return oy_ + v * s_; };

    area.fill_gradient(0, 0, W - 1, H - 1, kSky1, kSky2, false);
    for (int i = 0; i < 70; ++i)
    {
        const unsigned h = hash(i, 9);
        area.fill_rect(X(static_cast<int>(h % 400u)), Y(static_cast<int>((h >> 8) % 250u)),
                       X(static_cast<int>(h % 400u)), Y(static_cast<int>((h >> 8) % 250u)),
                       kStar);
    }

    // gravity wells as banded planets
    for (int i = 0; i < 2; ++i)
    {
        const int r = 11 + i * 3;
        for (int dy = -r; dy <= r; ++dy)
        {
            // corner_chord measures |dy| from the arc centre and reads the
            // full radius for dy <= 0 (the rounded-rect contract), so the
            // disc row needs the magnitude spelled out -- passing the raw
            // signed dy painted the upper half as a square
            const int half = core::Graphics::corner_chord(r, dy < 0 ? -dy : dy);
            if (half <= 0)
            {
                continue;
            }
            const int band = (dy + r) * 4 / (2 * r + 1);
            area.fill_rect(X(kWellX[i] - half), Y(kWellY[i] + dy),
                           X(kWellX[i] + half) - 1, Y(kWellY[i] + dy) + s_ - 1,
                           (band & 1) ? kPlanetA : kPlanetB);
        }
        area.draw_circle(X(kWellX[i]), Y(kWellY[i]), r * s_, kPlanetHi);
    }

    // gate rings: dashed circles, lit amber once threaded
    for (int i = 0; i < kGates; ++i)
    {
        const core::Color &c = gate_[i] ? kGateOn : kGate;
        for (int k = 0; k < 8; ++k)
        {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            core::point_on_circle(kGateX[i], kGateY[i], 8, k * 45 + 6, &x0, &y0);
            core::point_on_circle(kGateX[i], kGateY[i], 8, k * 45 + 34, &x1, &y1);
            area.draw_line(X(x0), Y(y0), X(x1), Y(y1), c);
        }
        if (gate_[i])
        {
            area.fill_circle(X(kGateX[i]), Y(kGateY[i]), s_, kGateOn);
        }
    }

    // the depot ring
    area.draw_circle(X(kDepotX), Y(kDepotY), 10 * s_, kDepot);
    area.draw_circle(X(kDepotX), Y(kDepotY), 6 * s_, kDepot);

    // trail (older dots dimmer by ring order)
    if (trail_n_ > 0)
    {
        for (int k = 0; k < kTrail && k < trail_n_; ++k)
        {
            const int idx = (trail_n_ - 1 - k) % kTrail;
            const int age = k;
            if (age % 2)
            {
                continue;
            }
            area.fill_rect(X(trail_x_[idx]), Y(trail_y_[idx]),
                           X(trail_x_[idx]), Y(trail_y_[idx]),
                           age < 20 ? kComet : kAimDim);
        }
    }

    // the comet
    area.fill_circle(X(static_cast<int>(px_)), Y(static_cast<int>(py_)), 3 * s_, kComet);
    area.fill_circle(X(static_cast<int>(px_)), Y(static_cast<int>(py_)), s_, kCometHot);

    // aiming: pull line, launch arrow and a 90-step predicted path
    if (aiming_)
    {
        area.draw_line(X(aim_x1_), Y(aim_y1_),
                       X(static_cast<int>(px_)), Y(static_cast<int>(py_)), kAimDim);
        float lx = static_cast<float>(aim_x0_ - aim_x1_);
        float ly = static_cast<float>(aim_y0_ - aim_y1_);
        float sx = px_, sy = py_, svx = lx * 0.06f, svy = ly * 0.06f;
        const float mag = std::sqrt(svx * svx + svy * svy);
        if (mag > 3.2f)
        {
            svx *= 3.2f / mag;
            svy *= 3.2f / mag;
        }
        for (int step = 0; step < 90; ++step)
        {
            float ax = 0.f, ay = 0.f;
            gravity(sx, sy, ax, ay);
            svx += ax;
            svy += ay;
            sx += svx;
            sy += svy;
            if (step % 8 == 0)
            {
                area.fill_rect(X(static_cast<int>(sx)), Y(static_cast<int>(sy)),
                               X(static_cast<int>(sx)), Y(static_cast<int>(sy)), kAim);
            }
        }
    }

    char16_t tip[48];
    int n = 0;
    const char *msg = cleared_
                        ? "ROUTE CLEAR"
                        : (flying_ ? "FLYING" : "DRAG ANYWHERE - RELEASE TO LAUNCH");
    for (; msg[n] != 0 && n < 47; ++n)
    {
        tip[n] = static_cast<char16_t>(msg[n]);
    }
    draw_text_at(area, tip, n, X(10), Y(4), cleared_ ? kDepot : kDim);
}

void Comet::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(comet_font, comet_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Comet::sync_status()
{
    const int key = sky_->attempts() * 100 + sky_->gates_passed();
    if (key != last_status_)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "ATTEMPT %d   GATES %d/3", sky_->attempts(),
                      sky_->gates_passed());
        status_->set_text(buf);
        last_status_ = key;
    }
}

void Comet::create_window(const uint32_t max_client_width,
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
    root.set_background_color(core::Color::from(0x06, 0x08, 0x18));

    auto title = std::make_unique<Label>();
    title->set_text("COMET COURIER");
    title->set_text_color(core::Color::from(0x6e, 0xe7, 0xff));
    title->set_bold(true);
    title->set_size(160 * u, 24 * u);
    title->set_position(10 * u, 6 * u);
    title->set_font_size(9 * u);
    root.add_child(std::move(title));

    auto stat = std::make_unique<Label>();
    stat->set_text("ATTEMPT 1   GATES 0/3");
    stat->set_text_color(core::Color::from(0xcf, 0xe6, 0xff));
    stat->set_size(180 * u, 20 * u);
    stat->set_position(180 * u, 8 * u);
    stat->set_font_size(8 * u);
    status_ = stat.get();
    root.add_child(std::move(stat));

    auto reset = std::make_unique<Button>();
    reset->set_text("RESET");
    reset->set_size(56 * u, 24 * u);
    reset->set_position(w - 68 * u, 6 * u);
    reset->set_font_size(7 * u);
    reset->set_text_color(core::Color::from(0x24, 0x1b, 0x15));
    reset->set_background_color(core::Color::from(0x6e, 0xe7, 0xff));
    sub_reset_ = reset->clicked.subscribe([this]
                                          { sky_->reset(); });
    root.add_child(std::move(reset));

    auto sky = std::make_unique<SkyCanvas>();
    const int ah = h - hud_h;
    const int s = clamp_i(w / SkyCanvas::kVW < ah / SkyCanvas::kVH
                              ? w / SkyCanvas::kVW
                              : ah / SkyCanvas::kVH,
                          1, 6);
    sky->set_size(w, ah);
    sky->set_position(0, hud_h);
    sky->set_geometry((w - SkyCanvas::kVW * s) / 2,
                      (ah - SkyCanvas::kVH * s) / 2, s, clamp_i(5 * s, 8, 30));
    sky_ = sky.get();
    root.add_child(std::move(sky));
}
