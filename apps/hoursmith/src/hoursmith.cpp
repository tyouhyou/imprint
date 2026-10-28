#include "hoursmith.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "hoursmith_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::hoursmith;
using namespace zb::ui;

namespace
{
    constexpr float kDeg = 0.017453292519943295f;  // pi / 180
    constexpr float kPi = 3.14159265358979323846f;

    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    constexpr float clamp_f(const float v, const float lo, const float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // sky keyframes at dawn / morning / noon / afternoon / dusk; the day
    // lerps between neighbors, the night has its own pair
    const zb::raster::Rgba kSkyTop[5] = {{44, 38, 74, 255}, {92, 120, 168, 255}, {108, 150, 196, 255}, {92, 120, 168, 255}, {70, 48, 84, 255}};
    const zb::raster::Rgba kSkyBot[5] = {{196, 120, 86, 255}, {168, 190, 208, 255}, {186, 208, 222, 255}, {168, 190, 208, 255}, {214, 120, 80, 255}};
    const zb::raster::Rgba kSkyTopNight{10, 12, 28, 255};
    const zb::raster::Rgba kSkyBotNight{28, 34, 54, 255};
    const zb::raster::Rgba kStone{172, 162, 144, 255};
    const zb::raster::Rgba kStoneGroove{146, 138, 124, 255};
    const zb::raster::Rgba kSkirt{64, 58, 54, 255};
    const zb::raster::Rgba kInk{28, 30, 46, 255};

    const core::Color kTitle = core::Color::from(0xff, 0xe2, 0xa8);
    const core::Color kStatus = core::Color::from(0xe0, 0xdc, 0xd0);

    uint32_t shade(const zb::raster::Rgba &c, const float bright)
    {
        const int r = static_cast<int>(static_cast<float>(c.r) * bright);
        const int g = static_cast<int>(static_cast<float>(c.g) * bright);
        const int b = static_cast<int>(static_cast<float>(c.b) * bright);
        return zb::raster::pack(r > 255 ? 255 : r, g > 255 ? 255 : g,
                                b > 255 ? 255 : b);
    }

    // the same color word as a framework Color (from() narrows to bytes)
    core::Color color_of(const zb::raster::Rgba &c)
    {
        return core::Color::from(static_cast<uint8_t>(c.r),
                                 static_cast<uint8_t>(c.g),
                                 static_cast<uint8_t>(c.b));
    }

    void box_corners(const float cx, const float cy, const float cz,
                     const float hx, const float hy, const float hz,
                     float out[24])
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            out[bit * 3] = cx + ((bit & 1) ? hx : -hx);
            out[bit * 3 + 1] = cy + ((bit & 2) ? hy : -hy);
            out[bit * 3 + 2] = cz + ((bit & 4) ? hz : -hz);
        }
    }

    // screen-space distance to a segment (stone picking)
    float seg_dist(const float px, const float py, const float ax,
                   const float ay, const float bx, const float by)
    {
        const float dx = bx - ax;
        const float dy = by - ay;
        const float len2 = dx * dx + dy * dy;
        float t = 0.f;
        if (len2 > 1e-6f)
        {
            t = clamp_f(((px - ax) * dx + (py - ay) * dy) / len2, 0.f, 1.f);
        }
        const float qx = ax + dx * t - px;
        const float qy = ay + dy * t - py;
        return std::sqrt(qx * qx + qy * qy);
    }

    // hull of <= 8 ground points: a point is a hull vertex unless it is
    // strictly inside a triangle of the others (n is tiny, clarity wins),
    // then ordered by angle around the centroid so a fan consumes it
    int hull2d(const float hx[], const float hy[], const int n, float out_x[],
               float out_y[])
    {
        int m = 0;
        for (int i = 0; i < n; ++i)
        {
            bool inside = false;
            for (int a = 0; a < n && !inside; ++a)
            {
                if (a == i)
                {
                    continue;
                }
                for (int b = 0; b < n && !inside; ++b)
                {
                    if (b == i || b == a)
                    {
                        continue;
                    }
                    for (int c = 0; c < n && !inside; ++c)
                    {
                        if (c == i || c == a || c == b)
                        {
                            continue;
                        }
                        // strictly inside triangle abc: the three edge
                        // crosses share one strict sign
                        const float d1 = (hx[b] - hx[a]) * (hy[i] - hy[a]) -
                                         (hy[b] - hy[a]) * (hx[i] - hx[a]);
                        const float d2 = (hx[c] - hx[b]) * (hy[i] - hy[b]) -
                                         (hy[c] - hy[b]) * (hx[i] - hx[b]);
                        const float d3 = (hx[a] - hx[c]) * (hy[i] - hy[c]) -
                                         (hy[a] - hy[c]) * (hx[i] - hx[c]);
                        inside = (d1 > 1e-9f && d2 > 1e-9f && d3 > 1e-9f) ||
                                 (d1 < -1e-9f && d2 < -1e-9f && d3 < -1e-9f);
                    }
                }
            }
            if (!inside)
            {
                out_x[m] = hx[i];
                out_y[m] = hy[i];
                ++m;
            }
        }
        float cx = 0.f, cy = 0.f;
        for (int i = 0; i < m; ++i)
        {
            cx += out_x[i];
            cy += out_y[i];
        }
        cx /= static_cast<float>(m);
        cy /= static_cast<float>(m);
        int idx[8];
        for (int i = 0; i < m; ++i)
        {
            idx[i] = i;
        }
        std::sort(idx, idx + m, [&out_x, &out_y, cx, cy](const int a, const int b)
                  { return std::atan2(out_y[a] - cy, out_x[a] - cx) <
                           std::atan2(out_y[b] - cy, out_x[b] - cx); });
        float sx[8], sy[8];
        for (int i = 0; i < m; ++i)
        {
            sx[i] = out_x[idx[i]];
            sy[i] = out_y[idx[i]];
        }
        for (int i = 0; i < m; ++i)
        {
            out_x[i] = sx[i];
            out_y[i] = sy[i];
        }
        return m;
    }

    // Sutherland-Hodgman against the dial circle: a dawn shadow can run
    // four hundred units; the plaza ends at 150
    void clip_circle(float x[], float y[], int &n, const float r)
    {
        float in_x[16], in_y[16];
        int m = 0;
        for (int i = 0; i < n; ++i)
        {
            const int j = (i + 1) % n;
            const float ax = x[i], ay = y[i];
            const float bx = x[j], by = y[j];
            const float da = ax * ax + ay * ay - r * r;
            const float db = bx * bx + by * by - r * r;
            if (da <= 0.f)
            {
                in_x[m] = ax;
                in_y[m] = ay;
                ++m;
            }
            if ((da <= 0.f) != (db <= 0.f))
            {
                // exactly one root lies in (0,1), but which one depends on
                // the crossing direction: an EXIT edge's in-segment root is
                // the plus one, an ENTRY edge's is the minus one. Taking the
                // plus root unconditionally dropped every entry crossing --
                // a wedge off every dawn/dusk shadow, exactly the hours the
                // puzzle is about.
                const float dx = bx - ax, dy = by - ay;
                const float qa = dx * dx + dy * dy;
                const float qb = 2.f * (ax * dx + ay * dy);
                const float qc = da;
                const float disc =
                    std::sqrt(std::max(0.f, qb * qb - 4.f * qa * qc));
                const float t1 = (-qb - disc) / (2.f * qa);
                const float t2 = (-qb + disc) / (2.f * qa);
                const float t = (t1 > 0.f && t1 < 1.f) ? t1 : t2;
                if (t > 0.f && t < 1.f)
                {
                    in_x[m] = ax + dx * t;
                    in_y[m] = ay + dy * t;
                    ++m;
                }
            }
        }
        n = clamp_i(m, 0, 16);
        for (int i = 0; i < n; ++i)
        {
            x[i] = in_x[i];
            y[i] = in_y[i];
        }
    }
}  // namespace

const zb::raster::Rgba DialCanvas::kPieceColor[kMaxPieces] = {
    {196, 92, 70, 255}, {86, 156, 144, 255}, {214, 168, 84, 255}};

unsigned DialCanvas::hash(const unsigned a, const unsigned b) const
{
    unsigned h = a * 73856093u ^ b * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float DialCanvas::hour_now() const
{
    return 6.f + 12.f * static_cast<float>(frame_in_day_) /
                     static_cast<float>(kDayFrames);
}

float DialCanvas::sun_elevation(const float hour)
{
    // a 62-degree arc peaking at noon, floored at 6 so the dawn and dusk
    // shadows stay long but finite (cot 6 deg is the worst stretch)
    const float th = kPi * (hour - 6.f) / 12.f;
    return std::max(kDeg * 6.f, kDeg * 62.f * std::sin(th));
}

void DialCanvas::sun(float &sx, float &sy, float &sz, float &elev) const
{
    if (night_)
    {
        // dim fixed night light from the north-east sky
        sx = 0.3f;
        sy = 0.85f;
        sz = -0.42f;
        elev = 0.f;
        return;
    }
    const float th = kPi * (hour_now() - 6.f) / 12.f;
    const float e = sun_elevation(hour_now());
    sx = std::cos(th) * std::cos(e);
    sy = std::sin(e);
    sz = std::sin(th) * std::cos(e);
    elev = e;
}

void DialCanvas::shadow_offset(const float h, float &dx, float &dz) const
{
    float sx, sy, sz, e;
    sun(sx, sy, sz, e);
    const float flat = std::sqrt(sx * sx + sz * sz);
    const float stretch = h * flat / sy;
    dx = -stretch * sx / flat;
    dz = -stretch * sz / flat;
}

void DialCanvas::tip_at_hour(const Piece &p, const float hour, float &tx,
                             float &tz) const
{
    // the same geometry as shadow_offset, at an arbitrary hour: the plan
    // inset's arc must not drift from what the world draws
    const float th = kPi * (hour - 6.f) / 12.f;
    const float e = sun_elevation(hour);
    const float st = p.h * std::cos(e) / std::sin(e);
    tx = p.x - st * std::cos(th);
    tz = p.z - st * std::sin(th);
}

void DialCanvas::tip_of(const Piece &p, float &tx, float &tz) const
{
    float dx, dz;
    shadow_offset(p.h, dx, dz);
    tx = p.x + dx;
    tz = p.z + dz;
}

void DialCanvas::build_camera(zb::raster::Camera &cam) const
{
    // eye south of the plaza, pitched down at the center; the basis is
    // orthonormal by construction (up = (0, fz, -fy) for right = +x)
    cam.ox = 0.f;
    cam.oy = 185.f;
    cam.oz = -255.f;
    const float len = std::sqrt(185.f * 185.f + 255.f * 255.f);
    cam.fx = 0.f;
    cam.fy = -185.f / len;
    cam.fz = 255.f / len;
    cam.rx = 1.f;
    cam.ry = 0.f;
    cam.rz = 0.f;
    cam.ux = 0.f;
    cam.uy = cam.fz;
    cam.uz = -cam.fy;
    cam.plane = 0.68f;
    cam.vw = vw_;
    cam.vh = vh_;
    cam.horizon = 0.36f * static_cast<float>(vh_);
    cam.near_clip = 0.5f;
    cam.depth = nullptr;  // painter's algorithm, back to front
}

bool DialCanvas::unproject(const int sx, const int sy, float &x,
                           float &z) const
{
    zb::raster::Camera cam;
    build_camera(cam);
    const float vs = zb::raster::vscale(cam);
    const float a =
        (static_cast<float>(sx) - 0.5f * static_cast<float>(vw_)) / vs;
    const float b = (cam.horizon - static_cast<float>(sy)) / vs;
    const float dx = cam.fx + cam.rx * a + cam.ux * b;
    const float dy = cam.fy + cam.ry * a + cam.uy * b;
    const float dz = cam.fz + cam.rz * a + cam.uz * b;
    if (dy > -1e-3f)
    {
        return false;  // the ray lifts off the plaza
    }
    const float t = -cam.oy / dy;
    x = cam.ox + t * dx;
    z = cam.oz + t * dz;
    return true;
}

void DialCanvas::set_geometry(const int width, const int height)
{
    vw_ = width;
    vh_ = height;
    rc_.resize(vw_, vh_);
    rc_.set_replicate(2);
    if (!inited_)
    {
        // the widget's first geometry: lay out level 1 (the level table
        // is constexpr but the pieces live only in set_level's copy)
        inited_ = true;
        set_level(0);
    }
    mark_dirty();
}

void DialCanvas::reset()
{
    day_ = 1;
    frame_ = 0;
    set_level(0);
}

void DialCanvas::set_level(const int level)
{
    level_ = clamp_i(level, 0, kLevels - 1);
    for (int i = 0; i < kMaxPieces; ++i)
    {
        pieces_[i] = kLevelStart[level_][i];
        targets_[i] = kLevelRings[level_][i];
        targets_[i].r = kLevelRingR[level_];
        charge_[i] = 0;
        rung_[i] = false;
        trail_len_[i] = 0;
        trail_head_[i] = 0;
    }
    frame_in_day_ = 0;
    night_ = false;
    night_frames_ = 0;
    flash_ = 0;
    selected_ = -1;
    dragging_ = false;
    mark_dirty();
}

int DialCanvas::level_bells() const
{
    int n = 0;
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        if (rung_[i])
        {
            ++n;
        }
    }
    return n;
}

void DialCanvas::level_text(char *buf, const int cap) const
{
    // the status label is ~200 px wide at the portal size: one short
    // line, and the count in front carries the progress
    static const char *const kHint[kLevels] = {
        "NOON: CRIMSON TIP IN RING AT 12",
        "BELLS 9 AND 15: ONE STONE EACH",
        "THREE BELLS: 8, 12 AND 16",
        "MASTER'S DAY: 7, 12 AND 17"};
    if (night_)
    {
        std::snprintf(buf, static_cast<size_t>(cap), "DUSK - DAY %d: %d/%d",
                      day_, level_bells(), kLevelCount[level_]);
    }
    else
    {
        std::snprintf(buf, static_cast<size_t>(cap), "DAY %d  %s", day_,
                      kHint[level_]);
    }
}

void DialCanvas::tick()
{
    // the frame counter keeps breathing through the night (stars and the
    // summary stay alive); only the sun and the bells sleep
    ++frame_;
    if (flash_ > 0)
    {
        --flash_;
    }
    if (night_)
    {
        if (++night_frames_ >= kNightFrames)
        {
            const bool cleared = level_bells() == kLevelCount[level_];
            day_++;
            if (cleared && level_ + 1 < kLevels)
            {
                set_level(level_ + 1);
            }
            else
            {
                night_ = false;
                night_frames_ = 0;
                frame_in_day_ = 0;
                for (int i = 0; i < kLevelCount[level_]; ++i)
                {
                    charge_[i] = 0;
                    rung_[i] = false;
                }
                mark_dirty();
            }
        }
        return;
    }
    if (++frame_in_day_ >= kDayFrames)
    {
        night_ = true;
        night_frames_ = 0;
        mark_dirty();
        return;
    }

    // each ring charges while its stone's tip holds inside it during the
    // ring's hour window; a miss decays and the bell waits for tomorrow
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        if (rung_[i])
        {
            continue;
        }
        const float dt = hour_now() - static_cast<float>(targets_[i].hour);
        if (dt < -0.35f || dt > 0.35f)
        {
            charge_[i] = 0;
            continue;
        }
        float tx, tz;
        tip_of(pieces_[i], tx, tz);
        const float ex = tx - targets_[i].x;
        const float ez = tz - targets_[i].z;
        if (ex * ex + ez * ez <= targets_[i].r * targets_[i].r)
        {
            if (++charge_[i] >= 50)
            {
                rung_[i] = true;
                flash_ = 40;
                mark_dirty();
            }
        }
        else
        {
            charge_[i] = charge_[i] > 2 ? charge_[i] - 2 : 0;
        }
    }

    // the plan inset's tip trails, one sample every third frame
    if (frame_ % 3u == 0u)
    {
        for (int i = 0; i < kLevelCount[level_]; ++i)
        {
            float tx, tz;
            tip_of(pieces_[i], tx, tz);
            trail_[i][trail_head_[i]][0] = tx;
            trail_[i][trail_head_[i]][1] = tz;
            trail_head_[i] = (trail_head_[i] + 1) % kTrail;
            if (trail_len_[i] < kTrail)
            {
                trail_len_[i]++;
            }
        }
    }
}

bool DialCanvas::on_input(const zb::input::input_event &ev)
{
    // the dispatcher hands on_input the ORIGINAL event in window space
    // while every rect, projection and unprojection here lives in this
    // widget's own space (the canvas sits below the HUD strip), so the
    // translation happens once, at the door
    const core::impoint_t origin = get_position();
    const int lx = ev.x - origin.x;
    const int ly = ev.y - origin.y;
    switch (ev.type)
    {
    case zb::input::input_type::touch_down:
    {
        const float px = static_cast<float>(lx);
        const float py = static_cast<float>(ly);
        // the height keys own their rects when a stone is picked
        const int up_y = vh_ - kPlanBox - 12;
        const int dn_y = up_y + kBtnH + 6;
        if (selected_ >= 0 && px >= static_cast<float>(kBtnX) &&
            px <= static_cast<float>(kBtnX + kBtnW) &&
            py >= static_cast<float>(up_y) &&
            py <= static_cast<float>(up_y + kBtnH))
        {
            pieces_[selected_].h =
                clamp_f(pieces_[selected_].h + 2.f, kHLo, kHHi);
            mark_dirty();
            return true;
        }
        if (selected_ >= 0 && px >= static_cast<float>(kBtnX) &&
            px <= static_cast<float>(kBtnX + kBtnW) &&
            py >= static_cast<float>(dn_y) &&
            py <= static_cast<float>(dn_y + kBtnH))
        {
            pieces_[selected_].h =
                clamp_f(pieces_[selected_].h - 2.f, kHLo, kHHi);
            mark_dirty();
            return true;
        }
        // pick the nearest stone: distance to its base->top screen edge
        zb::raster::Camera cam;
        build_camera(cam);
        int pick = -1;
        float best = 16.f;
        for (int i = 0; i < kLevelCount[level_]; ++i)
        {
            const zb::raster::Vtx b =
                zb::raster::project(cam, pieces_[i].x, 0.f, pieces_[i].z);
            const zb::raster::Vtx t =
                zb::raster::project(cam, pieces_[i].x, pieces_[i].h,
                                    pieces_[i].z);
            const float d = seg_dist(px, py, b.sx, b.sy, t.sx, t.sy);
            if (d < best)
            {
                best = d;
                pick = i;
            }
        }
        selected_ = pick;
        dragging_ = pick >= 0;
        if (pick >= 0)
        {
            float gx, gz;
            if (unproject(lx, ly, gx, gz))
            {
                drag_off_x_ = pieces_[pick].x - gx;
                drag_off_z_ = pieces_[pick].z - gz;
            }
            else
            {
                drag_off_x_ = 0.f;
                drag_off_z_ = 0.f;
            }
        }
        mark_dirty();
        return true;
    }
    case zb::input::input_type::touch_move:
    {
        if (!dragging_ || selected_ < 0)
        {
            return false;
        }
        float gx, gz;
        if (!unproject(lx, ly, gx, gz))
        {
            return true;
        }
        float nx = gx + drag_off_x_;
        float nz = gz + drag_off_z_;
        // stay on the plaza, off the tower and off the other stones
        const float r = std::sqrt(nx * nx + nz * nz);
        if (r > kDialR - 10.f)
        {
            const float s = (kDialR - 10.f) / r;
            nx *= s;
            nz *= s;
        }
        if (r < 16.f)
        {
            const float s = 16.f / (r > 1e-3f ? r : 1e-3f);
            nx *= s;
            nz *= s;
        }
        for (int i = 0; i < kLevelCount[level_]; ++i)
        {
            if (i == selected_)
            {
                continue;
            }
            const float ex = nx - pieces_[i].x;
            const float ez = nz - pieces_[i].z;
            const float d = std::sqrt(ex * ex + ez * ez);
            if (d < 10.f)
            {
                const float s = 10.f / (d > 1e-3f ? d : 1e-3f);
                nx = pieces_[i].x + ex * s;
                nz = pieces_[i].z + ez * s;
            }
        }
        pieces_[selected_].x = nx;
        pieces_[selected_].z = nz;
        mark_dirty();
        return true;
    }
    case zb::input::input_type::touch_up:
        dragging_ = false;
        return true;
    default:
        return false;
    }
}

void DialCanvas::adjust_height(const float delta)
{
    if (selected_ < 0)
    {
        return;
    }
    pieces_[selected_].h = clamp_f(pieces_[selected_].h + delta, kHLo, kHHi);
    mark_dirty();
}

void DialCanvas::render_world() const
{
    zb::raster::Camera cam;
    build_camera(cam);

    // sky: the day lerps between neighbor keyframes, the night is its own
    if (night_)
    {
        rc_.fill_gradient_v(0, 0, vw_ - 1, vh_ - 1, kSkyTopNight,
                            kSkyBotNight);
    }
    else
    {
        const float t = clamp_f((hour_now() - 6.f) / 12.f, 0.f, 1.f) * 4.f;
        const int seg = clamp_i(static_cast<int>(t), 0, 3);
        const int w = clamp_i(
            static_cast<int>((t - static_cast<float>(seg)) * 256.f), 0, 256);
        rc_.fill_gradient_v(0, 0, vw_ - 1, vh_ - 1,
                            zb::raster::lerp_rgba(kSkyTop[seg], kSkyTop[seg + 1],
                                                  w, 256),
                            zb::raster::lerp_rgba(kSkyBot[seg], kSkyBot[seg + 1],
                                                  w, 256));
    }

    draw_ground(cam);
    draw_shadows(cam);
    draw_solids(cam);

    rc_.upscale();

    // lights live at 1:1 on the surface, past the tent
    rc_.set_replicate(1);
    draw_sky_lights(cam);
}

void DialCanvas::draw_ground(const zb::raster::Camera &cam) const
{
    // the plaza: four flat bands x 24 sectors, grooves between, and a
    // slow hash-driven shimmer so the stone breathes
    constexpr float kBandR[5] = {0.f, 40.f, 80.f, 116.f, 150.f};
    constexpr int kSect = 24;
    for (int band = 0; band < 4; ++band)
    {
        for (int s = 0; s < kSect; ++s)
        {
            const float t0 = static_cast<float>(s) *
                             (6.2831853f / static_cast<float>(kSect));
            const float t1 = static_cast<float>(s + 1) *
                             (6.2831853f / static_cast<float>(kSect));
            const float r0 = kBandR[band];
            const float r1 = kBandR[band + 1];
            const unsigned sh = hash(static_cast<unsigned>(band * kSect + s),
                                     static_cast<unsigned>(frame_) / 16u);
            const float bright =
                0.985f + 0.03f * (static_cast<float>(sh % 16u) / 16.f);
            const zb::raster::Rgba &base =
                (band == 1 || band == 3) ? kStoneGroove : kStone;
            zb::raster::Vtx c[4];
            c[0] = zb::raster::project(cam, r0 * std::sin(t0), 0.f,
                                       r0 * std::cos(t0));
            c[1] = zb::raster::project(cam, r0 * std::sin(t1), 0.f,
                                       r0 * std::cos(t1));
            c[2] = zb::raster::project(cam, r1 * std::sin(t1), 0.f,
                                       r1 * std::cos(t1));
            c[3] = zb::raster::project(cam, r1 * std::sin(t0), 0.f,
                                       r1 * std::cos(t0));
            zb::raster::FaceShade shd;
            shd.base = shade(base, bright);
            shd.bright = 1.f;
            zb::raster::raster_quad(rc_, cam, c, shd);
        }
    }

    // the platform skirt: the dial is an observatory on a pillar, and
    // the sky shows past its far rim
    for (int s = 0; s < kSect; ++s)
    {
        const float t0 = static_cast<float>(s) *
                         (6.2831853f / static_cast<float>(kSect));
        const float t1 = static_cast<float>(s + 1) *
                         (6.2831853f / static_cast<float>(kSect));
        const float w0 =
            0.5f + 0.5f * std::abs(std::sin(0.5f * (t0 + t1)));
        zb::raster::Vtx c[4];
        c[0] = zb::raster::project(cam, kDialR * std::sin(t0), 0.f,
                                   kDialR * std::cos(t0));
        c[1] = zb::raster::project(cam, kDialR * std::sin(t1), 0.f,
                                   kDialR * std::cos(t1));
        c[2] = zb::raster::project(cam, kDialR * std::sin(t1), -26.f,
                                   kDialR * std::cos(t1));
        c[3] = zb::raster::project(cam, kDialR * std::sin(t0), -26.f,
                                   kDialR * std::cos(t0));
        zb::raster::FaceShade shd;
        shd.base = shade(kSkirt, 0.8f + 0.25f * w0);
        shd.bright = 1.f;
        zb::raster::raster_quad(rc_, cam, c, shd);
    }

    // ring wells: the targets, warming with charge, gold when rung
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        const float frac =
            rung_[i] ? 1.f
                     : clamp_f(static_cast<float>(charge_[i]) / 50.f, 0.f,
                               1.f);
        const zb::raster::Rgba warm{
            static_cast<int>(static_cast<float>(kPieceColor[i].r) +
                             (230.f - static_cast<float>(kPieceColor[i].r)) *
                                 frac),
            static_cast<int>(static_cast<float>(kPieceColor[i].g) +
                             (196.f - static_cast<float>(kPieceColor[i].g)) *
                                 frac),
            static_cast<int>(static_cast<float>(kPieceColor[i].b) +
                             (96.f - static_cast<float>(kPieceColor[i].b)) *
                                 frac),
            255};
        constexpr int kFan = 10;
        for (int s = 0; s < kFan; ++s)
        {
            const float t0 =
                static_cast<float>(s) * (6.2831853f / static_cast<float>(kFan));
            const float t1 = static_cast<float>(s + 1) *
                             (6.2831853f / static_cast<float>(kFan));
            zb::raster::Vtx c[4];
            c[0] = zb::raster::project(cam, targets_[i].x, 0.05f,
                                       targets_[i].z);
            c[1] = zb::raster::project(
                cam, targets_[i].x + targets_[i].r * std::sin(t0), 0.05f,
                targets_[i].z + targets_[i].r * std::cos(t0));
            c[2] = zb::raster::project(
                cam, targets_[i].x + targets_[i].r * std::sin(t1), 0.05f,
                targets_[i].z + targets_[i].r * std::cos(t1));
            c[3] = c[2];
            zb::raster::FaceShade shd;
            shd.base = shade(warm, 0.75f);
            shd.bright = 1.f;
            zb::raster::raster_quad(rc_, cam, c, shd);
        }
    }
}

void DialCanvas::shadow_hull(const Piece &p, float hx[8], float hz[8],
                             int &n) const
{
    float dx, dz;
    shadow_offset(p.h, dx, dz);
    // base square then its translated top: the shadow of a vertical prism
    n = 0;
    hx[n] = p.x - 3.f;
    hz[n] = p.z - 3.f;
    ++n;
    hx[n] = p.x + 3.f;
    hz[n] = p.z - 3.f;
    ++n;
    hx[n] = p.x + 3.f;
    hz[n] = p.z + 3.f;
    ++n;
    hx[n] = p.x - 3.f;
    hz[n] = p.z + 3.f;
    ++n;
    for (int i = 0; i < 4; ++i)
    {
        hx[n] = hx[i] + dx;
        hz[n] = hz[i] + dz;
        ++n;
    }
}

void DialCanvas::draw_shadows(const zb::raster::Camera &cam) const
{
    if (night_)
    {
        return;
    }
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        float hx[8], hz[8];
        int n = 0;
        shadow_hull(pieces_[i], hx, hz, n);
        // the clip can double the point count (one exit + one re-entry)
        float ox[16], oz[16];
        int m = hull2d(hx, hz, n, ox, oz);
        if (m < 3)
        {
            continue;
        }
        clip_circle(ox, oz, m, kDialR - 3.f);
        if (m < 3)
        {
            continue;
        }
        float cx = 0.f, cz = 0.f;
        for (int k = 0; k < m; ++k)
        {
            cx += ox[k];
            cz += oz[k];
        }
        cx /= static_cast<float>(m);
        cz /= static_cast<float>(m);
        zb::raster::FaceShade shd;
        shd.base = shade(kInk, 1.f);
        shd.bright = 1.f;
        for (int k = 0; k < m; ++k)
        {
            const int k1 = (k + 1) % m;
            zb::raster::Vtx c[4];
            c[0] = zb::raster::project(cam, cx, 0.05f, cz);
            c[1] = zb::raster::project(cam, ox[k], 0.05f, oz[k]);
            c[2] = zb::raster::project(cam, ox[k1], 0.05f, oz[k1]);
            c[3] = c[2];
            zb::raster::raster_quad(rc_, cam, c, shd);
        }
    }
}

void DialCanvas::draw_box_at(const zb::raster::Camera &cam, const float cx,
                             const float cy, const float cz, const float hx,
                             const float hy, const float hz,
                             const zb::raster::Rgba &c) const
{
    float corners[24];
    box_corners(cx, cy, cz, hx, hy, hz, corners);
    zb::raster::FaceStyle face[6];
    const uint32_t word = zb::raster::pack(c.r, c.g, c.b);
    const uint32_t top = shade(c, 1.12f);
    for (int f = 0; f < 6; ++f)
    {
        face[f].base = f == 2 ? top : word;
        face[f].grad = f == 2 ? 0.f : 0.22f;
    }
    float lx, ly, lz, e;
    sun(lx, ly, lz, e);
    zb::raster::BoxPaint paint;
    paint.light[0] = lx;
    paint.light[1] = ly;
    paint.light[2] = lz;
    paint.ambient = 0.55f;
    paint.lambert = 0.5f;
    paint.hemi = 0.2f;
    paint.rim = 0.15f;
    zb::raster::draw_box(rc_, cam, corners, face, paint);
}

void DialCanvas::draw_solids(const zb::raster::Camera &cam) const
{
    items_.clear();
    // stones
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        DrawItem it;
        it.v = (pieces_[i].x - cam.ox) * cam.fx +
               (pieces_[i].h * 0.5f - cam.oy) * cam.fy +
               (pieces_[i].z - cam.oz) * cam.fz;
        it.kind = 0;
        it.a = i;
        it.b = 0;
        items_.push_back(it);
    }
    // hour marks at 6..18: each sits where a shadow POINTS at that hour
    for (int hidx = 0; hidx <= 12; ++hidx)
    {
        const float th = kPi * static_cast<float>(hidx) / 12.f;
        const float x = -std::cos(th) * kMarkR;
        const float z = -std::sin(th) * kMarkR;
        DrawItem it;
        it.v = (x - cam.ox) * cam.fx + (0.9f - cam.oy) * cam.fy +
               (z - cam.oz) * cam.fz;
        it.kind = 1;
        it.a = hidx;
        it.b = 0;
        items_.push_back(it);
    }
    // the tower (one draw item, five boxes inside)
    DrawItem tw;
    tw.v = (0.f - cam.ox) * cam.fx + (9.f - cam.oy) * cam.fy +
           (0.f - cam.oz) * cam.fz;
    tw.kind = 2;
    tw.a = 0;
    tw.b = 0;
    items_.push_back(tw);
    // ring pebbles
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        for (int k = 0; k < 10; ++k)
        {
            const float t = static_cast<float>(k) * (6.2831853f / 10.f);
            const float x = targets_[i].x + targets_[i].r * std::sin(t);
            const float z = targets_[i].z + targets_[i].r * std::cos(t);
            DrawItem it;
            it.v = (x - cam.ox) * cam.fx + (0.7f - cam.oy) * cam.fy +
                   (z - cam.oz) * cam.fz;
            it.kind = 3;
            it.a = i;
            it.b = k;
            items_.push_back(it);
        }
    }

    std::sort(items_.begin(), items_.end(),
              [](const DrawItem &p, const DrawItem &q)
              {
                  if (p.v != q.v)
                  {
                      return p.v > q.v;
                  }
                  if (p.kind != q.kind)
                  {
                      return p.kind < q.kind;
                  }
                  if (p.a != q.a)
                  {
                      return p.a < q.a;
                  }
                  return p.b < q.b;
              });

    for (size_t i = 0; i < items_.size(); ++i)
    {
        const DrawItem &it = items_[i];
        switch (it.kind)
        {
        case 0:
        {
            const Piece &p = pieces_[it.a];
            const bool sel = it.a == selected_;
            draw_box_at(cam, p.x, p.h * 0.5f, p.z, 3.f, p.h * 0.5f, 3.f,
                        kPieceColor[it.a]);
            // the color cap links a stone to its ring; gold after the bell
            draw_box_at(cam, p.x, p.h + 1.1f, p.z, 2.f, 1.1f, 2.f,
                        rung_[it.a] ? zb::raster::Rgba{230, 196, 96, 255}
                                    : kPieceColor[it.a]);
            if (sel)
            {
                draw_box_at(cam, p.x, p.h + 2.6f, p.z, 0.5f, 0.4f, 0.5f,
                            zb::raster::Rgba{255, 250, 240, 255});
            }
            break;
        }
        case 1:
        {
            const float th = kPi * static_cast<float>(it.a) / 12.f;
            const float x = -std::cos(th) * kMarkR;
            const float z = -std::sin(th) * kMarkR;
            const bool noon = it.a == 6;
            draw_box_at(cam, x, noon ? 1.5f : 1.1f, z, 1.6f,
                        noon ? 1.5f : 1.1f, 1.6f,
                        noon ? zb::raster::Rgba{172, 160, 132, 255}
                             : zb::raster::Rgba{138, 130, 116, 255});
            break;
        }
        case 2:
            draw_box_at(cam, 0.f, 1.6f, 0.f, 8.f, 1.6f, 8.f,
                        zb::raster::Rgba{150, 142, 128, 255});
            draw_box_at(cam, 0.f, 7.7f, 0.f, 1.8f, 4.5f, 1.8f,
                        zb::raster::Rgba{142, 134, 120, 255});
            draw_box_at(cam, 0.f, 14.f, 0.f, 2.2f, 1.8f, 2.2f,
                        zb::raster::Rgba{222, 182, 96, 255});
            draw_box_at(cam, 0.f, 16.5f, 0.f, 4.5f, 0.7f, 4.5f,
                        zb::raster::Rgba{134, 126, 114, 255});
            draw_box_at(cam, 0.f, 18.2f, 0.f, 0.7f, 1.f, 0.7f,
                        zb::raster::Rgba{222, 182, 96, 255});
            break;
        case 3:
        {
            const float t = static_cast<float>(it.b) * (6.2831853f / 10.f);
            const float x = targets_[it.a].x + targets_[it.a].r * std::sin(t);
            const float z = targets_[it.a].z + targets_[it.a].r * std::cos(t);
            const float frac =
                rung_[it.a]
                    ? 1.f
                    : clamp_f(static_cast<float>(charge_[it.a]) / 50.f, 0.f,
                              1.f);
            const zb::raster::Rgba &col =
                rung_[it.a] ? zb::raster::Rgba{230, 196, 96, 255}
                            : kPieceColor[it.a];
            draw_box_at(cam, x, 0.65f + 0.5f * frac, z, 1.f,
                        0.65f + 0.5f * frac, 1.f, col);
            break;
        }
        default:
            break;
        }
    }
}

void DialCanvas::draw_sky_lights(const zb::raster::Camera &cam) const
{
    if (night_)
    {
        // the night interlude: a moon climbing the top band and a hash
        // of stars, all safely above the plaza's far rim
        const float mb =
            0.24f + 0.06f * (static_cast<float>(night_frames_) /
                             static_cast<float>(kNightFrames));
        const zb::raster::Vtx mp = zb::raster::project(
            cam, cam.ox, cam.oy + (cam.uy * mb + cam.fy) * 2500.f,
            cam.oz + (cam.uz * mb + cam.fz) * 2500.f);
        rc_.glow(static_cast<int>(mp.sx), static_cast<int>(mp.sy), 20, 220,
                 226, 240, 0.5f);
        rc_.fill_circle(static_cast<int>(mp.sx), static_cast<int>(mp.sy), 6,
                        shade(zb::raster::Rgba{226, 230, 244, 255}, 1.f));
        for (int s = 0; s < 70; ++s)
        {
            const unsigned sh = hash(static_cast<unsigned>(s), 7u);
            const int x = static_cast<int>(sh % static_cast<unsigned>(vw_));
            const int y = static_cast<int>((sh >> 8) % 40u);
            rc_.add_pixel(x, y, 200, 210, 235,
                          static_cast<int>(60u + (sh >> 16) % 120u));
        }
    }

    // ring charge glows and the bell's ring flash
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        const zb::raster::Vtx tp =
            zb::raster::project(cam, targets_[i].x, 2.f, targets_[i].z);
        if (tp.sy < 0.f || tp.sy > static_cast<float>(vh_))
        {
            continue;
        }
        if (rung_[i])
        {
            const float pulse =
                0.5f + 0.5f * std::sin(static_cast<float>(frame_) * 0.18f);
            rc_.glow(static_cast<int>(tp.sx), static_cast<int>(tp.sy),
                     12 + static_cast<int>(6.f * pulse), 240, 200, 100,
                     0.4f + 0.25f * pulse);
        }
        else if (charge_[i] > 0)
        {
            rc_.glow(static_cast<int>(tp.sx), static_cast<int>(tp.sy), 10,
                     kPieceColor[i].r, kPieceColor[i].g, kPieceColor[i].b,
                     0.4f * (static_cast<float>(charge_[i]) / 50.f));
        }
    }
    if (flash_ > 0)
    {
        const zb::raster::Vtx bp = zb::raster::project(cam, 0.f, 14.f, 0.f);
        rc_.glow(static_cast<int>(bp.sx), static_cast<int>(bp.sy),
                 8 + (40 - flash_) / 3, 255, 214, 120,
                 static_cast<float>(flash_) / 40.f);
    }
    if (selected_ >= 0)
    {
        const zb::raster::Vtx sp = zb::raster::project(
            cam, pieces_[selected_].x, 0.5f, pieces_[selected_].z);
        rc_.glow(static_cast<int>(sp.sx), static_cast<int>(sp.sy), 14, 255,
                 250, 235, 0.25f);
    }
}

void DialCanvas::draw_plan(core::Graphics &area) const
{
    // the smith's paper: a flat plan of the plaza. North is up (z-),
    // east is right (x+): the same compass the 3D view reads.
    const int bx = 12;
    const int by = vh_ - kPlanBox - 12;
    const int bw = kPlanBox, bh = kPlanBox;
    area.fill_rect(bx, by, bx + bw, by + bh, core::Color::from(14, 16, 26));
    area.draw_rect(bx, by, bx + bw, by + bh, core::Color::from(70, 80, 100));
    const float s = 58.f / kDialR;
    const float cx = static_cast<float>(bx) + 65.f;
    const float cy = static_cast<float>(by) + 65.f;
    // the plan is a window, not a projection: a dawn tip can run three
    // hundred units west, and the paper ends at the box edge
    auto inside = [&](const float wx, const float wz)
    {
        const float ix = cx + wx * s;
        const float iy = cy + wz * s;
        return ix >= static_cast<float>(bx) + 1.f &&
               ix <= static_cast<float>(bx + bw) - 1.f &&
               iy >= static_cast<float>(by) + 1.f &&
               iy <= static_cast<float>(by + bh) - 1.f;
    };
    auto dot = [&](const float wx, const float wz, const int r,
                   const core::Color &c)
    {
        if (!inside(wx, wz))
        {
            return;
        }
        if (r <= 0)
        {
            area.draw_pixel(static_cast<int>(cx + wx * s),
                            static_cast<int>(cy + wz * s), c);
            return;
        }
        area.fill_circle(static_cast<int>(cx + wx * s),
                         static_cast<int>(cy + wz * s), r, c);
    };
    // plaza rim + the noon notch
    for (int k = 0; k < 48; ++k)
    {
        const float t = static_cast<float>(k) * (6.2831853f / 48.f);
        dot(kDialR * std::sin(t), kDialR * std::cos(t), 0,
            core::Color::from(70, 76, 92));
    }
    dot(0.f, -kDialR, 1, core::Color::from(150, 150, 160));
    // hour marks
    for (int hidx = 0; hidx <= 12; ++hidx)
    {
        const float th = kPi * static_cast<float>(hidx) / 12.f;
        dot(-std::cos(th) * kMarkR, -std::sin(th) * kMarkR, 0,
            core::Color::from(96, 100, 112));
    }
    // rings
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        const core::Color col = rung_[i] ? core::Color::from(230, 196, 96)
                                         : color_of(kPieceColor[i]);
        for (int k = 0; k < 10; ++k)
        {
            const float t = static_cast<float>(k) * (6.2831853f / 10.f);
            dot(targets_[i].x + targets_[i].r * std::sin(t),
                targets_[i].z + targets_[i].r * std::cos(t), 0, col);
        }
        dot(targets_[i].x, targets_[i].z, 1, col);
    }
    // the picked stone's full-day tip arc: the sweep it must ride
    if (selected_ >= 0)
    {
        const Piece &p = pieces_[selected_];
        const core::Color half(
            core::Color::from(static_cast<uint8_t>(kPieceColor[selected_].r / 2),
                              static_cast<uint8_t>(kPieceColor[selected_].g / 2),
                              static_cast<uint8_t>(kPieceColor[selected_].b / 2)));
        for (int k = 0; k <= 24; ++k)
        {
            const float h = 6.f + 12.f * static_cast<float>(k) / 24.f;
            float ax, az;
            tip_at_hour(p, h, ax, az);
            dot(ax, az, 0, half);
        }
    }
    // trails, stones, tips
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        for (int k = 0; k < trail_len_[i]; k += 2)
        {
            const int slot = (trail_head_[i] - 1 - k + kTrail * 2) % kTrail;
            dot(trail_[i][slot][0], trail_[i][slot][1], 0,
                core::Color::from(60, 64, 80));
        }
        const Piece &p = pieces_[i];
        dot(p.x, p.z, 2, color_of(kPieceColor[i]));
        if (i == selected_)
        {
            dot(p.x, p.z, 3, core::Color::from(255, 250, 240));
        }
        float tx, tz;
        tip_of(p, tx, tz);
        if (inside(p.x, p.z) && inside(tx, tz))
        {
            area.draw_line(static_cast<int>(cx + p.x * s),
                           static_cast<int>(cy + p.z * s),
                           static_cast<int>(cx + tx * s),
                           static_cast<int>(cy + tz * s),
                           core::Color::from(150, 155, 175));
        }
        dot(tx, tz, 1, core::Color::from(240, 242, 250));
    }
    // the tower
    dot(0.f, 0.f, 2, core::Color::from(160, 152, 136));
}

void DialCanvas::draw_timeline(core::Graphics &area) const
{
    const int x0 = 170;
    const int x1 = vw_ - 24;
    const int y0 = vh_ - 44;
    const int y1 = vh_ - 32;
    area.fill_rect(x0, y0, x1, y1, core::Color::from(16, 18, 30));
    if (!night_)
    {
        const int xm = (x0 + x1) / 2;
        area.fill_gradient(x0 + 1, y0 + 1, xm, y1 - 1,
                           core::Color::from(196, 120, 86),
                           core::Color::from(186, 208, 222));
        area.fill_gradient(xm, y0 + 1, x1 - 1, y1 - 1,
                           core::Color::from(186, 208, 222),
                           core::Color::from(214, 120, 80));
        const int sx = x0 + static_cast<int>(
                                static_cast<float>(x1 - x0) * (hour_now() - 6.f) /
                                12.f);
        area.fill_rect(sx - 1, y0, sx + 1, y1, core::Color::from(255, 244, 214));
        area.fill_circle(sx, y0 - 3, 3, core::Color::from(255, 214, 140));
    }
    else
    {
        area.fill_rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1,
                       core::Color::from(30, 34, 56));
    }
    area.draw_rect(x0, y0, x1, y1, core::Color::from(70, 80, 100));

    // hour ticks with a bell under each target
    for (int i = 0; i < kLevelCount[level_]; ++i)
    {
        const int tx = x0 + static_cast<int>(
                                static_cast<float>(x1 - x0) *
                                (static_cast<float>(targets_[i].hour) - 6.f) /
                                12.f);
        const core::Color col = rung_[i] ? core::Color::from(230, 196, 96)
                                         : color_of(kPieceColor[i]);
        area.fill_rect(tx - 1, y1 + 2, tx + 1, y1 + 7, col);
        const int by0 = y1 + 11;
        const int by1 = y1 + 19;
        if (rung_[i])
        {
            area.fill_circle(tx, (by0 + by1) / 2, 5, col);
            area.fill_circle(tx, (by0 + by1) / 2, 2,
                             core::Color::from(20, 18, 12));
        }
        else if (charge_[i] > 0)
        {
            const int w = 4 * charge_[i] / 50;
            area.fill_rect(tx - w, by0 + 3, tx + w, by1 - 3, col);
        }
        else
        {
            area.draw_circle(tx, (by0 + by1) / 2, 4, col);
        }
    }

    // height keys for the picked stone (chevrons, no text at this size)
    const int up_y = vh_ - kPlanBox - 12;
    const int dn_y = up_y + kBtnH + 6;
    const core::Color key = selected_ >= 0
                              ? core::Color::from(240, 234, 214)
                              : core::Color::from(70, 76, 92);
    area.fill_rect(kBtnX, up_y, kBtnX + kBtnW, up_y + kBtnH,
                   core::Color::from(24, 26, 40));
    area.fill_rect(kBtnX, dn_y, kBtnX + kBtnW, dn_y + kBtnH,
                   core::Color::from(24, 26, 40));
    area.draw_rect(kBtnX, up_y, kBtnX + kBtnW, up_y + kBtnH,
                   core::Color::from(50, 56, 74));
    area.draw_rect(kBtnX, dn_y, kBtnX + kBtnW, dn_y + kBtnH,
                   core::Color::from(50, 56, 74));
    for (int k = 0; k < 5; ++k)
    {
        area.fill_rect(kBtnX + 8 + k, up_y + 16 - k, kBtnX + 16 - k,
                       up_y + 17 - k, key);
        area.fill_rect(kBtnX + 8 + k, dn_y + 8 + k, kBtnX + 16 - k,
                       dn_y + 9 + k, key);
    }
    if (selected_ >= 0)
    {
        // a height gauge beside the keys: the stone's reach in the day
        const int gx0 = kBtnX + kBtnW + 8;
        const int gx1 = gx0 + 6;
        const int gy0 = up_y;
        const int gy1 = dn_y + kBtnH;
        area.fill_rect(gx0, gy0, gx1, gy1, core::Color::from(24, 26, 40));
        const float frac = (pieces_[selected_].h - kHLo) / (kHHi - kHLo);
        const int fy = gy1 - 2 - static_cast<int>(static_cast<float>(gy1 - gy0 - 4) * frac);
        area.fill_rect(gx0 + 1, fy, gx1 - 1, gy1 - 2,
                       color_of(kPieceColor[selected_]));
    }
}

void DialCanvas::draw_at(core::Graphics &area) const
{
    render_world();

    // one-call hand-off (U-8): the rgba32 build's Color IS the raster
    // word, so the block goes over as-is
    area.draw_surface(reinterpret_cast<const core::Color *>(rc_.pixels()), vw_,
                      vw_, vh_, 0, 0);

    draw_plan(area);
    draw_timeline(area);
}

void Hoursmith::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(hoursmith_font, hoursmith_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Hoursmith::sync_status()
{
    // every field the line shows, in its own digit slot -- a shared
    // additive key lets a high day collide with a low level (day 101 at
    // level 0 == day 1 at level 1) and freezes the line
    const int key = (((dial_->day() * 8 + dial_->level_bells()) * 2 +
                      (dial_->is_night() ? 1 : 0)) *
                     8) +
                    dial_->level();
    if (key == last_status_)
    {
        return;
    }
    char hint[96];
    dial_->level_text(hint, static_cast<int>(sizeof(hint)));
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%d/%d  %s", dial_->level_bells(),
                  dial_->level_targets(), hint);
    status_->set_text(buf);
    last_status_ = key;
}

void Hoursmith::create_window(const uint32_t max_client_width,
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
    root.set_background_color(core::Color::from(10, 12, 28));

    auto title = std::make_unique<Label>();
    title->set_text("HOURSMITH");
    title->set_text_color(kTitle);
    title->set_bold(true);
    title->set_size(150 * u, 24 * u);
    title->set_position(10 * u, 6 * u);
    title->set_font_size(9 * u);
    root.add_child(std::move(title));

    auto stat = std::make_unique<Label>();
    stat->set_text("0/1  DAY 1");
    stat->set_text_color(kStatus);
    // the line carries the level's goal: wide enough for a full hint,
    // stopping short of the RESET button
    stat->set_size(w - 241 * u, 20 * u);
    stat->set_position(165 * u, 8 * u);
    stat->set_font_size(8 * u);
    status_ = stat.get();
    root.add_child(std::move(stat));

    auto reset = std::make_unique<Button>();
    reset->set_text("RESET");
    reset->set_size(56 * u, 24 * u);
    reset->set_position(w - 68 * u, 6 * u);
    reset->set_font_size(7 * u);
    reset->set_text_color(core::Color::from(30, 24, 12));
    reset->set_background_color(kTitle);
    sub_reset_ = reset->clicked.subscribe([this]
                                          { dial_->reset(); });
    root.add_child(std::move(reset));

    auto dial = std::make_unique<DialCanvas>();
    const int ah = h - hud_h;
    dial->set_size(w, ah);
    dial->set_position(0, hud_h);
    dial->set_geometry(w, ah);
    dial_ = dial.get();
    root.add_child(std::move(dial));
}
