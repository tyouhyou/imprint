#include "moonpull.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "moonpull_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

using namespace zb::app::moonpull;
using namespace zb::ui;

namespace
{
    constexpr float kDeg = 0.017453292519943295f;  // pi / 180
    constexpr float kTau = 6.283185307179586f;

    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    constexpr float clamp_f(const float v, const float lo, const float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // night palette; dawn lerps the sky and lifts the water
    const zb::raster::Rgba kSkyTop{6, 8, 22, 255};
    const zb::raster::Rgba kSkyBot{30, 38, 62, 255};
    const zb::raster::Rgba kSkyTopDawn{64, 54, 74, 255};
    const zb::raster::Rgba kSkyBotDawn{148, 104, 92, 255};
    const zb::raster::Rgba kWaterDeep{10, 26, 54, 255};
    const zb::raster::Rgba kWaterCrest{76, 130, 140, 255};

    const core::Color kTitle = core::Color::from(0x9f, 0xd8, 0xff);
    const core::Color kStatus = core::Color::from(0xd8, 0xe4, 0xf4);

    struct Site
    {
        float x, z;
    };

    Site site_at(const float deg, const float r)
    {
        const float a = deg * kDeg;
        return Site{r * std::sin(a), r * std::cos(a)};
    }

    // a solid's color word with the given brightness baked in
    uint32_t shade(const zb::raster::Rgba &c, const float bright)
    {
        const int r = static_cast<int>(static_cast<float>(c.r) * bright);
        const int g = static_cast<int>(static_cast<float>(c.g) * bright);
        const int b = static_cast<int>(static_cast<float>(c.b) * bright);
        return zb::raster::pack(r > 255 ? 255 : r, g > 255 ? 255 : g,
                                b > 255 ? 255 : b);
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
}  // namespace

unsigned TideCanvas::hash(const unsigned a, const unsigned b) const
{
    unsigned h = a * 73856093u ^ b * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float TideCanvas::strength() const
{
    // a low moon pulls harder: 4.4 at kMoonHLo down to 1.4 at kMoonHHi
    return 4.4f - (moon_h_ - kMoonHLo) * (3.0f / (kMoonHHi - kMoonHLo));
}
float TideCanvas::tide(const float x, const float z) const
{
    const float r = std::sqrt(x * x + z * z);
    const float g = clamp_f(r / kSeaR, 0.f, 1.f);
    // two crests along the moon line, two troughs between them; the
    // bulge grows toward the coast (g^1.5), where it can be used
    const float amp = strength() * g * std::sqrt(g);
    const float dth = std::atan2(x, z) - moon_phi_;
    return amp * std::cos(2.f * dth);
}

float TideCanvas::tide_at(const float deg, const float r) const
{
    const Site s = site_at(deg, r);
    return tide(s.x, s.z);
}

void TideCanvas::build_camera(zb::raster::Camera &cam) const
{
    // eye south of the disc, pitched down at the center; the basis is
    // orthonormal by construction (up = cross(right, forward))
    cam.ox = 0.f;
    cam.oy = 170.f;
    cam.oz = -180.f;
    const float len = std::sqrt(170.f * 170.f + 180.f * 180.f);
    cam.fx = 0.f;
    cam.fy = -170.f / len;
    cam.fz = 180.f / len;
    cam.rx = -1.f;
    cam.ry = 0.f;
    cam.rz = 0.f;
    cam.ux = 0.f;
    cam.uy = cam.fz;
    cam.uz = -cam.fy;
    cam.plane = 0.74f;
    cam.vw = vw_;
    cam.vh = vh_;
    cam.horizon = 0.36f * static_cast<float>(vh_);
    cam.near_clip = 0.5f;
    cam.depth = nullptr;  // painter's algorithm, back to front
}

void TideCanvas::set_geometry(const int width, const int height)
{
    vw_ = width;
    vh_ = height;
    rc_.resize(vw_, vh_);
    rc_.set_replicate(2);

    // the moon sphere once, at unit radius; the draw copies and places
    // it (verts are public so the caller transforms in place)
    moon_model_.reset();
    moon_model_.add_sphere(0.f, 0.f, 0.f, 1.f, 12, 8, 0, 1.f);
    mark_dirty();
}

void TideCanvas::reset()
{
    frame_ = 0;
    quest_ = Quest::FerryWait;
    night_over_ = 0;
    ferry_done_ = false;
    pearls_done_ = false;
    lantern_done_ = false;
    lantern_failed_ = false;
    ferry_wait_ = 0;
    pearl_dry_ = 0;
    lantern_dry_ = 0;
    ferry_t_ = 0.f;
    // parked off the pass and middling: the first commission needs both
    // an orbit and a lowering before the crest runs deep enough
    moon_phi_ = 0.55f;
    moon_h_ = 12.f;
    dragging_ = false;
    mark_dirty();
}

int TideCanvas::quests_done() const
{
    int n = 0;
    if (ferry_done_)
    {
        ++n;
    }
    if (pearls_done_)
    {
        ++n;
    }
    if (lantern_done_)
    {
        ++n;
    }
    return n;
}

void TideCanvas::quest_text(char *buf, const int cap) const
{
    switch (quest_)
    {
    case Quest::FerryWait:
        std::snprintf(buf, static_cast<size_t>(cap),
                      "1/3  PARK A CREST ON THE REEF PASS - THE FERRY WAITS");
        break;
    case Quest::FerryCross:
        std::snprintf(buf, static_cast<size_t>(cap), "1/3  THE FERRY IS CROSSING");
        break;
    case Quest::Pearls:
        std::snprintf(buf, static_cast<size_t>(cap),
                      "2/3  HOLD A TROUGH OVER THE PEARLER'S BED");
        break;
    case Quest::Lantern:
        std::snprintf(buf, static_cast<size_t>(cap),
                      "3/3  KEEP THE LANTERN ISLET DRY WHILE IT BURNS");
        break;
    case Quest::Done:
        if (lantern_failed_)
        {
            std::snprintf(buf, static_cast<size_t>(cap),
                          "THE ISLET FLOODED - %d/3 COMMISSIONS", quests_done());
        }
        else
        {
            std::snprintf(buf, static_cast<size_t>(cap), "ALL COMMISSIONS KEPT");
        }
        break;
    }
}

void TideCanvas::tick()
{
    // the frame counter keeps breathing after the night ends (the end
    // screen's shimmer and beacon pulses stay alive); only the quest
    // state freezes
    ++frame_;
    if (night_over_ != 0)
    {
        return;
    }
    if (frame_ >= static_cast<unsigned>(kNightFrames))
    {
        night_over_ = 1;
        mark_dirty();
        return;
    }

    const float sill = tide_at(kSill, kCoastR);
    const float bed = tide_at(kPearls, kBedR);
    const float islet = tide_at(kLantern, kCoastR);

    switch (quest_)
    {
    case Quest::FerryWait:
        ferry_wait_ = sill >= 3.6f ? ferry_wait_ + 1 : 0;
        if (ferry_wait_ >= 30)
        {
            quest_ = Quest::FerryCross;
            mark_dirty();
        }
        break;
    case Quest::FerryCross:
        ferry_t_ += 1.f / 240.f;
        if (ferry_t_ >= 1.f)
        {
            ferry_t_ = 1.f;
            ferry_done_ = true;
            quest_ = Quest::Pearls;
            mark_dirty();
        }
        break;
    case Quest::Pearls:
        pearl_dry_ = bed <= -1.5f ? pearl_dry_ + 1 : 0;
        if (pearl_dry_ >= 90)
        {
            pearls_done_ = true;
            quest_ = Quest::Lantern;
            mark_dirty();
        }
        break;
    case Quest::Lantern:
        if (islet >= 2.8f)
        {
            lantern_failed_ = true;
            night_over_ = 2;
            mark_dirty();
            return;
        }
        lantern_dry_ = islet < 1.4f ? lantern_dry_ + 1 : 0;
        if (lantern_dry_ >= 1500)
        {
            lantern_done_ = true;
            quest_ = Quest::Done;
            night_over_ = 3;
            mark_dirty();
        }
        break;
    case Quest::Done:
        break;
    }
}

bool TideCanvas::on_input(const zb::input::input_event &ev)
{
    switch (ev.type)
    {
    case zb::input::input_type::touch_down:
        dragging_ = true;
        last_x_ = ev.x;
        last_y_ = ev.y;
        return true;
    case zb::input::input_type::touch_move:
        if (!dragging_)
        {
            return false;
        }
        {
            // sideways drag orbits the moon, vertical drag raises it
            // (low = strong pull)
            moon_phi_ -= static_cast<float>(ev.x - last_x_) * 0.005f;
            moon_h_ = clamp_f(moon_h_ - static_cast<float>(ev.y - last_y_) * 0.1f,
                              kMoonHLo, kMoonHHi);
            last_x_ = ev.x;
            last_y_ = ev.y;
        }
        mark_dirty();
        return true;
    case zb::input::input_type::touch_up:
        dragging_ = false;
        return true;
    default:
        return false;
    }
}

void TideCanvas::render_world() const
{
    zb::raster::Camera cam;
    build_camera(cam);

    // sky, then the sea paints over it (the disc's rim leaves the sky
    // visible past the top corners)
    const int dawn_i = static_cast<int>(
        100.f * clamp_f(static_cast<float>(frame_) /
                            static_cast<float>(kNightFrames),
                        0.f, 1.f));
    rc_.fill_gradient_v(0, 0, vw_ - 1, vh_ - 1,
                        zb::raster::lerp_rgba(kSkyTop, kSkyTopDawn, dawn_i, 100),
                        zb::raster::lerp_rgba(kSkyBot, kSkyBotDawn, dawn_i, 100));

    draw_water(cam);

    rc_.upscale();

    // the moon and every light live at 1:1 on the surface, past the tent
    rc_.set_replicate(1);
    draw_moon_and_lights(cam);
}

// world-space center of solid id (shared by the painter sort and the
// draw itself)
void TideCanvas::solid_center(const int id, float &x, float &y, float &z) const
{
    switch (id)
    {
    case 0:
    {
        const Site a = site_at(kDockA, kCoastR);
        const Site b = site_at(kDockB, kCoastR);
        x = a.x + (b.x - a.x) * ferry_t_;
        z = a.z + (b.z - a.z) * ferry_t_;
        y = tide(x, z);
        break;
    }
    case 1:
    {
        const Site s = site_at(kPearls, kBedR);
        x = s.x;
        z = s.z;
        y = tide(x, z);
        break;
    }
    case 2:
    {
        const Site s = site_at(kLantern, kCoastR);
        x = s.x;
        z = s.z;
        y = tide(x, z);
        break;
    }
    case 3:
    case 4:
    {
        const Site s = site_at(id == 3 ? kDockA : kDockB, kCoastR + 4.f);
        x = s.x;
        z = s.z;
        y = tide(x, z);
        break;
    }
    case 5:
    {
        const Site s = site_at(60.f, 98.f);
        x = s.x;
        z = s.z;
        y = tide(x, z);
        break;
    }
    case 6:
    {
        const Site s = site_at(300.f, 100.f);
        x = s.x;
        z = s.z;
        y = tide(x, z);
        break;
    }
    default:  // 7: the reef bar at the pass
    {
        const Site s = site_at(kSill, kCoastR);
        x = s.x;
        z = s.z;
        y = tide(x, z);
        break;
    }
    }
}

void TideCanvas::draw_water(const zb::raster::Camera &cam) const
{
    items_.clear();
    items_.reserve(static_cast<size_t>(kRings * kSectors + 8));

    const float A = strength();
    for (int band = 0; band < kRings; ++band)
    {
        for (int sec = 0; sec < kSectors; ++sec)
        {
            const float r0 = kRingR[band];
            const float r1 = kRingR[band + 1];
            const float rm = 0.5f * (r0 + r1);
            const float tm = (static_cast<float>(sec) + 0.5f) *
                             (kTau / static_cast<float>(kSectors));
            const float xm = rm * std::sin(tm);
            const float zm = rm * std::cos(tm);
            // depth of the quad center along the camera forward
            const float v = (xm - cam.ox) * cam.fx + (0.f - cam.oy) * cam.fy +
                            (zm - cam.oz) * cam.fz;

            DrawItem it;
            it.v = v;
            it.kind = 0;
            it.a = band;
            it.b = sec;
            items_.push_back(it);
        }
    }

    for (int s = 0; s < 8; ++s)
    {
        float x = 0.f, y = 0.f, z = 0.f;
        solid_center(s, x, y, z);
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
            draw_water_quad(cam, it.a, it.b, A);
        }
        else
        {
            draw_solid(cam, it.kind - 1);
        }
    }
}

void TideCanvas::draw_water_quad(const zb::raster::Camera &cam, const int band,
                                 const int sec, const float A) const
{
    const float r0 = kRingR[band];
    const float r1 = kRingR[band + 1];
    const float t0 = static_cast<float>(sec) *
                     (kTau / static_cast<float>(kSectors));
    const float t1 = static_cast<float>(sec + 1) *
                     (kTau / static_cast<float>(kSectors));
    const float x00 = r0 * std::sin(t0), z00 = r0 * std::cos(t0);
    const float x01 = r0 * std::sin(t1), z01 = r0 * std::cos(t1);
    const float x10 = r1 * std::sin(t0), z10 = r1 * std::cos(t0);
    const float x11 = r1 * std::sin(t1), z11 = r1 * std::cos(t1);
    const float h00 = r0 > 0.f ? tide(x00, z00) : 0.f;
    const float h01 = r0 > 0.f ? tide(x01, z01) : 0.f;
    const float h10 = tide(x10, z10);
    const float h11 = tide(x11, z11);
    const float hm = 0.25f * (h00 + h01 + h10 + h11);

    // color by height with a shaped response (the flat cos(2dth) wash
    // lit half the sea; the curve keeps mid-tide water mid-blue and
    // only the bulge crests read light), sheen toward the moon, and a
    // slow hash-driven shimmer so the sea breathes
    const float th = clamp_f(hm / A, -1.f, 1.f);
    const float shaped = (th < 0.f ? -1.f : 1.f) *
                         std::pow(std::abs(th), 2.2f);
    const float hn = clamp_f(0.5f + 0.5f * shaped, 0.f, 1.f);
    const float tm = 0.5f * (t0 + t1);
    const float cosd = std::cos(tm - moon_phi_);
    float bright = 0.86f + 0.22f * (hn - 0.5f) +
                   0.2f * cosd * cosd * cosd * clamp_f(hm / A, 0.f, 1.f);
    const unsigned sh = hash(static_cast<unsigned>(band * kSectors + sec),
                             static_cast<unsigned>(frame_) / 8u);
    bright *= 0.985f + 0.03f * (static_cast<float>(sh % 16u) / 16.f);

    const zb::raster::Rgba c{
        static_cast<int>(static_cast<float>(kWaterDeep.r) * (1.f - hn) +
                         static_cast<float>(kWaterCrest.r) * hn),
        static_cast<int>(static_cast<float>(kWaterDeep.g) * (1.f - hn) +
                         static_cast<float>(kWaterCrest.g) * hn),
        static_cast<int>(static_cast<float>(kWaterDeep.b) * (1.f - hn) +
                         static_cast<float>(kWaterCrest.b) * hn),
        255};

    zb::raster::Vtx corners[4];
    corners[0] = zb::raster::project(cam, x00, h00, z00);
    corners[1] = zb::raster::project(cam, x01, h01, z01);
    corners[2] = zb::raster::project(cam, x11, h11, z11);
    corners[3] = zb::raster::project(cam, x10, h10, z10);

    zb::raster::FaceShade shd;
    shd.base = shade(c, bright);
    shd.bright = 1.f;
    zb::raster::raster_quad(rc_, cam, corners, shd);
}

void TideCanvas::draw_solid(const zb::raster::Camera &cam, const int id) const
{
    // shared light: the moon swings the shade, but it sits near the
    // horizon (a top face would see n.L ~ 0 and everything would be
    // flat ambient), so the lamp is the moon blended with a sky fill
    const float moonx = kMoonR * std::sin(moon_phi_);
    const float moonz = kMoonR * std::cos(moon_phi_);
    float lx = moonx, ly = moon_h_ + 0.55f * kMoonR, lz = moonz;
    const float mlen = std::sqrt(lx * lx + ly * ly + lz * lz);
    zb::raster::BoxPaint paint;
    paint.light[0] = lx / mlen;
    paint.light[1] = ly / mlen;
    paint.light[2] = lz / mlen;
    paint.ambient = 0.66f;
    paint.lambert = 0.42f;
    paint.hemi = 0.25f;
    paint.rim = 0.12f;

    zb::raster::FaceStyle face[6];
    const auto slab = [&](const float cx, const float base, const float cz,
                          const float hx, const float hy, const float hz,
                          const zb::raster::Rgba &col, const float bright)
    {
        float corners[24];
        box_corners(cx, base + hy, cz, hx, hy, hz, corners);
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

    switch (id)
    {
    case 0:  // the ferry rides the tide along the dock chord
        slab(x, y - 0.3f, z, 4.5f, 1.f, 1.8f, zb::raster::Rgba{176, 70, 52, 255},
             1.f);
        slab(x, y + 0.7f, z, 1.6f, 1.f, 1.3f,
             zb::raster::Rgba{224, 214, 192, 255}, 1.f);
        break;
    case 1:  // pearl bed: a low slab the trough can bare
    {
        const bool dry = y < 0.4f;
        slab(x, -0.6f, z, 5.f, 1.f, 3.f,
             zb::raster::Rgba{dry ? 128 : 92, dry ? 104 : 74, dry ? 150 : 96, 255},
             dry ? 1.15f : 0.85f);
        break;
    }
    case 2:  // lantern islet: rock, post, lamp
        slab(x, y - 0.5f, z, 4.5f, 2.f, 4.5f, zb::raster::Rgba{58, 56, 66, 255},
             1.f);
        slab(x, y + 1.5f, z, 0.5f, 2.5f, 0.5f, zb::raster::Rgba{40, 38, 46, 255},
             1.f);
        slab(x, y + 6.f, z, 1.f, 1.f, 1.f, zb::raster::Rgba{255, 190, 90, 255},
             1.3f);
        break;
    case 3:  // dock piers
    case 4:
        slab(x, y + 0.4f, z, 3.f, 0.6f, 6.f, zb::raster::Rgba{96, 72, 48, 255},
             1.f);
        break;
    case 5:  // rock islets
        slab(x, y - 0.5f, z, 4.f, 2.5f, 3.4f, zb::raster::Rgba{54, 52, 62, 255},
             1.f);
        break;
    case 6:
        slab(x, y - 0.5f, z, 5.f, 2.f, 4.f, zb::raster::Rgba{50, 50, 60, 255},
             1.f);
        break;
    default:  // 7: the reef bar the ferry's pass runs over
        slab(x, y - 1.6f, z, 6.f, 1.f, 2.5f, zb::raster::Rgba{64, 78, 84, 255},
             1.f);
        break;
    }
}

void TideCanvas::draw_moon_and_lights(const zb::raster::Camera &cam) const
{
    // ---- the moon, at 1:1 on the surface -------------------------
    const float moonx = kMoonR * std::sin(moon_phi_);
    const float moonz = kMoonR * std::cos(moon_phi_);
    constexpr float kMoonRad = 9.f;

    moon_world_.reset();
    moon_world_.verts = moon_model_.verts;
    moon_world_.tris = moon_model_.tris;
    for (size_t i = 0; i < moon_world_.verts.size(); ++i)
    {
        zb::raster::MeshVert &vt = moon_world_.verts[i];
        vt.x = moonx + vt.x * kMoonRad;
        vt.y = moon_h_ + vt.y * kMoonRad;
        vt.z = moonz + vt.z * kMoonRad;
    }

    zb::raster::MeshPaint mp;
    // light from the eye: the camera-facing hemisphere is lit, the limb
    // falls to ambient -- a plain sphere reads round
    const float dx = moonx - cam.ox, dy = moon_h_ - cam.oy,
                dz = moonz - cam.oz;
    const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
    mp.light_x = dx / dl;
    mp.light_y = dy / dl;
    mp.light_z = dz / dl;
    mp.ambient = 0.72f;
    mp.lambert = 0.3f;
    mp.hemi = 0.f;
    mp.rim = 0.18f;
    mp.fog_dist = 1e6f;
    static const uint32_t moon_col[1] = {zb::raster::pack(242, 232, 205)};
    mp.part_colors = moon_col;
    mp.part_count = 1;
    rc_.reset_mesh_depth();
    zb::raster::draw_mesh(rc_, cam, moon_world_, mp);

    const zb::raster::Vtx mv = zb::raster::project(cam, moonx, moon_h_, moonz);
    rc_.glow(static_cast<int>(mv.sx), static_cast<int>(mv.sy), 30, 255, 240,
             210, 0.65f);

    // ---- glitter along the crest corridor facing the eye ----------
    const float A = strength();
    const float sx0 = kSeaR * std::sin(moon_phi_);
    const float sz0 = kSeaR * std::cos(moon_phi_);
    const float exz = std::sqrt(cam.ox * cam.ox + cam.oz * cam.oz);
    const float cx0 = kSeaR * (cam.ox / exz);
    const float cz0 = kSeaR * (cam.oz / exz);
    for (int k = 0; k < 34; ++k)
    {
        const unsigned h = hash(static_cast<unsigned>(k),
                                static_cast<unsigned>(frame_));
        const float t = (static_cast<float>(k) +
                         static_cast<float>(h % 16u) / 16.f) /
                        34.f;
        const float jx = (static_cast<float>(h % 32u) - 16.f) * 1.6f;
        const float jz = (static_cast<float>((h >> 5) % 32u) - 16.f) * 1.6f;
        const float gx = sx0 + (cx0 - sx0) * t + jx;
        const float gz = sz0 + (cz0 - sz0) * t + jz;
        const float gh = tide(gx, gz);
        if (gh < 0.35f * A)
        {
            continue;  // only the crest glitters
        }
        const zb::raster::Vtx gv = zb::raster::project(cam, gx, gh, gz);
        const int gain = static_cast<int>(
            static_cast<float>(60 + 60 * static_cast<int>(h % 3u)) *
            clamp_f(gh / A, 0.f, 1.f));
        rc_.add_pixel(static_cast<int>(gv.sx), static_cast<int>(gv.sy), 255,
                      242, 205, gain);
    }

    // ---- beacon lights --------------------------------------------
    const auto beacon = [&](const float deg, const float r, const float lift,
                            const int rr, const int gg, const int bb,
                            const float str)
    {
        const Site s = site_at(deg, r);
        const zb::raster::Vtx bv = zb::raster::project(cam, s.x, lift, s.z);
        if (bv.sx > -40.f && bv.sx < static_cast<float>(vw_ + 40) && bv.sy > -40.f &&
            bv.sy < static_cast<float>(vh_ + 40))
        {
            rc_.glow(static_cast<int>(bv.sx), static_cast<int>(bv.sy), 12, rr, gg,
                     bb, str);
        }
    };

    const float pulse =
        0.55f + 0.35f * std::sin(static_cast<float>(frame_) * 0.11f);
    if (quest_ == Quest::FerryWait)
    {
        beacon(kDockA, kCoastR + 4.f, tide_at(kDockA, kCoastR + 4.f) + 2.f, 255,
               170, 60, pulse);
        beacon(kDockB, kCoastR + 4.f, tide_at(kDockB, kCoastR + 4.f) + 2.f, 255,
               170, 60, pulse);
    }
    else if (ferry_done_)
    {
        beacon(kDockB, kCoastR + 4.f, tide_at(kDockB, kCoastR + 4.f) + 2.f, 90,
               230, 140, 0.8f);
    }
    if (pearls_done_)
    {
        beacon(kPearls, kBedR, 0.8f, 90, 230, 140, 0.8f);
    }
    else if (quest_ == Quest::Pearls)
    {
        beacon(kPearls, kBedR, 0.8f, 120, 200, 255, pulse);
    }
    if (lantern_failed_)
    {
        beacon(kLantern, kCoastR, tide_at(kLantern, kCoastR) + 7.f, 255, 90, 70,
               0.9f);
    }
    else if (lantern_done_)
    {
        beacon(kLantern, kCoastR, tide_at(kLantern, kCoastR) + 7.f, 90, 230, 140,
               0.8f);
    }
    else if (quest_ == Quest::Lantern)
    {
        beacon(kLantern, kCoastR, tide_at(kLantern, kCoastR) + 7.f, 255, 190, 90,
               pulse);
    }
}

void TideCanvas::draw_at(core::Graphics &area) const
{
    render_world();

    // one-call hand-off (U-8): the rgba32 build's Color IS the raster
    // word, so the block goes over as-is
    area.draw_surface(reinterpret_cast<const core::Color *>(rc_.pixels()), vw_,
                      vw_, vh_, 0, 0);

    // tide gauge for the active site: a vertical well, the marker, and
    // the lantern quest's flood line
    float site_h = 0.f;
    switch (quest_)
    {
    case Quest::FerryWait:
    case Quest::FerryCross:
        site_h = tide_at(kSill, kCoastR);
        break;
    case Quest::Pearls:
        site_h = tide_at(kPearls, kBedR);
        break;
    case Quest::Lantern:
    case Quest::Done:
        site_h = tide_at(kLantern, kCoastR);
        break;
    }
    const float hn = clamp_f(0.5f + site_h / 8.8f, 0.f, 1.f);
    const int gx = 12;
    const int gy0 = vh_ - 140;
    const int gy1 = vh_ - 14;
    area.fill_rect(gx, gy0, gx + 12, gy1, core::Color::from(10, 18, 34));
    area.fill_rect(gx, gy0, gx + 12, gy0 + 1, core::Color::from(60, 76, 96));
    area.fill_rect(gx, gy1 - 1, gx + 12, gy1, core::Color::from(60, 76, 96));
    const int my =
        gy1 - static_cast<int>(static_cast<float>(gy1 - gy0 - 2) * hn) - 1;
    area.fill_rect(gx + 1, my, gx + 11, my + 1, core::Color::from(255, 210, 120));
    const float lf = clamp_f(0.5f + 1.4f / 8.8f, 0.f, 1.f);
    const int ly =
        gy1 - static_cast<int>(static_cast<float>(gy1 - gy0 - 2) * lf) - 1;
    area.fill_rect(gx, ly, gx + 12, ly, core::Color::from(200, 80, 70));
}

void Moonpull::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(moonpull_font, moonpull_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Moonpull::sync_status()
{
    const int dawn = static_cast<int>(
        100.f * clamp_f(static_cast<float>(sea_->frame()) /
                            static_cast<float>(TideCanvas::kNightFrames),
                        0.f, 1.f));
    const int key = dawn * 10 + sea_->quests_done() * 2 +
                    static_cast<int>(sea_->quest_stage()) * 100 +
                    (sea_->night_over() ? 1 : 0);
    if (key == last_status_)
    {
        return;
    }
    char quest[96];
    sea_->quest_text(quest, static_cast<int>(sizeof(quest)));
    char buf[160];
    if (sea_->night_over())
    {
        std::snprintf(buf, sizeof(buf), "DAWN - %d/3", sea_->quests_done());
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%s   DAWN %d%%", quest, dawn);
    }
    status_->set_text(buf);
    last_status_ = key;
}

void Moonpull::create_window(const uint32_t max_client_width,
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
    root.set_background_color(core::Color::from(6, 8, 22));

    auto title = std::make_unique<Label>();
    title->set_text("MOONPULL");
    title->set_text_color(kTitle);
    title->set_bold(true);
    title->set_size(150 * u, 24 * u);
    title->set_position(10 * u, 6 * u);
    title->set_font_size(9 * u);
    root.add_child(std::move(title));

    auto stat = std::make_unique<Label>();
    stat->set_text("1/3  PARK A CREST ON THE REEF PASS");
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
    reset->set_text_color(core::Color::from(20, 16, 10));
    reset->set_background_color(kTitle);
    sub_reset_ = reset->clicked.subscribe([this]
                                          { sea_->reset(); });
    root.add_child(std::move(reset));

    auto sea = std::make_unique<TideCanvas>();
    const int ah = h - hud_h;
    sea->set_size(w, ah);
    sea->set_position(0, hud_h);
    sea->set_geometry(w, ah);
    sea_ = sea.get();
    root.add_child(std::move(sea));
}
