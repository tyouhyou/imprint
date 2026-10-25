#include "drift.hpp"

#include <cstdio>
#include <cstring>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "drift_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::drift;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    constexpr unsigned char uc(const int v) { return static_cast<unsigned char>(v); }

    // feed palette: deep-space navy, monitor cyan accents, warm lamps
    const core::Color kSky1 = core::Color::from(0x06, 0x0a, 0x14);
    const core::Color kSky2 = core::Color::from(0x0b, 0x16, 0x26);
    const core::Color kStarDim = core::Color::from(0x5a, 0x70, 0x8a);
    const core::Color kStarDust = core::Color::from(0x33, 0x50, 0x6a);
    const core::Color kNorth = core::Color::from(0xea, 0xf6, 0xff);
    const core::Color kRing = core::Color::from(0x9f, 0xd8, 0xe8);
    const core::Color kMoon = core::Color::from(0xcb, 0xd5, 0xdd);
    const core::Color kMoon2 = core::Color::from(0xb7, 0xc4, 0xcf);
    const core::Color kSat = core::Color::from(0x6e, 0xe7, 0xff);
    const core::Color kHull = core::Color::from(0x2c, 0x3a, 0x46);
    const core::Color kHullBorder = core::Color::from(0x46, 0x58, 0x6a);
    const core::Color kWindow = core::Color::from(0xff, 0xd9, 0x8a);
    const core::Color kWindowOff = core::Color::from(0x10, 0x16, 0x1c);
    const core::Color kAntennaOn = core::Color::from(0xff, 0x6b, 0x5e);
    const core::Color kAntennaDim = core::Color::from(0x4a, 0x1f, 0x1a);
    const core::Color kMast = core::Color::from(0x46, 0x58, 0x6a);
    const core::Color kDock = core::Color::from(0xff, 0xb4, 0x54);
    const core::Color kDockDim = core::Color::from(0x6b, 0x4a, 0x20);
    const core::Color kDockRed = core::Color::from(0xff, 0x5f, 0x56);
    const core::Color kDockRedDim = core::Color::from(0x66, 0x24, 0x1f);
    const core::Color kShuttle = core::Color::from(0xd7, 0xdd, 0xe3);
    const core::Color kFlame = core::Color::from(0xff, 0x9a, 0x3d);
    const core::Color kData = core::Color::from(0xbf, 0xe8, 0xf2);
    const core::Color kHud = core::Color::from(0x8f, 0xb3, 0xc4);
    const core::Color kScan = core::Color::from(0x12, 0x30, 0x3a);
    const core::Color kBorder = core::Color::from(0x14, 0x30, 0x3c);
    const core::Color kGlitchA = core::Color::from(0x0a, 0x1a, 0x22);
    const core::Color kGlitchB = core::Color::from(0x1e, 0x3a, 0x46);
    const core::Color kGlitchSpeck = core::Color::from(0xd7, 0xf4, 0xff);
    const core::Color kAccent = core::Color::from(0xff, 0x4f, 0xd8);
    const core::Color kFound = core::Color::from(0x57, 0xe3, 0x89);
    const core::Color kMiss = core::Color::from(0xff, 0x5f, 0x56);
    const core::Color kTimer1 = core::Color::from(0x6e, 0xe7, 0xff);
    const core::Color kTimer2 = core::Color::from(0xff, 0xb4, 0x54);
    const core::Color kTimer3 = core::Color::from(0xff, 0x5f, 0x56);
    const core::Color kTimerTrack = core::Color::from(0x10, 0x22, 0x2c);

    // planet band ramps: teal (live feed) and the drifted amber variant
    const core::Color kBand[9] = {
        core::Color::from(0x0e, 0x34, 0x40), core::Color::from(0x14, 0x41, 0x4f),
        core::Color::from(0x1a, 0x4e, 0x5e), core::Color::from(0x20, 0x5b, 0x6d),
        core::Color::from(0x26, 0x68, 0x7b), core::Color::from(0x2c, 0x75, 0x88),
        core::Color::from(0x22, 0x5f, 0x71), core::Color::from(0x1a, 0x4e, 0x5e),
        core::Color::from(0x12, 0x3d, 0x4a)};
    const core::Color kBandAlt[9] = {
        core::Color::from(0x45, 0x2d, 0x11), core::Color::from(0x53, 0x37, 0x16),
        core::Color::from(0x61, 0x41, 0x1b), core::Color::from(0x6f, 0x4b, 0x20),
        core::Color::from(0x7d, 0x55, 0x25), core::Color::from(0x8b, 0x5f, 0x2a),
        core::Color::from(0x71, 0x4a, 0x21), core::Color::from(0x61, 0x41, 0x1b),
        core::Color::from(0x51, 0x38, 0x16)};
    const core::Color kBandDark[9] = {
        core::Color::from(0x07, 0x1a, 0x20), core::Color::from(0x0a, 0x20, 0x27),
        core::Color::from(0x0d, 0x27, 0x2f), core::Color::from(0x10, 0x2d, 0x36),
        core::Color::from(0x13, 0x34, 0x3d), core::Color::from(0x16, 0x3a, 0x44),
        core::Color::from(0x11, 0x2f, 0x38), core::Color::from(0x0d, 0x27, 0x2f),
        core::Color::from(0x09, 0x1e, 0x25)};

    // hit region per change kind, in virtual scene units
    struct vrect
    {
        int x, y, w, h;
    };
    const vrect kRegion[10] = {
        {30, 12, 14, 12},   // 0: the north star loses its sparkle
        {44, 16, 20, 24},   // 1: antenna starts blinking fast
        {208, 60, 18, 14},  // 2: a second moon shows up
        {150, 68, 90, 82},  // 3: the planet grows a ring
        {150, 68, 90, 82},  // 4: the planet's bands shift hue
        {0, 10, 240, 12},   // 5: the shuttle doubles its speed
        {4, 120, 124, 10},  // 6: data row 0 reads different hex
        {34, 56, 42, 10},   // 7: dock lights go red
        {36, 38, 38, 18},   // 8: the station's windows reshuffle
        {138, 18, 16, 14},  // 9: an extra star appears
    };

    // glitch moments inside the watch phase (one change each)
    constexpr int kGlitchAt[SceneWidget::kChanges] = {260, 760, 1260};

    unsigned mix(unsigned s)
    {
        s ^= s >> 15;
        s *= 0x2c1b3c6du;
        s ^= s >> 12;
        s *= 0x297a2d39u;
        s ^= s >> 15;
        return s;
    }

    unsigned fnv_bytes(const unsigned char *p, const size_t n)
    {
        unsigned h = 2166136261u;
        for (size_t i = 0; i < n; ++i)
        {
            h = (h ^ p[i]) * 16777619u;
        }
        return h;
    }
}  // namespace

void SceneWidget::set_geometry(const int ox, const int oy, const int scale,
                               const int font_px)
{
    ox_ = ox;
    oy_ = oy;
    s_ = scale;
    set_font_size(font_px);
    mark_dirty();
}

void SceneWidget::new_round()
{
    rframe_ = 0;
    glitch_left_ = 0;
    phase_ = phase::watch;
    flash_until_ = -1;
    reveal_ready_ = false;
    seed_ = mix(0x5eed1234u + static_cast<unsigned>(round_) * 7919u) | 1u;

    // sample kChanges distinct kinds: Fisher-Yates over 0..9 driven by
    // the round seed (the round is fully determined by round_)
    int idx[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    unsigned r = seed_;
    for (int i = 9; i > 0; --i)
    {
        r = r * 1664525u + 1013904223u;
        const int j = static_cast<int>((r >> 8) % static_cast<unsigned>(i + 1));
        const int tmp = idx[i];
        idx[i] = idx[j];
        idx[j] = tmp;
    }
    for (int i = 0; i < kChanges; ++i)
    {
        changes_[i].kind = idx[i];
        changes_[i].stamp = 0x7fffffff;
        changes_[i].found = false;
    }
    mark_dirty();
}

void SceneWidget::tick()
{
    ++rframe_;
    int left = kChanges;
    for (const change &c : changes_)
    {
        if (c.found)
        {
            --left;
        }
    }
    switch (phase_)
    {
    case phase::watch:
        for (int i = 0; i < kChanges; ++i)
        {
            if (rframe_ == kGlitchAt[i])
            {
                glitch_left_ = kGlitchLen;
                changes_[i].stamp = kGlitchAt[i];
            }
        }
        if (glitch_left_ > 0)
        {
            --glitch_left_;
        }
        std::snprintf(phase_buf_, sizeof(phase_buf_),
                      "WATCH - 3 CHANGES HIDE IN THE GLITCHES");
        if (rframe_ >= kWatchLen)
        {
            phase_ = phase::find;
        }
        break;
    case phase::find:
        std::snprintf(phase_buf_, sizeof(phase_buf_),
                      "FIND THEM - CLICK (%d LEFT)", left);
        if (rframe_ >= kWatchLen + kFindLen)
        {
            phase_ = phase::reveal;
        }
        break;
    case phase::reveal:
        std::snprintf(phase_buf_, sizeof(phase_buf_),
                      "T+0 vs NOW - BYTE-EXACT REPLAY");
        if (rframe_ >= kWatchLen + kFindLen + kRevealLen)
        {
            ++round_;
            new_round();
        }
        break;
    }
}

void SceneWidget::draw_ascii(core::Graphics &g, const char *text, const int x,
                             const int y, const core::Color &c) const
{
    char16_t tmp[80];
    int n = 0;
    for (; text[n] != 0 && n < 79; ++n)
    {
        tmp[n] = static_cast<char16_t>(text[n]);
    }
    draw_text_at(g, tmp, n, x, y, c);
}

void SceneWidget::draw_scene(core::Graphics &g, const unsigned t,
                             const bool *alts, const int glitch, const int bx,
                             const int by) const
{
    // virtual -> buffer mapping; bx/by shift the whole feed so a
    // clipped half-render lands where the clip expects it
    const auto X = [&](const int v)
    { return bx + ox_ + v * s_; };
    const auto Y = [&](const int v)
    { return by + oy_ + v * s_; };

    const int sw = kVW * s_;
    const int sh = kVH * s_;
    g.fill_gradient(bx, by, bx + sw - 1, by + sh - 1, kSky1, kSky2, false);

    // star field: positions and twinkle are pure functions of the seed
    unsigned r = seed_;
    for (int i = 0; i < 70; ++i)
    {
        r = r * 1664525u + 1013904223u;
        const int x = static_cast<int>((r >> 8) % 240u);
        r = r * 1664525u + 1013904223u;
        const int y = 2 + static_cast<int>((r >> 8) % 60u);
        r = r * 1664525u + 1013904223u;
        const int period = 24 + static_cast<int>((r >> 8) % 40u);
        r = r * 1664525u + 1013904223u;
        const int bright = 90 + static_cast<int>((r >> 8) % 120u);
        if (static_cast<int>((t + static_cast<unsigned>(i * 7)) % static_cast<unsigned>(period)) <
            period * 3 / 4)
        {
            const int b = bright;
            g.fill_rect(X(x), Y(y), X(x), Y(y),
                        core::Color::from(uc(b), uc(b > 245 ? 255 : b + 10), 255));
        }
    }

    // dim dust: a second, fainter layer low in the frame, same generator
    for (int i = 0; i < 30; ++i)
    {
        r = r * 1664525u + 1013904223u;
        const int x = static_cast<int>((r >> 8) % 240u);
        r = r * 1664525u + 1013904223u;
        const int y = 62 + static_cast<int>((r >> 8) % 56u);
        g.fill_rect(X(x), Y(y), X(x), Y(y), kStarDust);
    }

    // the north star: a sparkle cross; kind-0 dims it to a plain dot
    if (alts[0])
    {
        g.fill_rect(X(36), Y(18), X(36), Y(18), kStarDim);
    }
    else
    {
        g.draw_line(X(33), Y(18), X(39), Y(18), kNorth);
        g.draw_line(X(36), Y(15), X(36), Y(21), kNorth);
        g.fill_rect(X(36), Y(18), X(36), Y(18), kNorth);
    }
    if (alts[9])
    {
        // kind-9: an extra star with its own cross
        g.draw_line(X(143), Y(24), X(147), Y(24), kNorth);
        g.draw_line(X(145), Y(22), X(145), Y(26), kNorth);
    }

    // planet: a big banded disc entering the lower-right frame corner,
    // curved terminator (integer parabola), cropped by the feed edge
    const int pcx = 198, pcy = 114, pr = 44;
    const core::Color *ramp = alts[4] ? kBandAlt : kBand;
    for (int dy = -pr; dy <= pr; ++dy)
    {
        const int half = core::Graphics::corner_chord(pr, dy);
        if (half <= 0)
        {
            continue;
        }
        const int band = (dy + pr) * 9 / (2 * pr + 1);
        const int split = pcx + dy * dy * 2 / (3 * pr) - pr / 3;
        const int row_y = Y(pcy + dy);
        const int lit_end = split < pcx + half ? split : pcx + half;
        if (lit_end > pcx - half)
        {
            g.fill_rect(X(pcx - half), row_y, X(lit_end) - 1, row_y + s_ - 1,
                        ramp[band]);
        }
        const int dark_start = split > pcx - half ? split : pcx - half;
        if (dark_start < pcx + half)
        {
            g.fill_rect(X(dark_start), row_y, X(pcx + half) - 1, row_y + s_ - 1,
                        kBandDark[band]);
        }
    }
    if (alts[3])
    {
        g.draw_ellipse(X(pcx), Y(pcy), 56 * s_, 14 * s_, kRing);
    }

    // moons: one always, the second only after kind-2 lands
    g.fill_circle(X(150), Y(88), 3 * s_, kMoon);
    if (alts[2])
    {
        g.fill_circle(X(214), Y(66), 2 * s_, kMoon2);
    }

    // two satellites on the planet's orbit
    for (int i = 0; i < 2; ++i)
    {
        int sx = 0, sy = 0;
        const int deg = static_cast<int>((t * 2u + static_cast<unsigned>(i) * 137u) % 360u);
        core::point_on_circle(X(pcx), Y(pcy), 52 * s_, deg, &sx, &sy);
        g.fill_rect(sx - s_, sy - s_, sx + s_ - 1, sy + s_ - 1, kSat);
        g.fill_rect(sx - 3 * s_, sy, sx + 2 * s_, sy, kSat);
    }

    // shuttle: crawls left to right near the top; kind-5 doubles speed
    const int sx_v = static_cast<int>((t * (alts[5] ? 7u : 3u)) / 4u % 280u) - 20;
    const int sy_v = 14 + ((sx_v / 9) & 3);
    g.fill_rect(X(sx_v), Y(sy_v), X(sx_v + 9) - 1, Y(sy_v + 3) - 1, kShuttle);
    g.fill_rect(X(sx_v + 2), Y(sy_v + 1), X(sx_v + 3) - 1, Y(sy_v + 2) - 1,
                kWindowOff);
    if ((t & 3u) != 0u)
    {
        g.fill_rect(X(sx_v - 3), Y(sy_v + 1), X(sx_v - 1) - 1, Y(sy_v + 2) - 1,
                    kFlame);
    }

    // station: hull, windows, mast, dock lights
    g.fill_round_rect(X(34), Y(36), X(70) - 1, Y(58) - 1, 3 * s_, kHull);
    g.draw_round_rect(X(34), Y(36), X(70) - 1, Y(58) - 1, 3 * s_, kHullBorder);
    g.draw_line(X(52), Y(36), X(52), Y(22), kMast);
    for (int wy = 0; wy < 2; ++wy)
    {
        for (int wx = 0; wx < 5; ++wx)
        {
            const unsigned mix_in = static_cast<unsigned>(wx * 7 + wy * 13) +
                                    t / 40u + (alts[8] ? 5u : 0u);
            const bool lit = (mix_in & 3u) != 0u;
            g.fill_rect(X(40 + wx * 6), Y(40 + wy * 7), X(43 + wx * 6) - 1,
                        Y(43 + wy * 7) - 1, lit ? kWindow : kWindowOff);
        }
    }
    // antenna lamp: slow beacon until kind-1 makes it frantic
    const bool beacon = (t / (alts[1] ? 5u : 16u) & 1u) != 0u;
    if (beacon)
    {
        g.fill_circle(X(52), Y(21), s_, kAntennaOn);
    }
    else
    {
        g.fill_rect(X(52), Y(21), X(52), Y(21), kAntennaDim);
    }
    for (int i = 0; i < 5; ++i)
    {
        const bool chase = ((t / 8u + static_cast<unsigned>(i)) % 5u) == 0u;
        const core::Color &on = alts[7] ? kDockRed : kDock;
        const core::Color &off = alts[7] ? kDockRedDim : kDockDim;
        g.fill_rect(X(38 + i * 6), Y(60), X(39 + i * 6) - 1, Y(60), chase ? on : off);
    }

    // telemetry column: hex readouts that tick over every 32 frames
    char line[24];
    for (int row = 0; row < 5; ++row)
    {
        unsigned base = mix(seed_ + static_cast<unsigned>(row) * 7717u);
        unsigned v = base + (t / 32u) * static_cast<unsigned>(3 + row);
        if (row == 0 && alts[6])
        {
            v += 0x5a5au;
        }
        std::snprintf(line, sizeof(line), "%04X %04X", v & 0xffffu,
                      (v * 7u + 13u) & 0xffffu);
        draw_ascii(g, line, X(6), Y(120 + row * 6), kData);
    }

    // scanline sweep + hud + frame
    const int sl = static_cast<int>((t / 2u) % 150u);
    g.fill_rect(X(0), Y(sl), X(240) - 1, Y(sl) + s_ - 1, kScan);
    draw_ascii(g, "CH-07 SIGNAL DRIFT", X(5), Y(3), kHud);
    std::snprintf(line, sizeof(line), "SEED %06u", seed_ % 999983u);
    draw_ascii(g, line, X(178), Y(3), kHud);
    g.draw_rect(X(0), Y(0), X(240) - 1, Y(150) - 1, kBorder);

    // the glitch itself: tearing bars + specks while one is active
    if (glitch > 0)
    {
        for (int k = 0; k < 24; ++k)
        {
            const unsigned h = mix(seed_ + t * 131u + static_cast<unsigned>(k) * 977u);
            const int y = static_cast<int>(h % 150u);
            const int hh = 1 + static_cast<int>((h >> 8) % 3u);
            g.fill_rect(X(0), Y(y), X(240) - 1, Y(y + hh) - 1,
                        (k & 1) ? kGlitchA : kGlitchB);
        }
        for (int k = 0; k < 40; ++k)
        {
            const unsigned h = mix(seed_ * 31u + t * 17u + static_cast<unsigned>(k) * 613u);
            const int spx = static_cast<int>(h % 240u);
            const int spy = static_cast<int>((h >> 8) % 150u);
            g.fill_rect(X(spx), Y(spy), X(spx), Y(spy), kGlitchSpeck);
        }
    }
}

void SceneWidget::draw_at(core::Graphics &area) const
{
    const core::imsize_t sz = get_size();
    const int W = sz.width;
    const int H = sz.height;

    bool alts[10] = {};
    for (int i = 0; i < kChanges; ++i)
    {
        alts[changes_[i].kind] = rframe_ >= changes_[i].stamp;
    }

    if (phase_ == phase::reveal)
    {
        // both scenes are frozen: render once into offscreen buffers
        // (the zero-copy wrapper mode) and diff them every frame
        if (!reveal_ready_ || cache_w_ != W || cache_h_ != H)
        {
            buf_a_.resize(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
            buf_b_.resize(static_cast<size_t>(W) * static_cast<size_t>(H) * 4);
            cache_w_ = W;
            cache_h_ = H;
            bool none[10] = {};
            {
                auto ga = core::Graphics::make_ptr(static_cast<uint32_t>(W),
                                                   static_cast<uint32_t>(H), buf_a_.data());
                draw_scene(*ga, 0, none, 0, 0, 0);  // A = T+0, no changes yet
            }
            {
                auto gb = core::Graphics::make_ptr(static_cast<uint32_t>(W),
                                                   static_cast<uint32_t>(H), buf_b_.data());
                draw_scene(*gb, static_cast<unsigned>(kWatchLen + kFindLen - 1),
                           alts, 0, 0, 0);  // B = NOW, all changes applied
            }
            hash_a_ = fnv_bytes(buf_a_.data(), buf_a_.size());
            hash_b_ = fnv_bytes(buf_b_.data(), buf_b_.size());
            const uint32_t *pa = reinterpret_cast<const uint32_t *>(buf_a_.data());
            const uint32_t *pb = reinterpret_cast<const uint32_t *>(buf_b_.data());
            diff_px_ = 0;
            const size_t words = buf_a_.size() / 4;
            for (size_t i = 0; i < words; ++i)
            {
                if (pa[i] != pb[i])
                {
                    ++diff_px_;
                }
            }
            reveal_ready_ = true;
        }

        bool none[10] = {};
        const unsigned t_end = static_cast<unsigned>(kWatchLen + kFindLen - 1);
        {
            const auto cl = area.clip_safe(0, 0, W / 2, H);
            if (cl)
            {
                draw_scene(area, 0, none, 0, 0, 0);
            }
        }
        {
            const auto cl = area.clip_safe(W / 2, 0, W - W / 2, H);
            if (cl)
            {
                draw_scene(area, t_end, alts, 0, -(W / 2), 0);
            }
        }
        area.fill_rect(W / 2 - 1, 0, W / 2, H - 1, kAccent);

        // the actual pixel diff, marked on both halves
        const uint32_t *pa = reinterpret_cast<const uint32_t *>(buf_a_.data());
        const uint32_t *pb = reinterpret_cast<const uint32_t *>(buf_b_.data());
        const size_t words = buf_a_.size() / 4;
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                if (pa[y * static_cast<size_t>(W) + x] != pb[y * static_cast<size_t>(W) + x])
                {
                    area.draw_pixel(x, y, kAccent);
                }
            }
        }

        char cap[48];
        std::snprintf(cap, sizeof(cap), "T+0  #%08X", hash_a_);
        draw_ascii(area, cap, 10, H - 30, kTimer1);
        std::snprintf(cap, sizeof(cap), "NOW  #%08X", hash_b_);
        draw_ascii(area, cap, W / 2 + 10, H - 30, kTimer1);
        std::snprintf(cap, sizeof(cap), "%d PX DIFFER", diff_px_);
        draw_ascii(area, cap, W / 2 - 60, H - 30, kAccent);
        return;
    }

    draw_scene(area, static_cast<unsigned>(rframe_), alts,
               phase_ == phase::watch ? glitch_left_ : 0, 0, 0);

    if (phase_ == phase::find)
    {
        // countdown bar: cyan -> amber -> red as the clock runs out, on
        // a full-width dark track so it never floats on the feed
        const int remaining = kWatchLen + kFindLen - rframe_;
        const int frac = remaining * W / kFindLen;
        const core::Color &c = remaining * 3 > kFindLen * 2
            ? kTimer1
            : (remaining * 3 > kFindLen ? kTimer2 : kTimer3);
        area.fill_rect(0, 0, W - 1, 4, kTimerTrack);
        area.fill_rect(0, 1, frac, 3, c);

        for (int i = 0; i < kChanges; ++i)
        {
            if (!changes_[i].found)
            {
                continue;
            }
            const vrect &vr = kRegion[changes_[i].kind];
            const int x1 = ox_ + vr.x * s_ - 3;
            const int y1 = oy_ + vr.y * s_ - 3;
            const int x2 = ox_ + (vr.x + vr.w) * s_ + 2;
            const int y2 = oy_ + (vr.y + vr.h) * s_ + 2;
            area.draw_round_rect(x1, y1, x2, y2, 4, kFound);
            draw_ascii(area, "OK", x1 + 4, y2 + 4, kFound);
        }
        if (rframe_ < flash_until_)
        {
            area.draw_circle(flash_x_, flash_y_, 9, kMiss);
            area.draw_line(flash_x_ - 4, flash_y_ - 4, flash_x_ + 4, flash_y_ + 4, kMiss);
            area.draw_line(flash_x_ - 4, flash_y_ + 4, flash_x_ + 4, flash_y_ - 4, kMiss);
        }
    }
}

bool SceneWidget::on_input(const zb::input::input_event &ev)
{
    if (ev.type != zb::input::input_type::touch_down || phase_ != phase::find)
    {
        return false;
    }
    const core::impoint_t p = get_position();
    const int lx = ev.x - p.x;
    const int ly = ev.y - p.y;
    const int vx = (lx - ox_) / s_;
    const int vy = (ly - oy_) / s_;
    for (int i = 0; i < kChanges; ++i)
    {
        if (changes_[i].found)
        {
            continue;
        }
        const vrect &vr = kRegion[changes_[i].kind];
        if (vx >= vr.x - 5 && vx < vr.x + vr.w + 5 && vy >= vr.y - 5 &&
            vy < vr.y + vr.h + 5)
        {
            changes_[i].found = true;
            score_ += 100 + (kWatchLen + kFindLen - rframe_) / 12;
            mark_dirty();
            return true;
        }
    }
    score_ = score_ < 15 ? 0 : score_ - 15;
    flash_x_ = lx;
    flash_y_ = ly;
    flash_until_ = rframe_ + 18;
    mark_dirty();
    return true;
}

void Drift::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    // function-level static: the family must outlive every paint (the
    // showcase install_font shape)
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(drift_font, drift_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Drift::sync_header()
{
    if (!scene_)
    {
        return;
    }
    if (scene_->phase_text() != last_phase_)
    {
        phase_label_->set_text(scene_->phase_text());
        // the reveal caption wears magenta, mirroring the scene's XOR
        // accent (kAccent is private to the widget cpp); anything else
        // resets to the usual header tone
        phase_label_->set_text_color(
            std::strncmp(scene_->phase_text(), "T+0", 3) == 0
                ? core::Color::from(0xff, 0x4f, 0xd8)
                : core::Color::from(0xcf, 0xe6, 0xff));
        last_phase_ = scene_->phase_text();
    }
    if (scene_->score() != last_score_)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "SCORE %d", scene_->score());
        score_label_->set_text(buf);
        last_score_ = scene_->score();
    }
    if (scene_->round_no() != last_round_)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "R%d", scene_->round_no());
        round_label_->set_text(buf);
        last_round_ = scene_->round_no();
    }
}

void Drift::create_window(const uint32_t max_client_width,
                          const uint32_t max_client_height, void *buffer)
{
    install_font();

    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    const int w = static_cast<int>(max_client_width);
    const int h = static_cast<int>(max_client_height);
    const int u = clamp_i(h / 240, 1, 6);
    const int hh = 38 * u;

    auto &root = window_->root();
    root.set_background_color(core::Color::from(0x07, 0x0c, 0x16));

    auto title = std::make_unique<Label>();
    title->set_text("SIGNAL DRIFT");
    title->set_text_color(core::Color::from(0x6e, 0xe7, 0xff));
    title->set_bold(true);
    title->set_size(130 * u, 24 * u);
    title->set_position(10 * u, 8 * u);
    title->set_font_size(10 * u);
    root.add_child(std::move(title));

    auto phase = std::make_unique<Label>();
    phase->set_text("WATCH THE FEED");
    phase->set_text_color(core::Color::from(0xcf, 0xe6, 0xff));
    phase->set_size(w - 318 * u > 80 * u ? w - 318 * u : 80 * u, 18 * u);
    phase->set_position(150 * u, 12 * u);
    phase->set_font_size(7 * u);
    phase_label_ = phase.get();
    root.add_child(std::move(phase));

    auto score = std::make_unique<Label>();
    score->set_text("SCORE 0");
    score->set_text_color(core::Color::from(0xff, 0xb4, 0x54));
    score->set_size(90 * u, 20 * u);
    score->set_position(w - 160 * u, 10 * u);
    score->set_font_size(8 * u);
    score_label_ = score.get();
    root.add_child(std::move(score));

    auto round = std::make_unique<Label>();
    round->set_text("R1");
    round->set_text_color(core::Color::from(0x9f, 0xd8, 0xe8));
    round->set_size(46 * u, 20 * u);
    round->set_position(w - 62 * u, 10 * u);
    round->set_font_size(8 * u);
    round_label_ = round.get();
    root.add_child(std::move(round));

    auto scene = std::make_unique<SceneWidget>();
    const int ah = h - hh;
    const int s = clamp_i(w / SceneWidget::kVW < ah / SceneWidget::kVH
                              ? w / SceneWidget::kVW
                              : ah / SceneWidget::kVH,
                          1, 8);
    scene->set_size(w, ah);
    scene->set_position(0, hh);
    scene->set_geometry((w - SceneWidget::kVW * s) / 2,
                        (ah - SceneWidget::kVH * s) / 2, s,
                        clamp_i(5 * s, 8, 40));
    scene_ = scene.get();
    root.add_child(std::move(scene));
}
