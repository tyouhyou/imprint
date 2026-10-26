#include "firefly.hpp"

#include <cmath>
#include <cstdio>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "firefly_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::firefly;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // night palette; dawn is the same world lerped warm -- the timer is
    // the sky itself, not a UI number
    constexpr unsigned char uc(const int v) { return static_cast<unsigned char>(v); }

    const core::Color kGlowHalo = core::Color::from(0x5a, 0x4c, 0x1e);
    const core::Color kGlowMid = core::Color::from(0xcb, 0xb4, 0x5c);
    const core::Color kGlowCore = core::Color::from(0xff, 0xf3, 0xa0);
    const core::Color kWeb = core::Color::from(0x96, 0xa2, 0xae);
    const core::Color kWindStreak = core::Color::from(0x23, 0x32, 0x46);
    const core::Color kLampHalo = core::Color::from(0x33, 0x29, 0x0f);
    const core::Color kLampMid = core::Color::from(0x57, 0x43, 0x1a);
    const core::Color kLampHot = core::Color::from(0xff, 0xb4, 0x54);
    const core::Color kLampCore = core::Color::from(0xff, 0xf3, 0xd0);
    const core::Color kLampDim = core::Color::from(0x3a, 0x33, 0x24);
    const core::Color kPop = core::Color::from(0xff, 0xdf, 0x9a);
    const core::Color kMoon = core::Color::from(0xd6, 0xde, 0xe8);

    core::Color lerp3(const int r0, const int g0, const int b0, const int r1,
                      const int g1, const int b1, const int t100)
    {
        return core::Color::from(uc(r0 + (r1 - r0) * t100 / 100),
                                 uc(g0 + (g1 - g0) * t100 / 100),
                                 uc(b0 + (b1 - b0) * t100 / 100));
    }

    // fixed arena furniture (virtual 400x250): the grove the swarm is
    // led home to, sticky spider webs, and the wind band
    constexpr int kGroveX = 330;
    constexpr int kGroveY = 185;
    constexpr int kGroveR = 22;
    constexpr int kHomeR = 22;
    constexpr int kBeamR = 80;
    constexpr int kFarR = 260;  // beyond the beam, a weak far glow still pulls

    struct webdef
    {
        int x, y, r;
    };
    const webdef kWebs[3] = {{120, 82, 16}, {215, 158, 14}, {282, 58, 15}};

    constexpr int kWindTop = 108;
    constexpr int kWindBot = 126;
}  // namespace

void GroveCanvas::set_geometry(const int ox, const int oy, const int scale,
                               const int font_px)
{
    ox_ = ox;
    oy_ = oy;
    s_ = scale;
    set_font_size(font_px);
    mark_dirty();
}

unsigned GroveCanvas::hash(const int a, const int b) const
{
    unsigned h = static_cast<unsigned>(a) * 73856093u ^
                 static_cast<unsigned>(b) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

void GroveCanvas::seed_swarm()
{
    for (int i = 0; i < kSwarm; ++i)
    {
        const unsigned h = hash(i, 999);
        fx_[i] = 55.f + static_cast<float>(h & 31u) - 16.f;
        fy_[i] = 70.f + static_cast<float>((h >> 5) & 31u) - 16.f;
        vx_[i] = 0.f;
        vy_[i] = 0.f;
        stuck_[i] = false;
    }
}

void GroveCanvas::start_night()
{
    frame_ = 0;
    homed_ = 0;
    done_ = false;
    lit_ = false;
    pop_head_ = 0;
    seed_swarm();
    mark_dirty();
}

void GroveCanvas::tick()
{
    if (!playing_ || done_)
    {
        return;
    }
    ++frame_;
    if (frame_ >= static_cast<unsigned>(kNightLen) || homed_ >= kSwarm)
    {
        done_ = true;
        return;
    }

    for (int i = 0; i < kSwarm; ++i)
    {
        if (fx_[i] < 0.f)
        {
            continue;  // already home
        }
        // wander: the swarm's idle life, hashed per agent per frame
        const unsigned h = hash(i, static_cast<int>(frame_));
        float ax = (static_cast<float>(h & 0xffu) / 255.f - 0.5f) * 0.10f;
        float ay = (static_cast<float>((h >> 8) & 0xffu) / 255.f - 0.5f) * 0.10f;

        // the lantern: strong pull inside the beam, and a weak far
        // glow across the field -- holding the light anywhere makes
        // the whole night drift toward you (the first-hold magic)
        if (lit_)
        {
            const float dx = lamp_x_ - fx_[i];
            const float dy = lamp_y_ - fy_[i];
            const float d2 = dx * dx + dy * dy;
            if (d2 < kBeamR * kBeamR)
            {
                const float inv = 1.f / (d2 + 64.f);
                ax += dx * inv * 110.f;
                ay += dy * inv * 110.f;
            }
            else if (d2 < kFarR * kFarR)
            {
                const float w = 0.018f / (std::sqrt(d2) + 1.f);
                ax += dx * w;
                ay += dy * w;
            }
        }

        // the wind band pushes everything east
        if (fy_[i] >= kWindTop && fy_[i] <= kWindBot)
        {
            ax += 0.10f;
        }

        vx_[i] = (vx_[i] + ax) * 0.965f;
        vy_[i] = (vy_[i] + ay) * 0.965f;

        // webs stall whoever flies in; only the lantern's pull drags
        // them back out (attraction outweighs the damping)
        for (const webdef &w : kWebs)
        {
            const float dx = fx_[i] - w.x;
            const float dy = fy_[i] - w.y;
            if (dx * dx + dy * dy < w.r * w.r)
            {
                vx_[i] *= 0.70f;
                vy_[i] *= 0.70f;
                stuck_[i] = true;
                break;
            }
        }

        // speed cap keeps the swarm readable
        const float sp2 = vx_[i] * vx_[i] + vy_[i] * vy_[i];
        if (sp2 > 2.25f)
        {
            const float k = 1.5f / std::sqrt(sp2);
            vx_[i] *= k;
            vy_[i] *= k;
        }

        fx_[i] += vx_[i];
        fy_[i] += vy_[i];
        if (fx_[i] < 2.f)
        {
            fx_[i] = 2.f;
            vx_[i] = -vx_[i];
        }
        if (fx_[i] > kVW - 2.f)
        {
            fx_[i] = static_cast<float>(kVW - 2);
            vx_[i] = -vx_[i];
        }
        if (fy_[i] < 2.f)
        {
            fy_[i] = 2.f;
            vy_[i] = -vy_[i];
        }
        if (fy_[i] > kVH - 2.f)
        {
            fy_[i] = static_cast<float>(kVH - 2);
            vy_[i] = -vy_[i];
        }

        // home: reaching the grove counts the firefly in
        const float gx = fx_[i] - kGroveX;
        const float gy = fy_[i] - kGroveY;
        if (gx * gx + gy * gy < kHomeR * kHomeR)
        {
            fx_[i] = -1.f;  // homed sentinel
            ++homed_;
            pops_[pop_head_] = {kGroveX, kGroveY - 30, static_cast<int>(frame_)};
            pop_head_ = (pop_head_ + 1) % 16;
        }
        else
        {
            // clear the stuck flag once free of every web
            stuck_[i] = false;
            for (const webdef &w : kWebs)
            {
                const float dx = fx_[i] - w.x;
                const float dy = fy_[i] - w.y;
                if (dx * dx + dy * dy < w.r * w.r)
                {
                    stuck_[i] = true;
                    break;
                }
            }
        }
    }
}

bool GroveCanvas::on_input(const zb::input::input_event &ev)
{
    const core::impoint_t p = get_position();
    const int vx = (ev.x - p.x - ox_) / s_;
    const int vy = (ev.y - p.y - oy_) / s_;
    switch (ev.type)
    {
    case zb::input::input_type::touch_down:
        lit_ = true;
        lamp_x_ = static_cast<float>(clamp_i(vx, 0, kVW - 1));
        lamp_y_ = static_cast<float>(clamp_i(vy, 0, kVH - 1));
        return true;
    case zb::input::input_type::touch_move:
        if (!lit_)
        {
            return false;
        }
        lamp_x_ = static_cast<float>(clamp_i(vx, 0, kVW - 1));
        lamp_y_ = static_cast<float>(clamp_i(vy, 0, kVH - 1));
        return true;
    case zb::input::input_type::touch_up:
        lit_ = false;
        return true;
    default:
        return false;
    }
}

void GroveCanvas::draw_at(core::Graphics &area) const
{
    const core::imsize_t sz = get_size();
    const int W = sz.width;
    const int H = sz.height;
    const int t100 = static_cast<int>(frame_ * 100u / static_cast<unsigned>(kNightLen));
    const auto X = [&](const int v)
    { return ox_ + v * s_; };
    const auto Y = [&](const int v)
    { return oy_ + v * s_; };

    // the sky: night navy lerping toward a warm dawn -- the countdown
    area.fill_gradient(0, 0, W - 1, H - 1,
                       lerp3(0x06, 0x0a, 0x18, 0x3a, 0x2c, 0x48, t100),
                       lerp3(0x0c, 0x16, 0x26, 0xb0, 0x6a, 0x4a, t100), false);

    // stars fade out as dawn comes
    for (int i = 0; i < 60; ++i)
    {
        const unsigned h = hash(i, 7);
        const int x = static_cast<int>(h % 400u);
        const int y = static_cast<int>((h >> 8) % 150u);
        const int b = static_cast<int>(((h >> 16) & 0xffu) * (100 - t100) / 100);
        if (b > 40)
        {
            area.fill_rect(X(x), Y(y), X(x), Y(y),
                           core::Color::from(uc(120 + b / 2), uc(130 + b / 2),
                                             uc(150 + b / 2)));
        }
    }

    // moon
    if (t100 < 70)
    {
        area.fill_circle(X(36), Y(34), 9 * s_,
                         lerp3(0xd6, 0xde, 0xe8, 0x3a, 0x2c, 0x48, t100));
    }

    // ground
    area.fill_rect(0, Y(200), W - 1, H - 1,
                   lerp3(0x0a, 0x0f, 0x14, 0x24, 0x1c, 0x22, t100));

    // far tree silhouettes
    for (int i = 0; i < 8; ++i)
    {
        const unsigned h = hash(i, 11);
        const int x = 10 + static_cast<int>(h % 380u);
        const int th = 40 + static_cast<int>((h >> 8) % 30u);
        const core::Color c = lerp3(0x0e, 0x15, 0x22, 0x3a, 0x2c, 0x34, t100);
        area.fill_rect(X(x), Y(200 - th), X(x) + 2 * s_ - 1, Y(200), c);
        area.draw_line(X(x), Y(200 - th + 6), X(x) - 4 * s_, Y(200 - th + 14), c);
        area.draw_line(X(x) + 2 * s_ - 1, Y(200 - th + 10), X(x) + 6 * s_, Y(200 - th + 20), c);
    }
    // near tree silhouettes, darker and thicker
    for (int i = 0; i < 5; ++i)
    {
        const unsigned h = hash(i, 23);
        const int x = 20 + static_cast<int>(h % 360u);
        const int th = 60 + static_cast<int>((h >> 8) % 40u);
        const core::Color c = lerp3(0x08, 0x10, 0x19, 0x1c, 0x14, 0x1c, t100);
        area.fill_rect(X(x), Y(200 - th), X(x) + 4 * s_ - 1, Y(200), c);
        area.draw_line(X(x), Y(200 - th + 10), X(x) - 7 * s_, Y(200 - th + 22), c);
        area.draw_line(X(x) + 4 * s_ - 1, Y(200 - th + 14), X(x) + 9 * s_, Y(200 - th + 28), c);
    }

    // the mother grove: a tall dark tree with a warm glowing heart
    area.fill_rect(X(kGroveX - 4), Y(95), X(kGroveX + 4), Y(200),
                   lerp3(0x2a, 0x1f, 0x33, 0x4a, 0x34, 0x40, t100));
    area.fill_circle(X(kGroveX - 14), Y(92), 16 * s_, lerp3(0x12, 0x23, 0x3a, 0x30, 0x2a, 0x3e, t100));
    area.fill_circle(X(kGroveX + 13), Y(98), 14 * s_, lerp3(0x12, 0x23, 0x3a, 0x30, 0x2a, 0x3e, t100));
    area.fill_circle(X(kGroveX), Y(84), 18 * s_, lerp3(0x16, 0x2a, 0x44, 0x34, 0x2e, 0x44, t100));
    // glow rings around the heart
    area.fill_circle(X(kGroveX), Y(118), 26 * s_, core::Color::from(0x2a, 0x21, 0x18));
    area.fill_circle(X(kGroveX), Y(118), 16 * s_, core::Color::from(0x3d, 0x30, 0x1c));
    area.fill_circle(X(kGroveX), Y(118), 8 * s_, core::Color::from(0x5a, 0x45, 0x26));
    area.fill_circle(X(kGroveX), Y(118), 3 * s_, kLampHot);
    draw_text_at(area, u"GROVE", 5, X(kGroveX - 10), Y(196), kWeb);

    // spider webs: outline + three fixed spokes
    for (const webdef &w : kWebs)
    {
        area.draw_circle(X(w.x), Y(w.y), w.r * s_, kWeb);
        int px = 0, py = 0;
        core::point_on_circle(X(w.x), Y(w.y), w.r * s_, 0, &px, &py);
        int qx = 0, qy = 0;
        core::point_on_circle(X(w.x), Y(w.y), w.r * s_, 180, &qx, &qy);
        area.draw_line(px, py, qx, qy, kWeb);
        core::point_on_circle(X(w.x), Y(w.y), w.r * s_, 90, &px, &py);
        core::point_on_circle(X(w.x), Y(w.y), w.r * s_, 270, &qx, &qy);
        area.draw_line(px, py, qx, qy, kWeb);
        core::point_on_circle(X(w.x), Y(w.y), w.r * s_, 45, &px, &py);
        core::point_on_circle(X(w.x), Y(w.y), w.r * s_, 225, &qx, &qy);
        area.draw_line(px, py, qx, qy, kWeb);
    }

    // wind band: drifting streaks make the current visible
    for (int k = 0; k < 10; ++k)
    {
        const unsigned h = hash(k, 3);
        const int x = static_cast<int>((h + frame_ * 2u) % 400u);
        const int y = kWindTop + static_cast<int>((h >> 8) % 14u);
        area.fill_rect(X(x), Y(y), X(x) + 5 * s_ - 1, Y(y), kWindStreak);
    }

    // homing sparkles
    for (int k = 0; k < 16; ++k)
    {
        const sparkle &sp = pops_[k];
        if (sp.t0 == 0)
        {
            continue;
        }
        const int age = static_cast<int>(frame_) - sp.t0;
        if (age < 0 || age > 22)
        {
            continue;
        }
        area.draw_circle(X(sp.x), Y(sp.y), (2 + age / 2) * s_, kPop);
    }

    // the swarm
    for (int i = 0; i < kSwarm; ++i)
    {
        if (fx_[i] < 0.f)
        {
            continue;
        }
        const unsigned h = hash(i, static_cast<int>(frame_ >> 3));
        const int flick = static_cast<int>(h & 3u);
        int wx = static_cast<int>(fx_[i]);
        int wy = static_cast<int>(fy_[i]);
        if (stuck_[i])
        {
            wy += static_cast<int>((h >> 4) & 1u) * 2 - 1;
        }
        area.fill_circle(X(wx), Y(wy), 2 * s_, kGlowHalo);
        if (flick >= 1)
        {
            area.fill_circle(X(wx), Y(wy), s_, kGlowMid);
        }
        if (flick >= 2)
        {
            area.fill_rect(X(wx), Y(wy), X(wx), Y(wy), kGlowCore);
        }
    }

    // the lantern
    if (lit_)
    {
        const int lx = static_cast<int>(lamp_x_);
        const int ly = static_cast<int>(lamp_y_);
        area.fill_circle(X(lx), Y(ly), 26 * s_, kLampHalo);
        area.fill_circle(X(lx), Y(ly), 14 * s_, kLampMid);
        area.fill_circle(X(lx), Y(ly), 6 * s_, kLampHot);
        area.fill_circle(X(lx), Y(ly), 2 * s_, kLampCore);
    }
    else
    {
        area.fill_circle(X(static_cast<int>(lamp_x_)), Y(static_cast<int>(lamp_y_)),
                         s_, kLampDim);
    }
}

void FireflyApp::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    // function-level static: the family must outlive every paint (the
    // showcase install_font shape)
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(firefly_font, firefly_font_len);
    zb::ui::set_font_family(family);
#endif
}

void FireflyApp::sync_hud()
{
    if (!grove_)
    {
        return;
    }
    if (grove_->homed() != last_homed_)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "FIREFLIES %d/%d", grove_->homed(),
                      GroveCanvas::kSwarm);
        homed_label_->set_text(buf);
        last_homed_ = grove_->homed();
    }
    night_bar_->set_value(grove_->dawn_pct());
    if (grove_->done() && !last_done_)
    {
        last_done_ = true;
        const int pct = grove_->homed() * 100 / GroveCanvas::kSwarm;
        const int stars = pct >= 90 ? 3 : (pct >= 70 ? 2 : (pct >= 40 ? 1 : 0));
        dawn_title_->set_text(grove_->homed() == GroveCanvas::kSwarm ? "ALL HOME"
                                                                     : "DAWN");
        char buf[48];
        std::snprintf(buf, sizeof(buf), "HOMED %d/%d   %c %c %c",
                      grove_->homed(), GroveCanvas::kSwarm,
                      stars >= 1 ? '*' : '-', stars >= 2 ? '*' : '-',
                      stars >= 3 ? '*' : '-');
        dawn_score_->set_text(buf);
        dawn_card_->set_visible(true);
    }
    if (!grove_->done() && last_done_)
    {
        last_done_ = false;
        dawn_card_->set_visible(false);
    }
}

void FireflyApp::show_intro(const bool on)
{
    intro_->set_visible(on);
}

void FireflyApp::create_window(const uint32_t max_client_width,
                               const uint32_t max_client_height, void *buffer)
{
    install_font();

    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    const int w = static_cast<int>(max_client_width);
    const int h = static_cast<int>(max_client_height);
    const int u = clamp_i(h / 240, 1, 6);
    const int hud_h = 32 * u;

    auto &root = window_->root();
    root.set_background_color(core::Color::from(0x06, 0x0a, 0x18));

    auto title = std::make_unique<Label>();
    title->set_text("FIREFLY GROVE");
    title->set_text_color(core::Color::from(0xff, 0xe9, 0x8a));
    title->set_bold(true);
    title->set_size(150 * u, 22 * u);
    title->set_position(10 * u, 6 * u);
    title->set_font_size(9 * u);
    root.add_child(std::move(title));

    auto homed = std::make_unique<Label>();
    homed->set_text("FIREFLIES 0/120");
    homed->set_text_color(core::Color::from(0xcf, 0xe6, 0xff));
    homed->set_size(120 * u, 20 * u);
    homed->set_position(w - 260 * u, 8 * u);
    homed->set_font_size(8 * u);
    homed_label_ = homed.get();
    root.add_child(std::move(homed));

    auto night_tag = std::make_unique<Label>();
    night_tag->set_text("NIGHT");
    night_tag->set_text_color(core::Color::from(0x9f, 0xb3, 0xc8));
    night_tag->set_size(40 * u, 16 * u);
    night_tag->set_position(w - 138 * u, 10 * u);
    night_tag->set_font_size(7 * u);
    root.add_child(std::move(night_tag));

    auto bar = std::make_unique<ProgressBar>();
    bar->set_range(0, 100);
    bar->set_value(0);
    bar->set_size(120 * u, 12 * u);
    bar->set_position(w - 96 * u, 12 * u);
    bar->set_track_color(core::Color::from(0x1a, 0x26, 0x38));
    bar->set_fill_color(core::Color::from(0xff, 0xb4, 0x54));
    night_bar_ = bar.get();
    root.add_child(std::move(bar));

    // the world fills everything under the HUD strip
    auto grove = std::make_unique<GroveCanvas>();
    const int ah = h - hud_h;
    const int s = clamp_i(w / GroveCanvas::kVW < ah / GroveCanvas::kVH
                              ? w / GroveCanvas::kVW
                              : ah / GroveCanvas::kVH,
                          1, 6);
    grove->set_size(w, ah);
    grove->set_position(0, hud_h);
    grove->set_geometry((w - GroveCanvas::kVW * s) / 2,
                        (ah - GroveCanvas::kVH * s) / 2, s,
                        clamp_i(5 * s, 8, 30));
    grove_ = grove.get();
    root.add_child(std::move(grove));

    /*
     * Intro card: title, the one verb, START. The dawn card reuses the
     * same geometry with the score and AGAIN.
     */
    const int card_w = 190 * u < w - 24 * u ? 190 * u : w - 24 * u;
    const int card_h = 96 * u;
    const int card_x = (w - card_w) / 2;
    const int card_y = hud_h + (ah - card_h) / 2;

    auto intro = std::make_unique<Panel>();
    intro->set_background_color(core::Color::from(0x0d, 0x15, 0x20));
    intro->set_size(card_w, card_h);
    intro->set_position(card_x, card_y);
    intro->set_corner_radius(8 * u);
    intro->set_border(2 * u, core::Color::from(0x2a, 0x3a, 0x4a));

    auto it = std::make_unique<Label>();
    it->set_text("FIREFLY GROVE");
    it->set_text_color(core::Color::from(0xff, 0xe9, 0x8a));
    it->set_bold(true);
    it->set_size(card_w, 26 * u);
    it->set_position(0, 12 * u);
    it->set_h_align(Widget::h_align::center);
    it->set_font_size(12 * u);
    intro->add_child(std::move(it));

    auto is1 = std::make_unique<Label>();
    is1->set_text("HOLD TO SHINE - THE SWARM FOLLOWS YOUR LIGHT");
    is1->set_text_color(core::Color::from(0x9f, 0xb3, 0xc8));
    is1->set_size(card_w, 14 * u);
    is1->set_position(0, 44 * u);
    is1->set_h_align(Widget::h_align::center);
    is1->set_font_size(7 * u);
    intro->add_child(std::move(is1));

    auto is2 = std::make_unique<Label>();
    is2->set_text("LEAD THEM TO THE GROVE BEFORE DAWN");
    is2->set_text_color(core::Color::from(0x9f, 0xb3, 0xc8));
    is2->set_size(card_w, 14 * u);
    is2->set_position(0, 60 * u);
    is2->set_h_align(Widget::h_align::center);
    is2->set_font_size(7 * u);
    intro->add_child(std::move(is2));

    auto start = std::make_unique<Button>();
    start->set_text("START");
    start->set_size(70 * u, 22 * u);
    start->set_position((card_w - 70 * u) / 2, card_h - 28 * u);
    start->set_font_size(9 * u);
    start->set_text_color(core::Color::from(0x24, 0x1b, 0x15));
    start->set_background_color(core::Color::from(0xff, 0xb4, 0x54));
    sub_start_ = start->clicked.subscribe([this]
                                          {
                                              grove_->start_night();
                                              grove_->set_playing(true);
                                              show_intro(false);
                                          });
    intro->add_child(std::move(start));
    intro_ = intro.get();
    root.add_child(std::move(intro));

    auto dawn = std::make_unique<Panel>();
    dawn->set_background_color(core::Color::from(0x0d, 0x15, 0x20));
    dawn->set_size(card_w, card_h);
    dawn->set_position(card_x, card_y);
    dawn->set_corner_radius(8 * u);
    dawn->set_border(2 * u, core::Color::from(0x2a, 0x3a, 0x4a));
    dawn->set_visible(false);

    auto dt = std::make_unique<Label>();
    dt->set_text("DAWN");
    dt->set_text_color(core::Color::from(0xff, 0xe9, 0x8a));
    dt->set_bold(true);
    dt->set_size(card_w, 26 * u);
    dt->set_position(0, 12 * u);
    dt->set_h_align(Widget::h_align::center);
    dt->set_font_size(12 * u);
    dawn_title_ = dt.get();
    dawn->add_child(std::move(dt));

    auto ds = std::make_unique<Label>();
    ds->set_text("HOMED 0/120   - - -");
    ds->set_text_color(core::Color::from(0xcf, 0xe6, 0xff));
    ds->set_size(card_w, 16 * u);
    ds->set_position(0, 44 * u);
    ds->set_h_align(Widget::h_align::center);
    ds->set_font_size(8 * u);
    dawn_score_ = ds.get();
    dawn->add_child(std::move(ds));

    auto again = std::make_unique<Button>();
    again->set_text("AGAIN");
    again->set_size(70 * u, 22 * u);
    again->set_position((card_w - 70 * u) / 2, card_h - 28 * u);
    again->set_font_size(9 * u);
    again->set_text_color(core::Color::from(0x24, 0x1b, 0x15));
    again->set_background_color(core::Color::from(0xff, 0xb4, 0x54));
    sub_again_ = again->clicked.subscribe([this]
                                          {
                                              grove_->start_night();
                                              grove_->set_playing(true);
                                          });
    dawn->add_child(std::move(again));
    dawn_card_ = dawn.get();
    root.add_child(std::move(dawn));
}
