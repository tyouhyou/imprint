#include "fogkeeper.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "fogkeeper_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::fogkeeper;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    constexpr float clamp_f(const float v, const float lo, const float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // cold night; the fog band swallows the horizon
    const zb::raster::Rgba kSkyTop{6, 10, 26, 255};
    const zb::raster::Rgba kSkyBot{58, 62, 80, 255};
    const zb::raster::Rgba kSkyTopDawn{56, 48, 68, 255};
    const zb::raster::Rgba kSkyBotDawn{158, 112, 96, 255};
    const zb::raster::Rgba kSea{26, 40, 66, 255};
    const zb::raster::Rgba kBeamWarm{210, 196, 148, 255};
    // the air the far sea melts into (a haze ADDEND: multiply-only fog
    // drove the distance to black, and the horizon read as a hole)
    const zb::raster::Rgba kHaze{48, 52, 68, 255};

    const core::Color kTitle = core::Color::from(0xff, 0xd9, 0x8a);
    const core::Color kStatus = core::Color::from(0xd8, 0xe0, 0xf0);

    constexpr float kLampX = 0.f;
    constexpr float kLampY = 24.f;
    constexpr float kLampZ = -70.f;
    constexpr float kBeamDip = 0.1908f;  // sin(11 deg), fixed elevation
    constexpr float kBeamHalf = 0.15f;   // cone half-angle for the sea wash
    // the ship-lit cone is wider than the drawn one: a swept light
    // should catch a hull without pixel-perfect aiming
    constexpr float kShipCone = 0.26f;
    constexpr float kAimMax = 0.7f;  // azimuth clamp, rad

    constexpr int kRocks = 3;
    constexpr float kRockX[kRocks] = {-22.f, 18.f, -34.f};
    constexpr float kRockZ[kRocks] = {16.f, 34.f, 56.f};
    constexpr float kRockR[kRocks] = {5.5f, 6.5f, 4.5f};

    // the sea mesh: a FAN, not a rectangle -- each row's half-width
    // opens with distance so the mesh always outruns the frustum (a
    // rectangle's far edge lands inside the frame and reads as a wall
    // against the fog band). 14 bands == BeamCanvas::kRows.
    constexpr float kSeaZBand[15] = {-80.f, -56.f, -36.f, -18.f, 0.f, 16.f,
                                     32.f, 52.f, 76.f, 106.f, 144.f, 196.f,
                                     268.f, 360.f, 480.f};
    constexpr int kSeaCols = BeamCanvas::kCols;

    // column edge x at one row: the mesh fans from the eye line
    inline float sea_half_width(const float z)
    {
        return 0.84f * (z + 88.f) + 24.f;
    }
    inline float sea_x(const int col, const float z)
    {
        const float hw = sea_half_width(z);
        return -hw + 2.f * hw * static_cast<float>(col) /
                         static_cast<float>(kSeaCols);
    }

    constexpr float kGateX = 0.f;
    constexpr float kGateZ = -12.f;

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

    uint32_t shade(const zb::raster::Rgba &c, const float bright)
    {
        const int r = static_cast<int>(static_cast<float>(c.r) * bright);
        const int g = static_cast<int>(static_cast<float>(c.g) * bright);
        const int b = static_cast<int>(static_cast<float>(c.b) * bright);
        return zb::raster::pack(r > 255 ? 255 : r, g > 255 ? 255 : g,
                                b > 255 ? 255 : b);
    }
}  // namespace

unsigned BeamCanvas::hash(const unsigned a, const unsigned b) const
{
    unsigned h = a * 73856093u ^ b * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float BeamCanvas::beam_dir_x() const
{
    return std::sin(beam_) * std::sqrt(1.f - kBeamDip * kBeamDip);
}

float BeamCanvas::beam_dir_z() const
{
    return std::cos(beam_) * std::sqrt(1.f - kBeamDip * kBeamDip);
}

float BeamCanvas::beam_hit_x() const
{
    return kLampX + beam_dir_x() * (kLampY / kBeamDip);
}

float BeamCanvas::beam_hit_z() const
{
    return kLampZ + beam_dir_z() * (kLampY / kBeamDip);
}

float BeamCanvas::beam_factor(const float x, const float y, const float z,
                              const float half) const
{
    const float dx = x - kLampX, dy = y - kLampY, dz = z - kLampZ;
    const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dl < 1e-4f)
    {
        return 1.f;
    }
    const float dot = (dx * beam_dir_x() + dy * (-kBeamDip) + dz * beam_dir_z()) / dl;
    // angle from the beam axis, squared off at the rim
    const float ang = std::acos(clamp_f(dot, -1.f, 1.f));
    if (ang >= half)
    {
        return 0.f;
    }
    const float u = 1.f - ang / half;
    return u * u;
}

float BeamCanvas::fog(const float z) const
{
    return clamp_f(1.15f - (z + 80.f) / 360.f, 0.22f, 1.f);
}

void BeamCanvas::build_camera(zb::raster::Camera &cam) const
{
    // the keeper's eye just above and behind the lamp, pitched at the sea
    cam.ox = 0.f;
    cam.oy = 30.f;
    cam.oz = -88.f;
    const float len = std::sqrt(28.f * 28.f + 148.f * 148.f);
    cam.fx = 0.f;
    cam.fy = -28.f / len;
    cam.fz = 148.f / len;
    cam.rx = -1.f;
    cam.ry = 0.f;
    cam.rz = 0.f;
    cam.ux = 0.f;
    cam.uy = cam.fz;
    cam.uz = -cam.fy;
    cam.plane = 0.8f;
    cam.vw = vw_;
    cam.vh = vh_;
    cam.horizon = 0.30f * static_cast<float>(vh_);
    cam.near_clip = 0.5f;
    cam.depth = nullptr;  // painter's algorithm, back to front
}

void BeamCanvas::set_geometry(const int width, const int height)
{
    vw_ = width;
    vh_ = height;
    rc_.resize(vw_, vh_);
    rc_.set_replicate(2);
    mark_dirty();
}

void BeamCanvas::reset()
{
    frame_ = 0;
    night_over_ = 0;
    docked_ = 0;
    wrecks_ = 0;
    spawned_ = 0;
    next_spawn_ = 300;
    aim_ = 0.f;
    beam_ = 0.f;
    for (int i = 0; i < kMaxShips; ++i)
    {
        ships_[i] = Ship();
    }
    dragging_ = false;
    mark_dirty();
}

void BeamCanvas::tick()
{
    ++frame_;
    if (night_over_ != 0)
    {
        return;
    }
    if (frame_ >= static_cast<unsigned>(kNightFrames) || wrecks_ >= kWreckLimit)
    {
        night_over_ = 1;
        mark_dirty();
        return;
    }

    // the lamp swings after the aim: a heavy head on a friction ring
    beam_ += (aim_ - beam_) * 0.25f;

    // spawn from the fog on hash-gated frames, capped slots; the first
    // ship of the night always takes the middle lane (a readable opening)
    if (frame_ >= next_spawn_)
    {
        for (int i = 0; i < kMaxShips; ++i)
        {
            if (ships_[i].state == 0 || ships_[i].state == 3)
            {
                const unsigned h = hash(static_cast<unsigned>(spawned_), 7u);
                static constexpr float lanes[5] = {-48.f, -24.f, 0.f, 24.f, 48.f};
                ships_[i].x = spawned_ == 0 ? 0.f : lanes[h % 5u];
                ships_[i].z = 200.f;
                ships_[i].state = 1;
                ships_[i].sink_t = 0;
                ++spawned_;
                next_spawn_ = frame_ + 380u + hash(static_cast<unsigned>(spawned_), 11u) % 140u;
                break;
            }
        }
    }

    for (int i = 0; i < kMaxShips; ++i)
    {
        Ship &s = ships_[i];
        if (s.state == 2)
        {
            ++s.sink_t;
            if (s.sink_t > 120)
            {
                s.state = 3;
            }
            continue;
        }
        if (s.state != 1)
        {
            continue;
        }
        // a ship makes way only in the light; in the dark it drifts
        const float lit = beam_factor(s.x, 0.f, s.z, kShipCone);
        const float speed = 0.04f + 0.34f * lit;
        float tx = kGateX - s.x, tz = kGateZ - s.z;
        const float tl = std::sqrt(tx * tx + tz * tz);
        if (tl < 7.f)
        {
            s.state = 3;
            ++docked_;
            mark_dirty();
            continue;
        }
        tx /= tl;
        tz /= tl;
        s.x += tx * speed;
        s.z += tz * speed;

        for (int r = 0; r < kRocks; ++r)
        {
            const float dx = s.x - kRockX[r], dz = s.z - kRockZ[r];
            if (dx * dx + dz * dz < (kRockR[r] + 2.2f) * (kRockR[r] + 2.2f))
            {
                s.state = 2;
                s.sink_t = 0;
                ++wrecks_;
                if (wrecks_ >= kWreckLimit)
                {
                    night_over_ = 1;
                }
                mark_dirty();
                break;
            }
        }
    }
}

bool BeamCanvas::on_input(const zb::input::input_event &ev)
{
    switch (ev.type)
    {
    case zb::input::input_type::touch_down:
        dragging_ = true;
        last_x_ = ev.x;
        return true;
    case zb::input::input_type::touch_move:
        if (!dragging_)
        {
            return false;
        }
        aim_ = clamp_f(aim_ + static_cast<float>(ev.x - last_x_) * 0.0022f,
                       -kAimMax, kAimMax);
        last_x_ = ev.x;
        mark_dirty();
        return true;
    case zb::input::input_type::touch_up:
        dragging_ = false;
        return true;
    default:
        return false;
    }
}

void BeamCanvas::render_world() const
{
    zb::raster::Camera cam;
    build_camera(cam);

    const float dawn = clamp_f(static_cast<float>(frame_) /
                                   static_cast<float>(kNightFrames),
                               0.f, 1.f);

    // sky to fog band: the sea's far edge stops short of the true
    // horizon, so the gradient's bottom IS the haze the sea melts into
    const int dawn_i = static_cast<int>(100.f * dawn);
    rc_.fill_gradient_v(0, 0, vw_ - 1, vh_ - 1,
                        zb::raster::lerp_rgba(kSkyTop, kSkyTopDawn, dawn_i, 100),
                        zb::raster::lerp_rgba(kSkyBot, kSkyBotDawn, dawn_i, 100));

    draw_sea(cam);

    rc_.upscale();

    // the beam volume and the lights live at 1:1 on the surface
    rc_.set_replicate(1);
    draw_beam(cam);
    draw_lights(cam);
}

void BeamCanvas::draw_sea(const zb::raster::Camera &cam) const
{
    const float dawn = clamp_f(static_cast<float>(frame_) /
                                   static_cast<float>(kNightFrames),
                               0.f, 1.f);

    items_.clear();
    items_.reserve(static_cast<size_t>(kCols * kRows + 24));

    for (int row = 0; row < kRows; ++row)
    {
        for (int col = 0; col < kCols; ++col)
        {
            const float zm = 0.5f * (kSeaZBand[row] + kSeaZBand[row + 1]);
            const float xm = sea_x(col, zm) + 0.5f * (sea_x(col + 1, zm) - sea_x(col, zm));
            const float v = (xm - cam.ox) * cam.fx + (0.f - cam.oy) * cam.fy +
                            (zm - cam.oz) * cam.fz;
            DrawItem it;
            it.v = v;
            it.kind = 0;
            it.a = col;
            it.b = row;
            items_.push_back(it);
        }
    }

    // solids: three rocks (2 boxes each), harbor deck + two towers,
    // then up to kMaxShips ships (hull/cabin/mast as one solid each)
    for (int s = 0; s < 9 + kMaxShips; ++s)
    {
        float x = 0.f, y = 0.f, z = 0.f;
        solid_center(s, x, y, z);
        if (s >= 9 && z <= -1000.f)
        {
            continue;  // empty ship slot (the sentinel solid_center sets)
        }
        const float v = (x - cam.ox) * cam.fx + (y - cam.oy) * cam.fy +
                        (z - cam.oz) * cam.fz;
        DrawItem it;
        it.v = v;
        it.kind = 1 + s;
        it.a = 0;
        it.b = 0;
        items_.push_back(it);
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
        if (it.kind == 0)
        {
            draw_sea_quad(cam, it.a, it.b, dawn);
        }
        else
        {
            draw_solid(cam, it.kind - 1, dawn);
        }
    }
}

void BeamCanvas::draw_sea_quad(const zb::raster::Camera &cam, const int col,
                               const int row, const float dawn) const
{
    const float z0 = kSeaZBand[row];
    const float z1 = kSeaZBand[row + 1];
    const float x00 = sea_x(col, z0);
    const float x10 = sea_x(col + 1, z0);
    const float x01 = sea_x(col, z1);
    const float x11 = sea_x(col + 1, z1);
    const float xm = 0.25f * (x00 + x10 + x01 + x11);
    const float zm = 0.5f * (z0 + z1);

    const float fg = fog(zm);
    const float bf = beam_factor(xm, 0.f, zm, kBeamHalf);
    const unsigned sh = hash(static_cast<unsigned>(row * kCols + col),
                             static_cast<unsigned>(frame_) / 10u);
    // the shimmer fades with the fog: near rows are wide on screen and
    // full-amplitude sparkle read as vertical striping
    float bright = (0.85f + 0.06f * fg * (static_cast<float>(sh % 16u) / 16.f)) *
                       fg +
                   1.1f * bf;
    bright *= 1.f + 0.35f * dawn;

    // the wash lerps the slate sea toward lamplight inside the cone
    const float w = clamp_f(bf * fg * 0.85f, 0.f, 1.f);
    zb::raster::Rgba c{
        static_cast<int>(static_cast<float>(kSea.r) * (1.f - w) +
                         static_cast<float>(kBeamWarm.r) * w),
        static_cast<int>(static_cast<float>(kSea.g) * (1.f - w) +
                         static_cast<float>(kBeamWarm.g) * w),
        static_cast<int>(static_cast<float>(kSea.b) * (1.f - w) +
                         static_cast<float>(kBeamWarm.b) * w),
        255};
    // then the haze addend melts the distance into the sky band
    const zb::raster::Rgba lit{
        static_cast<int>(static_cast<float>(c.r) * bright),
        static_cast<int>(static_cast<float>(c.g) * bright),
        static_cast<int>(static_cast<float>(c.b) * bright), 255};
    const float hz = 1.f - fg;
    c.r = static_cast<int>(static_cast<float>(lit.r) * (1.f - hz) +
                           static_cast<float>(kHaze.r) * hz);
    c.g = static_cast<int>(static_cast<float>(lit.g) * (1.f - hz) +
                           static_cast<float>(kHaze.g) * hz);
    c.b = static_cast<int>(static_cast<float>(lit.b) * (1.f - hz) +
                           static_cast<float>(kHaze.b) * hz);

    zb::raster::Vtx corners[4];
    corners[0] = zb::raster::project(cam, x00, 0.f, z0);
    corners[1] = zb::raster::project(cam, x10, 0.f, z0);
    corners[2] = zb::raster::project(cam, x11, 0.f, z1);
    corners[3] = zb::raster::project(cam, x01, 0.f, z1);

    zb::raster::FaceShade shd;
    shd.base = shade(c, 1.f);
    shd.bright = 1.f;
    zb::raster::raster_quad(rc_, cam, corners, shd);
}

void BeamCanvas::solid_center(const int id, float &x, float &y, float &z) const
{
    x = 0.f;
    y = 0.f;
    z = 0.f;
    if (id < 6)
    {
        // rocks: two boxes each (base + head)
        const int r = id / 2;
        x = kRockX[r];
        z = kRockZ[r];
        y = id % 2 == 0 ? 1.f : 3.2f;
    }
    else if (id < 9)
    {
        // harbor: deck, west tower, east tower
        if (id == 6)
        {
            x = 0.f;
            y = 0.4f;
            z = -14.f;
        }
        else
        {
            x = id == 7 ? -13.f : 13.f;
            y = 3.5f;
            z = -14.f;
        }
    }
    else
    {
        const int si = id - 9;
        if (ships_[si].state == 0 || ships_[si].state == 3)
        {
            z = -1000.f;  // the empty-slot sentinel draw_sea checks
            return;
        }
        x = ships_[si].x;
        z = ships_[si].z;
        y = 0.f;
        if (ships_[si].state == 2)
        {
            y = -2.5f * static_cast<float>(ships_[si].sink_t) / 120.f;
        }
    }
}

void BeamCanvas::draw_solid(const zb::raster::Camera &cam, const int id,
                            const float dawn) const
{
    zb::raster::BoxPaint paint;
    paint.light[0] = 0.3f;
    paint.light[1] = 0.8f;
    paint.light[2] = 0.5f;
    paint.ambient = 0.62f;
    paint.lambert = 0.45f;
    paint.hemi = 0.3f;
    paint.rim = 0.1f;

    zb::raster::FaceStyle face[6];
    const auto slab = [&](const float cx, const float cy, const float cz,
                          const float hx, const float hy, const float hz,
                          const zb::raster::Rgba &col, const float bright)
    {
        float corners[24];
        box_corners(cx, cy, cz, hx, hy, hz, corners);
        const uint32_t side = shade(col, bright * 0.8f);
        const uint32_t top = shade(col, bright);
        for (int f = 0; f < 6; ++f)
        {
            face[f].base = f == 2 ? top : side;
            face[f].grad = f == 2 ? 0.f : 0.25f;
        }
        zb::raster::draw_box(rc_, cam, corners, face, paint);
    };

    float x = 0.f, y = 0.f, z = 0.f;
    solid_center(id, x, y, z);

    if (id < 6)
    {
        const int r = id / 2;
        const float fg = fog(z);
        const float bf = beam_factor(x, y, z, kBeamHalf);
        slab(x, y, z, id % 2 == 0 ? kRockR[r] : kRockR[r] * 0.7f,
             id % 2 == 0 ? 0.8f : 1.3f,
             id % 2 == 0 ? kRockR[r] : kRockR[r] * 0.7f,
             zb::raster::Rgba{44, 46, 56, 255}, (0.75f + 1.3f * bf) * fg);
        return;
    }
    if (id < 9)
    {
        const float fg = fog(z);
        if (id == 6)
        {
            slab(x, y, z, 13.f, 0.5f, 3.5f, zb::raster::Rgba{58, 48, 40, 255},
                 1.f * fg);
        }
        else
        {
            slab(x, y, z, 1.6f, 3.5f, 1.6f, zb::raster::Rgba{52, 50, 54, 255},
                 1.f * fg);
        }
        return;
    }

    // a ship: hull, cabin, mast -- warm when the beam holds it
    const int si = id - 9;
    const Ship &s = ships_[si];
    const float fg = fog(z);
    const float bf = beam_factor(x, 0.f, z, kBeamHalf);
    const float bright = (0.5f + 1.5f * bf) * fg * (1.f + 0.2f * dawn);
    const zb::raster::Rgba hull{bf > 0.05f ? 120 : 44, bf > 0.05f ? 96 : 52,
                                bf > 0.05f ? 72 : 66, 255};
    const float sink = s.state == 2
                         ? -2.5f * static_cast<float>(s.sink_t) / 120.f
                         : 0.f;
    slab(x, 0.5f + sink, z, 1.1f, 0.8f, 2.75f, hull, bright);
    slab(x, 1.9f + sink, z, 0.8f, 0.7f, 0.9f,
         zb::raster::Rgba{64, 58, 54, 255}, bright);
    slab(x, 3.4f + sink, z, 0.15f, 1.5f, 0.15f,
         zb::raster::Rgba{30, 30, 36, 255}, bright);
}

void BeamCanvas::draw_beam(const zb::raster::Camera &cam) const
{
    // the beam volume: three nested translucent pyramids from the lamp
    // to the water (the contact-shadow blend path). Layered low-alpha
    // shells read as a soft light; one shell at full strength reads as
    // a glass pane with hard edges.
    const float t = kLampY / kBeamDip;
    const float dx = beam_dir_x(), dz = beam_dir_z();
    const float e1x = dz, e1z = -dx;  // horizontal axis across the beam

    const float hx = kLampX + dx * t, hz = kLampZ + dz * t;
    static constexpr float kApertures[3] = {13.f, 8.5f, 4.5f};
    static constexpr float kAlphas[3] = {0.055f, 0.055f, 0.06f};

    for (int shell = 0; shell < 3; ++shell)
    {
        const float ap0 = 0.4f + 0.2f * static_cast<float>(shell);
        const float ap1 = kApertures[shell];
        const zb::raster::Vtx lamp_a = zb::raster::project(
            cam, kLampX + e1x * ap0, kLampY, kLampZ + e1z * ap0);
        const zb::raster::Vtx lamp_b = zb::raster::project(
            cam, kLampX - e1x * ap0, kLampY, kLampZ - e1z * ap0);
        const zb::raster::Vtx lamp_c =
            zb::raster::project(cam, kLampX, kLampY + ap0, kLampZ);
        const zb::raster::Vtx lamp_d =
            zb::raster::project(cam, kLampX, kLampY - ap0, kLampZ);
        const zb::raster::Vtx far_a =
            zb::raster::project(cam, hx + e1x * ap1, 0.f, hz + e1z * ap1);
        const zb::raster::Vtx far_b =
            zb::raster::project(cam, hx - e1x * ap1, 0.f, hz - e1z * ap1);
        const zb::raster::Vtx far_c =
            zb::raster::project(cam, hx, ap1 * 0.8f, hz);
        const zb::raster::Vtx far_d =
            zb::raster::project(cam, hx, -ap1 * 0.8f, hz);

        zb::raster::FaceShade shd;
        shd.base = zb::raster::pack(255, 244, 200);
        shd.bright = 1.f;
        shd.alpha = kAlphas[shell];

        const zb::raster::Vtx quads[4][4] = {
            {lamp_a, lamp_b, far_b, far_a},
            {lamp_c, lamp_d, far_d, far_c},
            {lamp_a, lamp_c, far_c, far_a},
            {lamp_b, lamp_d, far_d, far_b},
        };
        for (int q = 0; q < 4; ++q)
        {
            zb::raster::raster_quad(rc_, cam, quads[q], shd);
        }
    }
}

void BeamCanvas::draw_lights(const zb::raster::Camera &cam) const
{
    // stars in the sky band, twinkling
    for (int k = 0; k < 18; ++k)
    {
        const unsigned h = hash(static_cast<unsigned>(k), 3u);
        const int sx = static_cast<int>(static_cast<float>(h % 1024u) / 1024.f *
                                        static_cast<float>(vw_));
        const int sy = static_cast<int>(static_cast<float>((h >> 10) % 256u) /
                                        256.f * static_cast<float>(vh_) * 0.14f);
        const unsigned tw = hash(static_cast<unsigned>(k),
                                 static_cast<unsigned>(frame_) / 12u);
        rc_.add_pixel(sx, sy, 200, 210, 235,
                      70 + static_cast<int>(tw % 3u) * 40);
    }

    // the lamp itself and the pool where the beam meets the water
    const zb::raster::Vtx lv = zb::raster::project(cam, kLampX, kLampY, kLampZ);
    rc_.glow(static_cast<int>(lv.sx), static_cast<int>(lv.sy), 14, 255, 240,
             200, 0.7f);
    const zb::raster::Vtx hv =
        zb::raster::project(cam, beam_hit_x(), 0.f, beam_hit_z());
    if (hv.sy > 0.f && hv.sy < static_cast<float>(vh_))
    {
        rc_.glow(static_cast<int>(hv.sx), static_cast<int>(hv.sy), 22, 255,
                 236, 190, 0.4f);
    }

    // harbor gate beacon
    const zb::raster::Vtx gv = zb::raster::project(cam, kGateX, 6.f, kGateZ);
    const float pulse =
        0.55f + 0.35f * std::sin(static_cast<float>(frame_) * 0.09f);
    rc_.glow(static_cast<int>(gv.sx), static_cast<int>(gv.sy), 14, 110, 235,
             150, pulse);

    // wrecks burn red while they go down; docked pops green at the gate
    for (int i = 0; i < kMaxShips; ++i)
    {
        const Ship &s = ships_[i];
        if (s.state == 2)
        {
            const zb::raster::Vtx sv = zb::raster::project(cam, s.x, 1.f, s.z);
            rc_.glow(static_cast<int>(sv.sx), static_cast<int>(sv.sy), 12, 255,
                     90, 60, 0.5f);
        }
        else if (s.state == 1 && beam_factor(s.x, 0.f, s.z, kShipCone) > 0.f)
        {
            const zb::raster::Vtx sv = zb::raster::project(cam, s.x, 4.5f, s.z);
            rc_.glow(static_cast<int>(sv.sx), static_cast<int>(sv.sy), 6, 255,
                     220, 150, 0.5f);
        }
    }
}

void BeamCanvas::draw_at(core::Graphics &area) const
{
    render_world();

    // one-call hand-off (U-8): the rgba32 build's Color IS the raster
    // word, so the block goes over as-is
    area.draw_surface(reinterpret_cast<const core::Color *>(rc_.pixels()), vw_,
                      vw_, vh_, 0, 0);

    // beam azimuth dial, bottom center: a tick strip with the aim mark
    const int cx = vw_ / 2;
    const int cy = vh_ - 26;
    area.fill_rect(cx - 90, cy, cx + 90, cy + 8, core::Color::from(10, 16, 30));
    for (int k = -5; k <= 5; ++k)
    {
        const int tx = cx + k * 18;
        area.fill_rect(tx, cy + (k % 2 == 0 ? 0 : 3), tx, cy + 8,
                       core::Color::from(70, 84, 104));
    }
    const int ax = cx + static_cast<int>(aim_ / kAimMax * 88.f);
    area.fill_rect(ax - 1, cy - 2, ax + 1, cy + 10,
                   core::Color::from(255, 214, 130));
}

void Fogkeeper::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(fogkeeper_font, fogkeeper_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Fogkeeper::sync_status()
{
    const int key = bay_->docked() * 8 + bay_->wrecks() * 2 +
                    (bay_->night_over() ? 1 : 0);
    if (key == last_status_)
    {
        return;
    }
    char buf[96];
    if (bay_->night_over())
    {
        std::snprintf(buf, sizeof(buf), "DAWN - %d DOCKED, %d WRECKED",
                      bay_->docked(), bay_->wrecks());
    }
    else
    {
        std::snprintf(buf, sizeof(buf),
                      "DOCKED %d   WRECKED %d   (3 WRECKS END THE WATCH)",
                      bay_->docked(), bay_->wrecks());
    }
    status_->set_text(buf);
    last_status_ = key;
}

void Fogkeeper::create_window(const uint32_t max_client_width,
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
    root.set_background_color(core::Color::from(6, 10, 26));

    auto title = std::make_unique<Label>();
    title->set_text("FOGKEEPER");
    title->set_text_color(kTitle);
    title->set_bold(true);
    title->set_size(150 * u, 24 * u);
    title->set_position(10 * u, 6 * u);
    title->set_font_size(9 * u);
    root.add_child(std::move(title));

    auto stat = std::make_unique<Label>();
    stat->set_text("DOCKED 0   WRECKED 0");
    stat->set_text_color(kStatus);
    stat->set_size(w - 300 * u, 20 * u);
    stat->set_position(165 * u, 8 * u);
    stat->set_font_size(8 * u);
    status_ = stat.get();
    root.add_child(std::move(stat));

    auto reset = std::make_unique<Button>();
    reset->set_text("RESET");
    reset->set_size(56 * u, 24 * u);
    reset->set_position(w - 68 * u, 6 * u);
    reset->set_font_size(7 * u);
    reset->set_text_color(core::Color::from(30, 20, 8));
    reset->set_background_color(kTitle);
    sub_reset_ = reset->clicked.subscribe([this]
                                          { bay_->reset(); });
    root.add_child(std::move(reset));

    auto bay = std::make_unique<BeamCanvas>();
    const int ah = h - hud_h;
    bay->set_size(w, ah);
    bay->set_position(0, hud_h);
    bay->set_geometry(w, ah);
    bay_ = bay.get();
    root.add_child(std::move(bay));
}
