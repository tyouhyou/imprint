#include "rotafarm.hpp"

#include <cmath>
#include <cstdio>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "rotafarm_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::rotafarm;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    constexpr unsigned char uc(const int v)
    {
        return static_cast<unsigned char>(v);
    }

    // clockface palette on dark wood
    const core::Color kWood = core::Color::from(0x14, 0x10, 0x0e);
    const core::Color kSoilA = core::Color::from(0x3a, 0x2c, 0x20);
    const core::Color kSoilB = core::Color::from(0x46, 0x35, 0x26);
    const core::Color kLitWarm = core::Color::from(0x8a, 0x6a, 0x3a);
    const core::Color kNightCool = core::Color::from(0x24, 0x26, 0x36);
    const core::Color kStem = core::Color::from(0x6a, 0x9a, 0x4a);
    const core::Color kLeaf = core::Color::from(0x8a, 0xc2, 0x5a);
    const core::Color kSun = core::Color::from(0xff, 0xd9, 0x8a);
    const core::Color kSunCore = core::Color::from(0xff, 0xef, 0xc0);
    const core::Color kRipe = core::Color::from(0xff, 0xb4, 0x54);
    const core::Color kLocust = core::Color::from(0x1c, 0x14, 0x10);
    const core::Color kLocustHi = core::Color::from(0x4a, 0x30, 0x20);
    const core::Color kDim = core::Color::from(0x9a, 0x86, 0x6e);

    core::Color mix(const core::Color &a, const core::Color &b, const int t100)
    {
        // channel mix through the 8-bit accessors, never the pixel word
        return core::Color::from(uc(a.r() + (b.r() - a.r()) * t100 / 100),
                                 uc(a.g() + (b.g() - a.g()) * t100 / 100),
                                 uc(a.b() + (b.b() - a.b()) * t100 / 100));
    }

    constexpr int kCx = 200;
    constexpr int kCy = 125;
}  // namespace

unsigned FarmCanvas::hash(const int a, const int b) const
{
    unsigned h = static_cast<unsigned>(a) * 73856093u ^
                 static_cast<unsigned>(b) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

int FarmCanvas::ang_dist(const int a, const int b) const
{
    int d = a - b;
    while (d < -180)
    {
        d += 360;
    }
    while (d > 180)
    {
        d -= 360;
    }
    return d < 0 ? -d : d;
}

void FarmCanvas::sector_poly(const int i, const int r0, const int r1,
                             int *pts) const
{
    // 6-point donut slice: arc approximated with one midpoint each side
    const int a0 = i * 30 - 15;
    const int a1 = i * 30 + 15;
    const int am = i * 30;
    int x = 0, y = 0;
    core::point_on_circle(kCx, kCy, r1, a0, &x, &y);
    pts[0] = x;
    pts[1] = y;
    core::point_on_circle(kCx, kCy, r1, am, &x, &y);
    pts[2] = x;
    pts[3] = y;
    core::point_on_circle(kCx, kCy, r1, a1, &x, &y);
    pts[4] = x;
    pts[5] = y;
    core::point_on_circle(kCx, kCy, r0, a1, &x, &y);
    pts[6] = x;
    pts[7] = y;
    core::point_on_circle(kCx, kCy, r0, am, &x, &y);
    pts[8] = x;
    pts[9] = y;
    core::point_on_circle(kCx, kCy, r0, a0, &x, &y);
    pts[10] = x;
    pts[11] = y;
}

void FarmCanvas::fill_sector(core::Graphics &area, const int i,
                             const core::Color &c) const
{
    int p[12];
    sector_poly(i, kRIn, kROut, p);
    // the polygon is in virtual space; the widget paints through the same
    // ox_/s_ transform every other primitive uses, so the ring lands where
    // the hit test (also virtual space) expects it
    for (int k = 0; k < 12; k += 2)
    {
        p[k] = ox_ + p[k] * s_;
        p[k + 1] = oy_ + p[k + 1] * s_;
    }
    // fan the 6-gon from vertex 0 (deterministic order)
    for (int k = 1; k < 5; ++k)
    {
        area.fill_triangle(p[0], p[1], p[k * 2], p[k * 2 + 1],
                           p[k * 2 + 2], p[k * 2 + 3], c);
    }
}

void FarmCanvas::set_geometry(const int ox, const int oy, const int scale,
                              const int font_px)
{
    ox_ = ox;
    oy_ = oy;
    s_ = scale;
    set_font_size(font_px);
    mark_dirty();
}

void FarmCanvas::reset()
{
    frame_ = 0;
    harvests_ = 0;
    for (int i = 0; i < kSectors; ++i)
    {
        stage_[i] = -1;
        light_[i] = 0;
        eaten_at_[i] = -1000;
    }
    mark_dirty();
}

void FarmCanvas::tick()
{
    ++frame_;
    const int sun = static_cast<int>(frame_ * 360u / kDayLen % 360u);
    const int locust = static_cast<int>((frame_ * 360u / 3600u + 180u) % 360u);
    for (int i = 0; i < kSectors; ++i)
    {
        const int center = i * 30;
        if (stage_[i] >= 0 && stage_[i] < 5 && ang_dist(center, sun) < 55)
        {
            ++light_[i];
            if (light_[i] >= kGrowLight)
            {
                light_[i] = 0;
                ++stage_[i];
            }
        }
        // the locust shade eats any crop past half growth on its pass
        if (stage_[i] >= 3 && ang_dist(center, locust) < 8 &&
            static_cast<int>(frame_) - eaten_at_[i] > 300)
        {
            stage_[i] = -1;
            light_[i] = 0;
            eaten_at_[i] = static_cast<int>(frame_);
        }
    }
}

bool FarmCanvas::on_input(const zb::input::input_event &ev)
{
    if (ev.type != zb::input::input_type::touch_down)
    {
        return false;
    }
    const core::impoint_t p = get_position();
    const float fs = static_cast<float>(s_);
    const float lx = static_cast<float>(ev.x - p.x - ox_) / fs;
    const float ly = static_cast<float>(ev.y - p.y - oy_) / fs;
    const float dx = lx - static_cast<float>(kCx);
    const float dy = ly - static_cast<float>(kCy);
    const float r = std::sqrt(dx * dx + dy * dy);
    if (r < kRIn - 4 || r > kROut + 6)
    {
        return false;
    }
    int deg = static_cast<int>(std::atan2(dy, dx) * 180.f / 3.14159265f);
    while (deg < 0)
    {
        deg += 360;
    }
    const int i = ((deg + 15) / 30) % kSectors;
    if (stage_[i] == 5)
    {
        ++harvests_;
        stage_[i] = -1;
        light_[i] = 0;
    }
    else if (stage_[i] == -1)
    {
        stage_[i] = 0;
        light_[i] = 0;
    }
    mark_dirty();
    return true;
}

void FarmCanvas::draw_at(core::Graphics &area) const
{
    const auto X = [&](const int v)
    { return ox_ + v * s_; };
    const auto Y = [&](const int v)
    { return oy_ + v * s_; };
    const int sun = static_cast<int>(frame_ * 360u / kDayLen % 360u);
    const int locust = static_cast<int>((frame_ * 360u / 3600u + 180u) % 360u);

    area.fill(kWood);

    // hour ticks around the field
    for (int i = 0; i < 12; ++i)
    {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        core::point_on_circle(kCx, kCy, kROut + 4, i * 30, &x0, &y0);
        core::point_on_circle(kCx, kCy, kROut + 10, i * 30, &x1, &y1);
        area.draw_line(X(x0), Y(y0), X(x1), Y(y1), kDim);
    }

    // sectors: soil warms inside the day arc, cools in the night half
    for (int i = 0; i < kSectors; ++i)
    {
        const core::Color base = (i % 2) ? kSoilB : kSoilA;
        const int d = ang_dist(i * 30, sun);
        const core::Color c = d < 55
                                ? mix(base, kLitWarm, 70 - d)
                                : mix(base, kNightCool, d < 120 ? 25 : 45);
        fill_sector(area, i, c);
    }

    // crops at each sector's plant spot
    for (int i = 0; i < kSectors; ++i)
    {
        if (stage_[i] < 0)
        {
            continue;
        }
        int x = 0, y = 0;
        core::point_on_circle(kCx, kCy, kRPlant, i * 30, &x, &y);
        const int st = stage_[i];
        if (st == 0)
        {
            area.fill_rect(X(x), Y(y), X(x), Y(y), kStem);
        }
        else
        {
            area.draw_line(X(x), Y(y + 3), X(x), Y(y - 2), kStem);
        }
        if (st >= 2)
        {
            area.draw_line(X(x), Y(y), X(x - 3), Y(y - 3), kLeaf);
            area.draw_line(X(x), Y(y - 1), X(x + 3), Y(y - 4), kLeaf);
        }
        if (st == 4)
        {
            area.fill_circle(X(x), Y(y - 5), s_, kLeaf);
        }
        if (st == 5)
        {
            // ripe: a warm flower with a ready pulse
            for (int q = 0; q < 5; ++q)
            {
                int px = 0, py = 0;
                core::point_on_circle(x, y - 5, 3, q * 72, &px, &py);
                area.fill_circle(X(px), Y(py), s_, kRipe);
            }
            const unsigned h = hash(i, static_cast<int>(frame_ >> 4));
            if ((h & 3u) < 3u)
            {
                area.draw_circle(X(x), Y(y - 3), 7 * s_, kRipe);
            }
        }
    }

    // night fireflies: dim dots on the dark half of the ring
    for (int i = 0; i < 8; ++i)
    {
        const unsigned h = hash(i, static_cast<int>(frame_ >> 3));
        const int a = static_cast<int>((h % 360u + 180u) % 360u);  // roughly opposite the sun
        if (ang_dist(a, sun) < 60)
        {
            continue;
        }
        int x = 0, y = 0;
        core::point_on_circle(kCx, kCy,
                              kRPlant + static_cast<int>((h >> 8) % 20u) - 10, a, &x, &y);
        area.fill_rect(X(x), Y(y), X(x), Y(y),
                       core::Color::from(0xd9, 0xc2, 0x5a));
    }

    // the locust shade: a dark cluster sweeping against the sun
    {
        int x = 0, y = 0;
        core::point_on_circle(kCx, kCy, kROut + 16, locust, &x, &y);
        for (int k = 0; k < 6; ++k)
        {
            const unsigned h = hash(k, static_cast<int>(frame_ >> 2));
            area.fill_circle(X(x + static_cast<int>(h % 9u) - 4),
                             Y(y + static_cast<int>((h >> 4) % 7u) - 3), s_,
                             k == 0 ? kLocustHi : kLocust);
        }
    }

    // the sun walking its orbit, rays trailing behind
    {
        int x = 0, y = 0;
        core::point_on_circle(kCx, kCy, kROut + 26, sun, &x, &y);
        for (int k = 0; k < 8; ++k)
        {
            int rx = 0, ry = 0;
            core::point_on_circle(x, y, 14, sun + k * 45, &rx, &ry);
            area.draw_line(X(x), Y(y), X(rx), Y(ry), kSun);
        }
        area.fill_circle(X(x), Y(y), 8 * s_, kSun);
        area.fill_circle(X(x), Y(y), 4 * s_, kSunCore);
    }

    char16_t tip[40];
    int n = 0;
    const char *msg = "TAP SECTORS TO PLANT - HARVEST WHEN GOLD";
    for (; msg[n] != 0 && n < 39; ++n)
    {
        tip[n] = static_cast<char16_t>(msg[n]);
    }
    draw_text_at(area, tip, n, X(58), Y(4), kDim);
}

void Rotafarm::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(rotafarm_font, rotafarm_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Rotafarm::sync_status()
{
    const int key = farm_->day() * 1000 + farm_->harvests();
    if (key != last_status_)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "DAY %d   HARVEST %d", farm_->day(),
                      farm_->harvests());
        status_->set_text(buf);
        last_status_ = key;
    }
}

void Rotafarm::create_window(const uint32_t max_client_width,
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
    root.set_background_color(core::Color::from(0x14, 0x10, 0x0e));

    auto title = std::make_unique<Label>();
    title->set_text("ROTAFARM");
    title->set_text_color(core::Color::from(0xff, 0xd9, 0x8a));
    title->set_bold(true);
    title->set_size(110 * u, 24 * u);
    title->set_position(10 * u, 6 * u);
    title->set_font_size(9 * u);
    root.add_child(std::move(title));

    auto stat = std::make_unique<Label>();
    stat->set_text("DAY 1   HARVEST 0");
    stat->set_text_color(core::Color::from(0xcf, 0xe6, 0xd0));
    stat->set_size(170 * u, 20 * u);
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
    reset->set_background_color(core::Color::from(0xff, 0xd9, 0x8a));
    sub_reset_ = reset->clicked.subscribe([this]
                                          { farm_->reset(); });
    root.add_child(std::move(reset));

    auto farm = std::make_unique<FarmCanvas>();
    const int ah = h - hud_h;
    const int s = clamp_i(w / FarmCanvas::kVW < ah / FarmCanvas::kVH
                              ? w / FarmCanvas::kVW
                              : ah / FarmCanvas::kVH,
                          1, 6);
    farm->set_size(w, ah);
    farm->set_position(0, hud_h);
    farm->set_geometry((w - FarmCanvas::kVW * s) / 2,
                       (ah - FarmCanvas::kVH * s) / 2, s, clamp_i(5 * s, 8, 30));
    farm_ = farm.get();
    root.add_child(std::move(farm));

    farm_->reset();
}
