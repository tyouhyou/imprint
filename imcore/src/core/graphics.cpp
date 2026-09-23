#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <iterator>
#include "graphics.hpp"

using namespace zb::ui::core;

#if defined(USE_INTEGER_GEOMETRY)
namespace
{
    /*
     * floor(sqrt(n)) for n < 2^62 by the binary restoring algorithm
     * (no division, no floating point). Callers pass screen-sized radii,
     * so rx^2*ry^2 stays below 2^62 for rx, ry <= 46340.
     */
    int64_t isqrt_u64(const uint64_t n)
    {
        uint64_t r = n, q = 1, root = 0;
        while (q <= n)
        {
            q <<= 2;
        }
        while (q > 1)
        {
            q >>= 2;
            const uint64_t t = root + q;
            root >>= 1;
            if (r >= t)
            {
                r -= t;
                root += q;
            }
        }
        return static_cast<int64_t>(root);
    }
}  // namespace
#endif

namespace
{
    /*
     * Half-chord of the corner circle at vertical distance dy (1..r):
     * floor(sqrt(r*r - dy*dy)). Float-free under USE_INTEGER_GEOMETRY,
     * the same split as draw_circle's octant bound.
     */
    int corner_chord(const int r, const int dy)
    {
        // the radius can exceed sqrt(INT_MAX) (the round-rect clamp only
        // needs the rect edge, and an unbounded surface's edge is huge):
        // r*r must not wrap `int` before the subtraction. dy <= r by the
        // callers, so sq stays non-negative; clamp anyway for defense.
        const int64_t sq = static_cast<int64_t>(r) * r - static_cast<int64_t>(dy) * dy;
        if (sq <= 0)
        {
            return 0;
        }
#if defined(USE_INTEGER_GEOMETRY)
        return static_cast<int>(isqrt_u64(static_cast<uint64_t>(sq)));
#else
        return static_cast<int>(std::sqrt(static_cast<double>(sq)));
#endif
    }

    /*
     * Rounded-rect row span on the pixel-center grid, in 1/4-px units.
     * The old index-space chords (dy = row - (bottom - r)) measured even
     * boxes against half-pixel arc centers, so their tangent rows
     * collapsed a full row early: the model500 knob's conic face
     * underfilled the whole bottom arc and leaked bright pixels between
     * the border ring and the inset shadow. Arc centers sit at
     * (left + r, top + r) and (right + 1 - r, bottom + 1 - r)
     * continuously; straight-run rows keep the full span.
     */
    struct span_q
    {
        int lx;      // first full pixel
        int rx;      // last full pixel (lx > rx = nothing this row)
        int fl, fr;  // 0..255 coverage of the partial pixels lx-1 / rx+1
    };

    span_q rounded_span_q(const int left, const int top, const int right,
                          const int bottom, const int r, const int row)
    {
        const int r4 = 4 * r;
        const int y4 = 4 * row + 2;
        const int top_arc4 = 4 * top + r4;
        const int bot_arc4 = 4 * (bottom + 1) - r4;
        const int dy4 = std::max(0, std::max(top_arc4 - y4, y4 - bot_arc4));
        span_q s{left, right, 0, 0};
        if (dy4 > 0)
        {
            const int chord4 = corner_chord(r4, dy4);
            // the row's boundaries ride the CORNER arcs (left + r -
            // chord, right + 1 - r + chord); a box-center ± chord form
            // would draw a circle, shrinking every rounded rect's edge
            // rows (the locked top-edge span test caught exactly that)
            const int lo4 = 4 * left + r4 - chord4;
            const int hi4 = 4 * (right + 1) - r4 + chord4;
            s.lx = std::max(left, (lo4 + 1) / 4);   // first center >= lo
            s.rx = std::min(right, (hi4 - 3) / 4);  // last center <= hi
            if (s.lx > s.rx)
            {
                s.lx = 1;
                s.rx = 0;
                return s;
            }
            // corner arcs: the fringe pixel's coverage is its true area
            // overlap with the rounded rect (8x8 subsample of the integer
            // indicator, squared-distance corner test). The 1D chord
            // fraction under-covers on the curve — the chord lands on a
            // pixel boundary at a different sub-pixel position row by
            // row, so a translucent border over the fill read the PAGE
            // through the band's outer tail instead of the face (the
            // model500 knob rim's speckle; the browser's background-clip
            // paints the face anti-aliased to the border-box edge)
            s.fl = Graphics::rounded_overlap255(left, top, right, bottom, r,
                                               s.lx - 1, row);
            s.fr = Graphics::rounded_overlap255(left, top, right, bottom, r,
                                               s.rx + 1, row);
        }
        return s;
    }

    /*
     * Channel-wise linear interpolation through the 8-bit-normalized
     * accessors (A-19); `steps` must be > 0, `i` in [0, steps].
     */
    Color lerp_color(const Color &from, const Color &to, const int i, const int steps)
    {
        Color c{};
        c.set_r(static_cast<uint8_t>((from.r() * (steps - i) + to.r() * i) / steps));
        c.set_g(static_cast<uint8_t>((from.g() * (steps - i) + to.g() * i) / steps));
        c.set_b(static_cast<uint8_t>((from.b() * (steps - i) + to.b() * i) / steps));
        c.set_a(static_cast<uint8_t>((from.a() * (steps - i) + to.a() * i) / steps));
        return c;
    }

    /*
     * floor(sqrt(n)) by Newton for screen-sized n (radius^2 stays far
     * below int64 range); used by the AA circle chord coverage. Works
     * on every depth/geometry configuration, no FPU needed.
     */
    int64_t isqrt_floor(const int64_t n)
    {
        if (n <= 0)
        {
            return 0;
        }
        int64_t x = n, y = (x + 1) / 2;
        while (y < x)
        {
            x = y;
            y = (x + n / x) / 2;
        }
        return x;
    }

    /*
     * Normalize any integer degree into [0, 360); the arc angle walk uses
     * it so multi-turn sweeps wrap (V-5 never needs the turn count).
     */
    inline int norm_deg(const int d)
    {
        const int r = d % 360;
        return r >= 0 ? r : r + 360;
    }

    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

#if defined(USE_INTEGER_GEOMETRY)
    /*
     * 1-degree sin/cos over the whole circle from a compile-time-generated
     * [0, 90] quarter table (V-5 two-trig-path policy). The table is
     * evaluated by the host compiler -- a constexpr Taylor series, so no
     * libm call and no runtime floating point on the target -- scaled by
     * 256 (8-bit fraction) and rounded; quadrant symmetry expands it to
     * 360 degrees. Coordinates are one int32 multiply + shift per sample.
     */
    constexpr int kArcSinScale = 256;

    constexpr int arc_sin_deg_q(const int deg)
    {
        const double x = deg * kDegToRad;
        const double x2 = x * x;
        const double s = x * (1.0 - x2 / 6.0 + x2 * x2 / 120.0 - x2 * x2 * x2 / 5040.0 + x2 * x2 * x2 * x2 / 362880.0);
        return static_cast<int>(s * kArcSinScale + 0.5);
    }

    struct ArcSinLut
    {
        int16_t q[91];
        constexpr ArcSinLut() : q{}
        {
            for (int i = 0; i <= 90; ++i)
            {
                q[i] = static_cast<int16_t>(arc_sin_deg_q(i));
            }
        }
    };
    constexpr ArcSinLut kArcSinLut{};

    inline int arc_sin_q(const int deg)
    {
        const int d = norm_deg(deg);
        const int quad = d / 90;
        const int r = d % 90;
        switch (quad)
        {
        case 0: return kArcSinLut.q[r];
        case 1: return kArcSinLut.q[90 - r];
        case 2: return -kArcSinLut.q[r];
        default: return -kArcSinLut.q[90 - r];
        }
    }
    inline int arc_cos_q(const int deg) { return arc_sin_q(norm_deg(90 - deg)); }
#endif

    /*
     * Sample the arc point at integer degree `deg` onto the circle
     * (cx, cy, radius), rounded to the pixel grid. USE_INTEGER_GEOMETRY
     * selects the lookup path (no runtime float); otherwise IEEE float
     * sin/cos. Both agree on each sample to within +-0.5px at radius <=
     * 128 (locked by the gated arc expectations in test_graphics).
     */
    inline void arc_point(const int deg, const int cx, const int cy, const int radius, int &ox, int &oy)
    {
#if defined(USE_INTEGER_GEOMETRY)
        const int vx = arc_cos_q(deg) * radius;
        const int vy = arc_sin_q(deg) * radius;
        // round half away from zero on the 256-scaled value
        ox = cx + (vx >= 0 ? (vx + 128) >> 8 : -((-vx + 128) >> 8));
        oy = cy + (vy >= 0 ? (vy + 128) >> 8 : -((-vy + 128) >> 8));
#else
        const double a = deg * kDegToRad;
        ox = cx + static_cast<int>(std::floor(radius * std::cos(a) + 0.5));
        oy = cy + static_cast<int>(std::floor(radius * std::sin(a) + 0.5));
#endif
    }

    /*
     * CSS conic degrees of the pixel offset (dx, dy, y down): 0 = up,
     * clockwise, into [0, 360). Dual-path like arc_point above: the
     * integer path folds the y-up vector (dx, -dy) into the first
     * octant and binary-searches the 1-degree sin LUT (monotonic on
     * [0, 45]; compared 256-scaled against the isqrt hypotenuse), the
     * float path is one atan2. The two agree within 1 degree.
     */
    inline int conic_theta(const int dx, const int dy)
    {
        if (dx == 0 && dy == 0)
        {
            return 0;
        }
#if defined(USE_INTEGER_GEOMETRY)
        const int ax = dx >= 0 ? dx : -dx;
        const int ay = dy >= 0 ? dy : -dy;
        const bool swap = ay > ax;
        const int big = swap ? ay : ax;
        const int small = swap ? ax : ay;
        const int64_t hyp = isqrt_floor(1LL * big * big + 1LL * small * small);
        const int64_t target = 1LL * small * 256;
        int lo = 0;
        int hi = 45;
        while (lo < hi)
        {
            const int mid = (lo + hi) / 2;
            if (1LL * arc_sin_q(mid) * hyp < target)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        const int small_a = swap ? 90 - lo : lo;
        // unfold by the quadrant of (dx, -dy)
        int alpha = small_a;
        if (dx < 0)
        {
            alpha = 180 - small_a;
            if (dy > 0)
            {
                alpha = 180 + small_a;
            }
        }
        else if (dy > 0)
        {
            alpha = 360 - small_a;
        }
        return norm_deg(90 - alpha);
#else
        constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
        const double a =
            std::atan2(static_cast<double>(dx), static_cast<double>(-dy)) * kRadToDeg;
        int t = static_cast<int>(a >= 0.0 ? a + 0.5 : a - 0.5);
        return norm_deg(t);
#endif
    }
}  // namespace

// public chord alias for widget-level paint (P-2e): the shadow span
// masks reuse the same chords the fills use (same formula as the
// file-local corner_chord above, restated: the member body cannot
// name it — class scope would recurse into this very member)
int Graphics::corner_chord(const int r, const int dy)
{
    if (dy <= 0)
    {
        return r;
    }
    const int64_t sq = static_cast<int64_t>(r) * r - static_cast<int64_t>(dy) * dy;
    if (sq <= 0)
    {
        return 0;
    }
#if defined(USE_INTEGER_GEOMETRY)
    return static_cast<int>(isqrt_u64(static_cast<uint64_t>(sq)));
#else
    return static_cast<int>(std::sqrt(static_cast<double>(sq)));
#endif
}

// THE single radius clamp (semantics on the declaration): pixel-count
// box dimensions, one definition for every rounded primitive
int Graphics::inscribed_radius(const int width, const int height,
                               const int radius)
{
    if (radius <= 0 || width <= 0 || height <= 0)
    {
        return 0;
    }
    const int half = std::min(width, height) / 2;
    return radius < half ? radius : half;
}

int Graphics::rounded_overlap255(const int left, const int top, const int right,
                                 const int bottom, const int radius,
                                 const int px, const int py)
{
    const int w = right - left + 1;
    const int h = bottom - top + 1;
    int r = inscribed_radius(w, h, radius);
    if (w <= 0 || h <= 0 || r == 0)
    {
        // plain rect: the pixel is fully inside or outside
        const bool inside = px >= left && px <= right && py >= top && py <= bottom;
        return inside ? 255 : 0;
    }
    const int rr256 = r * 256;
    const int ex256 = w * 128 - rr256;
    const int ey256 = h * 128 - rr256;
    const int cx256 = left * 256 + w * 128;
    const int cy256 = top * 256 + h * 128;
    constexpr int SUB = 8;           // 8x8 subsamples per pixel
    constexpr int SUBQ = 256 / SUB;  // sub-cell in the 256 grid
    int inside = 0;
    for (int sy = 0; sy < SUB; ++sy)
    {
        const int Y = py * 256 + sy * SUBQ + SUBQ / 2;
        const int qy = std::abs(Y - cy256) - ey256;
        for (int sx = 0; sx < SUB; ++sx)
        {
            const int X = px * 256 + sx * SUBQ + SUBQ / 2;
            const int qx = std::abs(X - cx256) - ex256;
            if (qx <= 0 || qy <= 0
                ? std::max(qx, qy) <= rr256
                : 1LL * qx * qx + 1LL * qy * qy <= 1LL * rr256 * rr256)
            {
                ++inside;
            }
        }
    }
    return inside * 255 / (SUB * SUB);
}

Graphics::Graphics(uint32_t width, uint32_t height, void *data)
    : pixels{nullptr}
    , is_wrapper_mode{false}
    , alpha_enabled{false}
    , draw_area_offset_enabled{false}
    , draw_area_offset{0, 0}
    , imsize{static_cast<int>(width), static_cast<int>(height)}
    , draw_area{0, 0, static_cast<int>(width) - 1, static_cast<int>(height) - 1}
{
    // init-path validation (contract §1): the pixel index math is
    // int-based, so the pixel count must fit in one; the product is
    // computed in 64 bits so the check itself cannot wrap. 65536x65536
    // used to wrap to zero -- allocate nothing, still report a
    // full-size draw area and overflow the heap on the first fill/draw
    if (width == 0 || height == 0 ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) >
            static_cast<uint64_t>(2147483647))
    {
        throw error("surface " + std::to_string(width) + "x" +
                    std::to_string(height) + " is empty or too large");
    }
    const auto len = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (nullptr == data)
    {
        is_wrapper_mode = false;
        pixels = new Color[len]{};
    }
    else
    {
        is_wrapper_mode = true;
        pixels = static_cast<Color *>(data);
    }
}

Graphics::~Graphics()
{
    if (!is_wrapper_mode)
        delete[] pixels;
}

Graphics::ptr Graphics::clone(int x, int y, int32_t width, int32_t height) const
{
    // 64-bit sums: x + width must not wrap the check itself
    // (2^30 + 2^30 used to go negative and pass)
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        static_cast<int64_t>(x) + width > imsize.width ||
        static_cast<int64_t>(y) + height > imsize.height)
    {
        throw error("clone area out of bounds");
    }

    auto g = Graphics::make_ptr((uint32_t)width, (uint32_t)height);
    g->set_draw_area(0, 0, width, height);
    for (int row = 0; row < height; row++)
    {
        std::copy_n(pixels + imsize.width * (y + row) + x, width, g->pixels + width * row);
    }
    return g;
}

Graphics::ClipGuard Graphics::clip_safe(int x, int y, int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0)
    {
        return ClipGuard(*this, draw_area, draw_area_offset_enabled, draw_area_offset, false);
    }

    // requested area in absolute (buffer) coordinates
    const int ax = draw_area.start_x + x;
    const int ay = draw_area.start_y + y;
    const int aex = ax + width - 1;
    const int aey = ay + height - 1;

    // intersect with the current draw area
    const int cx = std::max(ax, draw_area.start_x);
    const int cy = std::max(ay, draw_area.start_y);
    const int cex = std::min(aex, draw_area.end_x);
    const int cey = std::min(aey, draw_area.end_y);
    if (cex < cx || cey < cy)
    {
        return ClipGuard(*this, draw_area, draw_area_offset_enabled, draw_area_offset, false);
    }

    const imarea_t saved_area = draw_area;
    const bool saved_offset_enabled = draw_area_offset_enabled;
    const impoint_t saved_offset = draw_area_offset;

    // the clip bounds are the intersection; the local-coordinate origin
    // stays the REQUESTED one. Using the intersection origin as the
    // offset too (what set_draw_area does) translated a child that
    // hangs off the left/top of its parent's clip into view by the
    // clipped-away amount; right/bottom overflow was never affected
    // (cx == ax there), which is the asymmetry that pinned it
    draw_area = {cx, cy, cex, cey};
    draw_area_offset_enabled = true;
    draw_area_offset = {ax, ay};
    return ClipGuard(*this, saved_area, saved_offset_enabled, saved_offset, true);
}

Graphics::ClipGuard Graphics::clip_surface_safe(int x, int y, int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0)
    {
        return ClipGuard(*this, draw_area, draw_area_offset_enabled, draw_area_offset, false);
    }

    // intersect with the whole SURFACE, not the current draw area: the
    // caller passes surface coordinates and means to escape nested box
    // clips (the P-2e outer-shadow pass)
    // int64 math: on devkitARM int32_t is long, so the raw expression
    // would not mix with int in std::min
    const int cx = static_cast<int>(std::max<int64_t>(x, 0));
    const int cy = static_cast<int>(std::max<int64_t>(y, 0));
    const int cex = static_cast<int>(
        std::min<int64_t>(static_cast<int64_t>(x) + width - 1,
                          static_cast<int64_t>(imsize.width) - 1));
    const int cey = static_cast<int>(
        std::min<int64_t>(static_cast<int64_t>(y) + height - 1,
                          static_cast<int64_t>(imsize.height) - 1));
    if (cex < cx || cey < cy)
    {
        return ClipGuard(*this, draw_area, draw_area_offset_enabled, draw_area_offset, false);
    }

    const imarea_t saved_area = draw_area;
    const bool saved_offset_enabled = draw_area_offset_enabled;
    const impoint_t saved_offset = draw_area_offset;

    draw_area = {cx, cy, cex, cey};
    draw_area_offset_enabled = true;
    draw_area_offset = {x, y};
    return ClipGuard(*this, saved_area, saved_offset_enabled, saved_offset, true);
}

void Graphics::set_draw_area(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    draw_area_offset = {x, y};
    auto sx = x;
    auto sy = y;

    if (0 > sx)
        sx = 0;
    if (sx >= imsize.width)
        sx = imsize.width - 1;
    if (0 > sy)
        sy = 0;
    if (sy >= imsize.height)
        sy = imsize.height - 1;

    auto endx = sx + (int)width - 1;
    if (endx > (int)imsize.width - 1)
        endx = (int)imsize.width - 1;
    auto endy = sy + (int)height - 1;
    if (endy > (int)imsize.height - 1)
        endy = (int)imsize.height - 1;

    draw_area = {sx, sy, endx, endy};
}

void Graphics::draw_image(
    const Color *img,          // bitmap buffer pointer
    int img_width,      // bitmap widht
    int img_height,     // bitmap height
    int img_row_stride, // pixel amount in one row
    int start_x,        // x coordinate in the graphic where starting to draw bitmap
    int start_y         // y coordinate in the graphic where starting to draw bitmap
)
{
    int sx, sy;
    for (int row = 0; row < img_height; row++)
    {
        sy = row + start_y;
        if (sy < 0)
            continue;
        if (sy >= imsize.height)
            break;
        for (int col = 0; col < img_width; col++)
        {
            sx = col + start_x;
            if (sx < 0)
                continue;
            if (sx >= imsize.width)
                break;
            draw_pixel(sx, sy, img[row * img_row_stride + col]);
        }
    }
}

void Graphics::draw_image(
    const image_t &img,
    int start_x,
    int start_y)
{
    const int stride = img.row_stride > 0 ? img.row_stride : img.width;
    // malformed view (rows would overlap, or no pixels): refuse to draw
    // instead of reading past each row (batch K / N1)
    if (img.pixels == nullptr || stride < img.width)
    {
        return;
    }
    draw_image(img.pixels, img.width, img.height, stride, start_x, start_y);
}

void Graphics::draw_image(
    const Color *img,
    int img_width,
    int img_height,
    int img_row_stride,
    int start_x,
    int start_y,
    const Color &tint)
{
    // a tint whose channels all sit at the normalized maximum is the
    // 1.0 multiplier: take the plain path so the identity is pixel-exact
    // even at 16bpp, where the <<3 expansion would otherwise darken the
    // modulate (255 reads back 248, and 248*248/255 requantizes off by
    // one bit step). The 16bpp alpha bit reads back 0/1, so opacity
    // there is "any bit set", not a 8-bit threshold
    constexpr int max8 = Color::depth == 32 ? 0xFF : 0xF8;
    const bool tint_opaque = Color::per_channel_blend ? tint.a() >= 0xFF : tint.a() != 0;
    if (tint.r() >= max8 && tint.g() >= max8 && tint.b() >= max8 && tint_opaque)
    {
        draw_image(img, img_width, img_height, img_row_stride, start_x, start_y);
        return;
    }
    int sx, sy;
    for (int row = 0; row < img_height; row++)
    {
        sy = row + start_y;
        if (sy < 0)
            continue;
        if (sy >= imsize.height)
            break;
        for (int col = 0; col < img_width; col++)
        {
            sx = col + start_x;
            if (sx < 0)
                continue;
            if (sx >= imsize.width)
                break;
            const Color &src = img[row * img_row_stride + col];
            Color c{};
            c.set_r(static_cast<uint8_t>(src.r() * tint.r() / 0xFF));
            c.set_g(static_cast<uint8_t>(src.g() * tint.g() / 0xFF));
            c.set_b(static_cast<uint8_t>(src.b() * tint.b() / 0xFF));
            // alpha: a real weight at 32bpp; a binary gate at 16bpp (the
            // single alpha bit has no 8-bit weight to multiply -- an
            // 8-bit modulate there would clear every pixel's bit)
            if constexpr (Color::per_channel_blend)
            {
                c.set_a(static_cast<uint8_t>(src.a() * tint.a() / 0xFF));
            }
            else
            {
                c.set_a(static_cast<uint8_t>(tint.a() != 0 ? src.a() : 0));
            }
            draw_pixel(sx, sy, c);
        }
    }
}

void Graphics::draw_image(
    const image_t &img,
    int start_x,
    int start_y,
    const Color &tint)
{
    const int stride = img.row_stride > 0 ? img.row_stride : img.width;
    if (img.pixels == nullptr || stride < img.width)
    {
        return;
    }
    draw_image(img.pixels, img.width, img.height, stride, start_x, start_y, tint);
}

void Graphics::fill(const Color &colr)
{
    // damage mode: the fill is part of a partial repaint -- rows and
    // spans outside the region must keep their previous content
    int start_x = draw_area.start_x;
    int end_x = draw_area.end_x;
    int start_y = draw_area.start_y;
    int end_y = draw_area.end_y;
    if (damage_on_)
    {
        // half-open damage vs inclusive draw_area (A-13): intersect with
        // [l, r-1] x [t, b-1]. A non-intersecting draw area used to yield
        // start > end and a negative width fed to fill_n (A-12 underflow)
        start_x = std::max(start_x, damage_l_);
        end_x = std::min(end_x, damage_r_ - 1);
        start_y = std::max(start_y, damage_t_);
        end_y = std::min(end_y, damage_b_ - 1);
        if (start_x > end_x || start_y > end_y)
        {
            return;
        }
    }
    auto draw_width = end_x - start_x + 1;
    for (int row = start_y; row <= end_y; row++)
    {
        std::fill_n(pixels + (imsize.width * row + start_x), draw_width, colr);
    }
}

void Graphics::draw_pixel(int x, int y, const Color &colr)
{
    int sx = x, sy = y;
    if (draw_area_offset_enabled)
    {
        sx = draw_area_offset.x + x;
        sy = draw_area_offset.y + y;
    }

    if (sx < draw_area.start_x || sy < draw_area.start_y || sx > draw_area.end_x || sy > draw_area.end_y)
    {
        return;
    }

    // damage mode: writes are hard-clipped to the repainted region so
    // widgets redrawn for it cannot smear over pruned neighbors whose
    // stale pixels the region-present relies on (CanvasWindow::paint).
    // The rect is half-open, so the exclusive edge itself is outside
    // (A-12: `sx > damage_r_` used to permit the boundary column)
    if (damage_on_ && !damage_contains(sx, sy))
    {
        return;
    }

    Color &px = pixels[imsize.width * sy + sx];
    if (!this->alpha_enabled)
    {
        px = colr;
    }
    else
    {
        px = alpha_blend(colr, px);
    }
}

void Graphics::plot_aa(int x, int y, int coverage, const Color &colr)
{
    if (coverage <= 0)
    {
        return;
    }
    int sx = x, sy = y;
    if (draw_area_offset_enabled)
    {
        sx = draw_area_offset.x + x;
        sy = draw_area_offset.y + y;
    }
    if (sx < draw_area.start_x || sy < draw_area.start_y || sx > draw_area.end_x || sy > draw_area.end_y)
    {
        return;
    }
    if (damage_on_ && !damage_contains(sx, sy))
    {
        return;
    }
    Color &px = pixels[imsize.width * sy + sx];
    if constexpr (Color::per_channel_blend)
    {
        // the coverage is the weight, stacked on the color's own alpha
        const uint32_t a = static_cast<uint32_t>(coverage) * colr.a() / 0xFF;
        if (a == 0)
        {
            return;
        }
        Color front = colr;
        front.set_a(static_cast<uint8_t>(a));
        px = alpha_blend(front, px);
    }
    else
    {
        // binary alpha: coverage quantizes to plot/skip at half, so the
        // stroke stays one pixel wide instead of doubling
        if (coverage >= 128)
        {
            px = colr;
        }
    }
}

Color Graphics::alpha_blend(const Color &front_color, const Color &back_color)
{
    const uint32_t alpha = front_color.a();
    if (alpha <= 0)
    {
        return back_color;
    }
    if constexpr (!Color::per_channel_blend)
    {
        // a single alpha bit: opacity is binary. The 8-bit blend math
        // below would treat the bit as a 1/255 weight and make every
        // covered pixel nearly transparent
        return front_color;
    }
    if (alpha >= 0xFF)
    {
        return front_color;
    }

    const uint32_t inv_alpha = 0xFF - alpha;
    Color rst{};
    rst.set_r((uint8_t)((front_color.r() * alpha + back_color.r() * inv_alpha) / 0xFF));
    rst.set_g((uint8_t)((front_color.g() * alpha + back_color.g() * inv_alpha) / 0xFF));
    rst.set_b((uint8_t)((front_color.b() * alpha + back_color.b() * inv_alpha) / 0xFF));
    rst.set_a((uint8_t)(alpha + back_color.a() * inv_alpha / 0xFF));  // source-over alpha
    return rst;
}

void Graphics::fill_rect(int x1, int y1, int x2, int y2, const Color &colr)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_rect(x1, y1, x2, y2, colr);  // S-1: bones only
        return;
    }
    // the row span is normalized: y1 == y2 is a single row (the old
    // descending branch never ran and looped forever on a single row)
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;
    for (int row = top; row <= bottom; ++row)
    {
        draw_line(x1, row, x2, row, colr);
    }
}

void Graphics::draw_line(int x1, int y1, int x2, int y2, const Color &colr)
{
    int nDx = x2 - x1;
    int nDy = y2 - y1;

    int nIx = abs(nDx);
    int nIy = abs(nDy);

    int nInc = (nIx > nIy ? nIx : nIy);

    if (nInc < 2)
    {
        // draw the two endpoints at least, otherwise a short line (|dx|+|dy| <= 1) draws nothing
        draw_pixel(x1, y1, colr);
        draw_pixel(x2, y2, colr);
        return;
    }

    // the judge accumulators add 2*nIx/2*nIy every step and only ever
    // subtract the cross term when their own coordinate's branch fires;
    // on a horizontal/vertical line the cross term is 0, so the active
    // accumulator grows to ~2*L*L -- wide enough to overflow `int` for a
    // segment longer than ~26844 px (an unbounded surface fill). The
    // pixel coordinates stay `int` (clipped by the surface); only the
    // accumulation is widened.
    int64_t nJudgeX = -nIy;
    int64_t nJudgeY = -nIx;
    int x = x1;
    int y = y1;

    nInc--;
    int64_t nTwoIx = 2 * nIx;
    int64_t nTwoIy = 2 * nIy;

    for (int i = 0; i < nInc; i++)
    {
        nJudgeX += nTwoIx;
        nJudgeY += nTwoIy;

        bool bPlot = false;

        if (nJudgeX >= 0)
        {
            bPlot = true;
            nJudgeX -= nTwoIy;

            if (nDx > 0)
                x++;
            else if (nDx < 0)
                x--;
        }
        if (nJudgeY >= 0)
        {
            bPlot = true;
            nJudgeY -= nTwoIx;

            if (nDy > 0)
                y++;
            else if (nDy < 0)
                y--;
        }
        if (bPlot)
        {
            draw_pixel(x, y, colr);
        }
    }
    draw_pixel(x1, y1, colr);
    draw_pixel(x2, y2, colr);
}

void Graphics::draw_triangle(const impoint_t &p1, const impoint_t &p2, const impoint_t &p3, const Color &colr)
{
    draw_line(p1, p2, colr);
    draw_line(p2, p3, colr);
    draw_line(p3, p1, colr);
}

void Graphics::draw_circle(int x, int y, int radius, const Color &colr)
{
    if (radius < 0)
    {
        return;  // a negative radius is not a circle (round-rect clamps; raw draws nothing)
    }
    int px, py, d, x2m1;
    py = radius;
    d = -radius;
    x2m1 = -1;
    px = 0;

    // The scanline is pure integer Bresenham; only the 45-degree loop
    // bound needs sqrt(). USE_INTEGER_GEOMETRY (FPU-less targets such as
    // the NDS) replaces it with the algebraically equivalent integer
    // test 2*px^2 < radius^2  <=>  px < radius/sqrt(2).
#if defined(USE_INTEGER_GEOMETRY)
    draw_8pixels(x, y, px, py, colr);
    for (px = 1; 2LL * px * px < 1LL * radius * radius; px++)
#else
    // sqrt() is evaluated once per call, not per pixel
    const double octant_limit = radius / std::sqrt(2.0);

    draw_8pixels(x, y, px, py, colr);
    for (px = 1; px < octant_limit; px++)
#endif
    {
        x2m1 += 2;
        d += x2m1;
        if (d >= 0)
        {
            py--;
            d -= (py << 1);
        }
        draw_8pixels(x, y, px, py, colr);
    }
}

void Graphics::fill_circle(int x, int y, int radius, const Color &colr)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_circle(x, y, radius, colr);  // S-1: bones only
        return;
    }
    if (radius < 0)
    {
        return;  // see draw_circle
    }

    int px, py, d, x2m1;
    py = radius;
    d = -radius;
    x2m1 = -1;
    px = 0;

    // same integer-only bound as draw_circle (see the comment there)
#if defined(USE_INTEGER_GEOMETRY)
    draw_incir_pixels(x, y, px, py, colr);
    for (px = 1; 2LL * px * px < 1LL * radius * radius; px++)
#else
    const double octant_limit = radius / std::sqrt(2.0);

    draw_incir_pixels(x, y, px, py, colr);
    for (px = 1; px < octant_limit; px++)
#endif
    {
        x2m1 += 2;
        d += x2m1;
        if (d >= 0)
        {
            py--;
            d -= (py << 1);
        }
        draw_incir_pixels(x, y, px, py, colr);
    }
}

void Graphics::fill_circle_aa(int x, int y, int radius, const Color &colr)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_circle_aa(x, y, radius, colr);  // S-1: bones stay smooth
        return;
    }
    if (radius < 0)
    {
        return;  // see draw_circle
    }
    if (radius == 0)
    {
        draw_pixel(x, y, colr);
        return;
    }
    // one chord span per row: the solid interior writes once, the two
    // fractional edge pixels blend through plot_aa. The chord fraction
    // is the draw_circle_aa formula (exact boundary between py and
    // py+1 splits by the remainder's share), so fills meet outlines.
    for (int dy = -radius; dy <= radius; ++dy)
    {
        const int64_t t = 1LL * radius * radius - 1LL * dy * dy;
        const int half = static_cast<int>(isqrt_floor(t));
        const int64_t rem = t - 1LL * half * half;
        const int frac8 = static_cast<int>(rem * 255 / (2LL * half + 1));
        draw_line(x - half, y + dy, x + half, y + dy, colr);
        if (frac8 > 0)
        {
            plot_aa(x - half - 1, y + dy, frac8, colr);
            plot_aa(x + half + 1, y + dy, frac8, colr);
        }
    }
}

void Graphics::draw_rect(int x1, int y1, int x2, int y2, const Color &colr)
{
    draw_line(x1, y1, x1, y2, colr);
    draw_line(x1, y1, x2, y1, colr);
    draw_line(x1, y2, x2, y2, colr);
    draw_line(x2, y1, x2, y2, colr);
}

void Graphics::fill_round_rect(int x1, int y1, int x2, int y2, int radius, const Color &colr)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_round_rect(x1, y1, x2, y2, radius, colr);  // S-1: bones only
        return;
    }
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);
    if (r == 0)
    {
        fill_rect(left, top, right, bottom, colr);
        return;
    }

    // one span per row: corner rows get the circle chord, middle rows
    // degenerate to dy == 0 == full width, so a single formula covers all
    for (int row = top; row <= bottom; ++row)
    {
        int dy;
        if (row < top + r)
        {
            dy = top + r - row;
        }
        else if (row > bottom - r)
        {
            dy = row - (bottom - r);
        }
        else
        {
            dy = 0;
        }
        const int dx = dy == 0 ? r : corner_chord(r, dy);
        draw_line(left + r - dx, row, right - r + dx, row, colr);
    }
}

void Graphics::draw_round_rect(int x1, int y1, int x2, int y2, int radius, const Color &colr)
{
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);
    if (r == 0)
    {
        draw_rect(left, top, right, bottom, colr);
        return;
    }

    // four straight edges first; the arc extremes (dy == 0 and dy == r)
    // coincide with their endpoints, so no pixel is drawn twice
    draw_line(left + r, top, right - r, top, colr);
    draw_line(left + r, bottom, right - r, bottom, colr);
    draw_line(left, top + r, left, bottom - r, colr);
    draw_line(right, top + r, right, bottom - r, colr);
    for (int i = 1; i < r; ++i)
    {
        const int dx = corner_chord(r, i);
        draw_pixel(left + r - dx, top + r - i, colr);
        draw_pixel(right - r + dx, top + r - i, colr);
        draw_pixel(left + r - dx, bottom - r + i, colr);
        draw_pixel(right - r + dx, bottom - r + i, colr);
    }
}

void Graphics::draw_round_rect_aa(int x1, int y1, int x2, int y2, int radius,
                                  const Color &colr)
{
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);
    if (r == 0)
    {
        draw_rect(left, top, right, bottom, colr);
        return;
    }

    // Translucent 1px rims take a signed-distance band rasterization.
    // The stroke follows the rounded rect's continuous outline (box
    // [left, right+1] x [top, bottom+1]; inclusive indices are pixels)
    // evaluated on the pixel-center grid, so the ring fades linearly
    // over one pixel on every side and diagonal — the polyline fallback
    // leaves under-covered seams at the diagonals and tangents (the
    // model500 knob's broken lower rim and flat sides). Opaque and
    // binary depths keep the polyline (locked pixel expectations);
    // wireframe keeps it too.
    if constexpr (Color::per_channel_blend)
    {
        if (colr.a() > 0 && colr.a() < 0xFF &&
            render_mode_ != render_mode::wireframe && r >= 2)
        {
            // 1px stroke inside the box edge: sd in [-1, 0], midline
            // half a pixel in. At the box edge the pixel center sits
            // ON the midline (even-sized circles: the 54px knob's rim
            // row centers are 26.5px from the middle), so south/east
            // rims get the same full coverage north/west do. The old
            // pixel-corner grid put the band a half pixel further out,
            // clipping the whole lower arc away (the knob's bright
            // lower leak where the border should read).
            //
            // Straight sections take the tent 1 - |sd + 0.5|: on a
            // straight band edge that IS the exact pixel-area coverage.
            // Corner arcs subsample the band indicator instead — the
            // point-sampled tent loses mass where the midline sweeps
            // across a column (the knob rim's speckle gaps and column
            // jitter), while the box-filtered indicator conserves the
            // ring's mass through the curve.
            constexpr int SUB = 8;              // 8x8 subsamples per pixel
            constexpr int SUBQ = 256 / SUB;     // sub-cell in the 256 grid
            const int w256 = (right - left + 1) * 128;
            const int h256 = (bottom - top + 1) * 128;
            const int rr256 = r * 256;
            const int ex256 = w256 - rr256;
            const int ey256 = h256 - rr256;
            const int cx256 = left * 256 + w256;
            const int cy256 = top * 256 + h256;
            const auto corner_sd = [&](const int px, const int py) -> int64_t
            {
                const int dx = std::abs(px - cx256) - ex256;
                const int dy = std::abs(py - cy256) - ey256;
                return static_cast<int64_t>(isqrt_floor(
                           1LL * dx * dx + 1LL * dy * dy)) -
                       rr256;
            };
            const auto rim_cov = [&](const int colx, const int row) -> int
            {
                const int dx = std::abs(colx * 256 + 128 - cx256) - ex256;
                const int dy = std::abs(row * 256 + 128 - cy256) - ey256;
                if (dx <= 0 || dy <= 0)
                {
                    const int64_t sd = 1LL * (dx > dy ? dx : dy) - rr256;
                    const int64_t ad = (sd + 128) < 0 ? -(sd + 128) : sd + 128;
                    if (ad >= 256)
                    {
                        return 0;
                    }
                    return 255 - static_cast<int>(ad * 255 >> 8);
                }
                // corner arc. Isqrt-free mass bounds reject everything
                // off the annulus: max(dx,dy) <= isqrt(dx^2+dy^2) <=
                // dx+dy, so the point sd lies in [max - rr, sum - rr]
                if (std::max(dx, dy) - rr256 > 0 ||
                    1LL * dx + dy - rr256 < -256)
                {
                    return 0;
                }
                int inside = 0;
                for (int sy = 0; sy < SUB; ++sy)
                {
                    const int py = row * 256 + sy * SUBQ + SUBQ / 2;
                    for (int sx = 0; sx < SUB; ++sx)
                    {
                        const int px = colx * 256 + sx * SUBQ + SUBQ / 2;
                        const int64_t sd = corner_sd(px, py);
                        inside += (sd <= 0 && sd >= -256) ? 1 : 0;
                    }
                }
                return inside * 255 / (SUB * SUB);
            };
            for (int row = top; row <= bottom; ++row)
            {
                for (int colx = left; colx <= right; ++colx)
                {
                    const int cov = rim_cov(colx, row);
                    if (cov > 0)
                    {
                        plot_aa(colx, row, cov, colr);
                    }
                }
            }
            return;
        }
    }
    // four straight edges (axis-aligned, so draw_line_aa stays solid)
    // plus four quarter-arc corners. Angles are the draw_arc_aa
    // convention (0 = +x, positive sweep visually clockwise, y down).
    // Each straight edge stops one pixel short of the corner tangent:
    // the arc polyline already plots its start point, so sharing the
    // tangent pixel would paint it twice — invisible for opaque colors
    // but stacking (squared alpha) for translucent ones (rgba borders
    // and shadow outlines read double-dark at the four tangents).
    // Tiny boxes whose edge collapses to the tangents draw arcs only.
    if (left + r + 1 <= right - r - 1)
    {
        draw_line_aa(left + r + 1, top, right - r - 1, top, colr);
        draw_line_aa(left + r + 1, bottom, right - r - 1, bottom, colr);
    }
    if (top + r + 1 <= bottom - r - 1)
    {
        draw_line_aa(left, top + r + 1, left, bottom - r - 1, colr);
        draw_line_aa(right, top + r + 1, right, bottom - r - 1, colr);
    }
    draw_arc_aa(left + r, top + r, r, 180, 90, colr);      // TL: west -> north
    draw_arc_aa(right - r, top + r, r, 270, 90, colr);     // TR: north -> east
    draw_arc_aa(right - r, bottom - r, r, 0, 90, colr);    // BR: east -> south
    draw_arc_aa(left + r, bottom - r, r, 90, 90, colr);    // BL: south -> west
}

void Graphics::fill_round_rect_aa(int x1, int y1, int x2, int y2, int radius,
                                  const Color &colr)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_round_rect_aa(x1, y1, x2, y2, radius, colr);  // S-1: smooth bones
        return;
    }
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);
    if (r == 0)
    {
        fill_rect(left, top, right, bottom, colr);
        return;
    }

    // the fill_round_rect row layout, but corner rows blend their two
    // fractional edge pixels (the fill_circle_aa chord formula); middle
    // rows stay solid full-width spans
    for (int row = top; row <= bottom; ++row)
    {
        const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
        if (sp.lx > sp.rx)
        {
            continue;
        }
        draw_line(sp.lx, row, sp.rx, row, colr);
        if (sp.fl > 0)
        {
            plot_aa(sp.lx - 1, row, sp.fl, colr);
        }
        if (sp.fr > 0)
        {
            plot_aa(sp.rx + 1, row, sp.fr, colr);
        }
    }
}

void Graphics::fill_round_rect_rotated(int x, int y, int w, int h, int radius, int angle_deg,
                                       int pivot_x, int pivot_y, const Color &colr)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }
    int r = radius < 0 ? 0 : radius;
    const int half = (w < h ? w : h) / 2;
    if (r > half)
    {
        r = half;
    }
    const int deg = norm_deg(angle_deg);
    // 256-scaled trig, dual-path like arc_point/conic_theta: the LUT
    // lives behind USE_INTEGER_GEOMETRY, desktop uses libm
    int cos256 = 256;
    int sin256 = 0;
#if defined(USE_INTEGER_GEOMETRY)
    cos256 = arc_cos_q(deg);
    sin256 = arc_sin_q(deg);
#else
    {
        const double a = deg * kDegToRad;
        const double c = std::cos(a) * 256.0;
        const double s = std::sin(a) * 256.0;
        cos256 = static_cast<int>(c >= 0.0 ? c + 0.5 : c - 0.5);
        sin256 = static_cast<int>(s >= 0.0 ? s + 0.5 : s - 0.5);
    }
#endif
    // rotated corners bound the raster walk (one spare pixel ring)
    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    {
        bool first = true;
        const int cxs[2] = {x, x + w};
        const int cys[2] = {y, y + h};
        for (const int qx : cxs)
        {
            for (const int qy : cys)
            {
                const int64_t dx = static_cast<int64_t>(qx - pivot_x) * 256;
                const int64_t dy = static_cast<int64_t>(qy - pivot_y) * 256;
                const int rx =
                    pivot_x + static_cast<int>((dx * cos256 - dy * sin256) / 65536);
                const int ry =
                    pivot_y + static_cast<int>((dx * sin256 + dy * cos256) / 65536);
                if (first)
                {
                    bx0 = bx1 = rx;
                    by0 = by1 = ry;
                    first = false;
                }
                else
                {
                    if (rx < bx0)
                    {
                        bx0 = rx;
                    }
                    if (rx > bx1)
                    {
                        bx1 = rx;
                    }
                    if (ry < by0)
                    {
                        by0 = ry;
                    }
                    if (ry > by1)
                    {
                        by1 = ry;
                    }
                }
            }
        }
        --bx0;
        --by0;
        ++bx1;
        ++by1;
    }
    const int64_t hw = static_cast<int64_t>(w) * 256 / 2;
    const int64_t hh = static_cast<int64_t>(h) * 256 / 2;
    const int64_t rr = static_cast<int64_t>(r) * 256;
    const int64_t ex = hw - rr;
    const int64_t ey = hh - rr;
    // box center into the pivot frame, UNROTATED: the SDF tests
    // the inverse-mapped pixel against the box where it started,
    // so the center stays put while only pixels rotate back
    const int64_t dcx = static_cast<int64_t>(x) * 256 + hw -
                        static_cast<int64_t>(pivot_x) * 256;
    const int64_t dcy = static_cast<int64_t>(y) * 256 + hh -
                        static_cast<int64_t>(pivot_y) * 256;
    for (int row = by0; row <= by1; ++row)
    {
        for (int col = bx0; col <= bx1; ++col)
        {
            // pixel center into the pivot frame, rotated back by -angle
            const int64_t dx = (static_cast<int64_t>(col - pivot_x) * 256) + 128;
            const int64_t dy = (static_cast<int64_t>(row - pivot_y) * 256) + 128;
            const int64_t lx = (dx * cos256 + dy * sin256) / 256 - dcx;
            const int64_t ly = (-dx * sin256 + dy * cos256) / 256 - dcy;
            const int64_t qx = (lx >= 0 ? lx : -lx) - ex;
            const int64_t qy = (ly >= 0 ? ly : -ly) - ey;
            int64_t d;
            if (qx > 0 && qy > 0)
            {
                d = isqrt_floor(qx * qx + qy * qy) - rr;
            }
            else
            {
                // straight edge / interior: the corner radius still
                // stands off the distance (missing it thins every
                // straight run by r and clips r off each end)
                d = (qx > qy ? qx : qy) - rr;
            }
            // coverage: 1px linear ramp over the zero crossing
            int64_t cov = (128 - d) * 255 + 128;
            cov >>= 8;
            if (cov <= 0)
            {
                continue;
            }
            plot_aa(col, row, cov > 255 ? 255 : static_cast<int>(cov), colr);
        }
    }
}

void Graphics::draw_wireframe_grid(const int spacing, const Color &colr)
{
    if (spacing <= 0)
    {
        return;
    }
    // 1px dots on the spacing lattice across the (closed) draw area;
    // through draw_pixel, so clip/damage gates apply unchanged
    for (int y = draw_area.start_y; y <= draw_area.end_y; ++y)
    {
        if (y % spacing != 0)
        {
            continue;
        }
        for (int x = draw_area.start_x; x <= draw_area.end_x; ++x)
        {
            if (x % spacing == 0)
            {
                draw_pixel(x, y, colr);
            }
        }
    }
}

void Graphics::fill_gradient(int x1, int y1, int x2, int y2, const Color &from, const Color &to, const bool horizontal,
                             const int radius)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_rect(x1, y1, x2, y2, from);  // S-1: no gradient fill; from-outline
        return;
    }
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    // corner radius shared with the round-rect pair: clamp to half the
    // shorter side, non-positive keeps the square fast path
    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);

    // fractional chord edge (the fill_round_rect_aa formula): the two
    // pixels just outside the span blend by coverage
    auto fringe = [&](const int lx, const int rx, const int row, const Color &lc, const Color &rc, const int fl, const int fr) {
        if (fl > 0)
        {
            plot_aa(lx - 1, row, fl, lc);
        }
        if (fr > 0)
        {
            plot_aa(rx + 1, row, fr, rc);
        }
    };

    if (horizontal)
    {
        const int steps = right - left;
        auto col_color = [&](const int col) {
            return steps == 0 ? from : lerp_color(from, to, col - left, steps);
        };
        if (r == 0)
        {
            // square fast path: one span per column (the old body)
            for (int col = left; col <= right; ++col)
            {
                draw_line(col, top, col, bottom, col_color(col));
            }
            return;
        }
        for (int row = top; row <= bottom; ++row)
        {
            const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
            if (sp.lx > sp.rx)
            {
                continue;
            }
            for (int col = sp.lx; col <= sp.rx; ++col)
            {
                draw_pixel(col, row, col_color(col));
            }
            fringe(sp.lx, sp.rx, row, col_color(sp.lx), col_color(sp.rx),
                   sp.fl, sp.fr);
        }
    }
    else
    {
        const int steps = bottom - top;
        for (int row = top; row <= bottom; ++row)
        {
            const Color c = steps == 0 ? from : lerp_color(from, to, row - top, steps);
            const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
            if (sp.lx > sp.rx)
            {
                continue;
            }
            draw_line(sp.lx, row, sp.rx, row, c);
            fringe(sp.lx, sp.rx, row, c, c, sp.fl, sp.fr);
        }
    }
}

void Graphics::fill_radial(int x1, int y1, int x2, int y2, const int cx, const int cy, const Color &from,
                           const int from_p, const Color &to, const int to_p, const int radius)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_rect(x1, y1, x2, y2, from);  // S-1: bones only
        return;
    }
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);

    // CSS farthest-corner extent: the radius reaches the remotest rect
    // corner from the center (cx/cy are rect-local pixels)
    const int acx = left + cx;
    const int acy = top + cy;
    int64_t rad2 = 0;
    const int cxs[2] = {left, right};
    const int cys[2] = {top, bottom};
    for (const int px : cxs)
    {
        for (const int py : cys)
        {
            const int64_t d2 =
                1LL * (px - acx) * (px - acx) + 1LL * (py - acy) * (py - acy);
            if (d2 > rad2)
            {
                rad2 = d2;
            }
        }
    }
    const int rad = static_cast<int>(isqrt_floor(rad2));
    const int span = to_p - from_p;

    auto shade = [&](const int x, const int y) {
        if (rad <= 0)
        {
            return from;
        }
        const int64_t d2 = 1LL * (x - acx) * (x - acx) + 1LL * (y - acy) * (y - acy);
        const int pct = static_cast<int>(isqrt_floor(d2) * 100 / rad);
        if (pct <= from_p)
        {
            return from;
        }
        if (pct >= to_p || span <= 0)
        {
            return to;
        }
        return lerp_color(from, to, pct - from_p, span);
    };

    for (int row = top; row <= bottom; ++row)
    {
        const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
        for (int col = sp.lx; col <= sp.rx; ++col)
        {
            draw_pixel(col, row, shade(col, row));
        }
        if (sp.fl > 0)
        {
            plot_aa(sp.lx - 1, row, sp.fl, shade(sp.lx, row));
        }
        if (sp.fr > 0)
        {
            plot_aa(sp.rx + 1, row, sp.fr, shade(sp.rx, row));
        }
    }
}

void Graphics::fill_conic(int x1, int y1, int x2, int y2, int from_deg, const int *stop_deg,
                           const Color *stop_col, int nstops, int radius)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_rect(x1, y1, x2, y2, nstops > 0 ? stop_col[0] : Color{});
        return;
    }
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;
    if (stop_deg == nullptr || stop_col == nullptr || nstops < 2)
    {
        return;
    }

    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);

    // continuous center via doubled offsets: the integer-midpoint
    // center put even boxes' sweep origin half a pixel off (the knob's
    // dark sector sat one degree high). conic_theta is scale-invariant,
    // so the doubled vector keeps the 1-degree resolution.
    const int c2x = 2 * left + (right - left + 1);
    const int c2y = 2 * top + (bottom - top + 1);
    int from = from_deg % 360;
    if (from < 0)
    {
        from += 360;
    }

    auto shade = [&](const int x, const int y) {
        int t = conic_theta(2 * x + 1 - c2x, 2 * y + 1 - c2y) - from;
        t %= 360;
        if (t < 0)
        {
            t += 360;
        }
        // last stop at or below t owns the pixel (stops arrive
        // clamped non-decreasing from the builder)
        int seg = 0;
        for (int i = 1; i < nstops; ++i)
        {
            if (stop_deg[i] <= t)
            {
                seg = i;
            }
        }
        if (seg >= nstops - 1)
        {
            return stop_col[nstops - 1];
        }
        const int span = stop_deg[seg + 1] - stop_deg[seg];
        if (span <= 0)
        {
            return stop_col[seg];
        }
        return lerp_color(stop_col[seg], stop_col[seg + 1], t - stop_deg[seg], span);
    };

    for (int row = top; row <= bottom; ++row)
    {
        const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
        for (int col = sp.lx; col <= sp.rx; ++col)
        {
            draw_pixel(col, row, shade(col, row));
        }
        if (sp.fl > 0)
        {
            plot_aa(sp.lx - 1, row, sp.fl, shade(sp.lx, row));
        }
        if (sp.fr > 0)
        {
            plot_aa(sp.rx + 1, row, sp.fr, shade(sp.rx, row));
        }
    }
}

void Graphics::fill_gradient3(int x1, int y1, int x2, int y2, const Color &from, const Color &mid, int mid_p,
                               const Color &to, const bool horizontal, const int radius)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_rect(x1, y1, x2, y2, from);  // S-1: bones only
        return;
    }
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);
    const int mp = mid_p < 0 ? 0 : (mid_p > 100 ? 100 : mid_p);

    // two straight segments meeting exactly at mid (integer grid:
    // the mid column/row reads mid on both sides)
    auto ramp = [&](const int t, const int steps) {
        if (steps <= 0)
        {
            return from;
        }
        const int t_mid = mp * steps / 100;
        if (t <= t_mid)
        {
            if (t_mid <= 0)
            {
                return mid;
            }
            return lerp_color(from, mid, t, t_mid);
        }
        if (steps - t_mid <= 0)
        {
            return to;
        }
        return lerp_color(mid, to, t - t_mid, steps - t_mid);
    };

    auto fringe = [&](const int lx, const int rx, const int row, const Color &lc, const Color &rc, const int fl, const int fr) {
        if (fl > 0)
        {
            plot_aa(lx - 1, row, fl, lc);
        }
        if (fr > 0)
        {
            plot_aa(rx + 1, row, fr, rc);
        }
    };

    if (horizontal)
    {
        const int steps = right - left;
        auto col_color = [&](const int col) { return ramp(col - left, steps); };
        if (r == 0)
        {
            for (int col = left; col <= right; ++col)
            {
                draw_line(col, top, col, bottom, col_color(col));
            }
            return;
        }
        for (int row = top; row <= bottom; ++row)
        {
            const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
            if (sp.lx > sp.rx)
            {
                continue;
            }
            for (int col = sp.lx; col <= sp.rx; ++col)
            {
                draw_pixel(col, row, col_color(col));
            }
            fringe(sp.lx, sp.rx, row, col_color(sp.lx), col_color(sp.rx),
                   sp.fl, sp.fr);
        }
    }
    else
    {
        const int steps = bottom - top;
        for (int row = top; row <= bottom; ++row)
        {
            const Color c = ramp(row - top, steps);
            const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
            if (sp.lx > sp.rx)
            {
                continue;
            }
            draw_line(sp.lx, row, sp.rx, row, c);
            fringe(sp.lx, sp.rx, row, c, c, sp.fl, sp.fr);
        }
    }
}

void Graphics::fill_linear_stops(int x1, int y1, int x2, int y2, const int *stop_pos,
                                   const Color *stop_col, int nstops, const bool horizontal,
                                   const int radius)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_rect(x1, y1, x2, y2, (nstops > 0 && stop_col != nullptr)
                                      ? stop_col[0]
                                      : Color{});  // S-1: bones only
        return;
    }
    if (stop_pos == nullptr || stop_col == nullptr || nstops < 2)
    {
        return;
    }
    const int n = nstops > 8 ? 8 : nstops;
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);

    // N-stop ramp over percent positions (non-decreasing by contract):
    // the last stop at or below t owns the pixel, zero-length spans
    // read flat (the conic segment rule)
    auto ramp = [&](const int t, const int steps) {
        if (steps <= 0)
        {
            return stop_col[0];
        }
        const int tp = t * 100 / steps;
        int seg = 0;
        for (int i = 1; i < n; ++i)
        {
            if (stop_pos[i] <= tp)
            {
                seg = i;
            }
        }
        if (seg >= n - 1)
        {
            return stop_col[n - 1];
        }
        const int span = stop_pos[seg + 1] - stop_pos[seg];
        if (span <= 0)
        {
            return stop_col[seg];
        }
        return lerp_color(stop_col[seg], stop_col[seg + 1], tp - stop_pos[seg], span);
    };

    // fractional chord edge (the fill_round_rect_aa formula): the two
    // pixels just outside the span blend by coverage
    auto fringe = [&](const int lx, const int rx, const int row, const Color &lc, const Color &rc, const int fl, const int fr) {
        if (fl > 0)
        {
            plot_aa(lx - 1, row, fl, lc);
        }
        if (fr > 0)
        {
            plot_aa(rx + 1, row, fr, rc);
        }
    };

    if (horizontal)
    {
        const int steps = right - left;
        if (r == 0)
        {
            // square fast path: one span per column
            for (int col = left; col <= right; ++col)
            {
                draw_line(col, top, col, bottom, ramp(col - left, steps));
            }
            return;
        }
        for (int row = top; row <= bottom; ++row)
        {
            const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
            if (sp.lx > sp.rx)
            {
                continue;
            }
            for (int col = sp.lx; col <= sp.rx; ++col)
            {
                draw_pixel(col, row, ramp(col - left, steps));
            }
            fringe(sp.lx, sp.rx, row, ramp(sp.lx - left, steps),
                   ramp(sp.rx - left, steps), sp.fl, sp.fr);
        }
    }
    else
    {
        const int steps = bottom - top;
        for (int row = top; row <= bottom; ++row)
        {
            const Color c = ramp(row - top, steps);
            const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
            if (sp.lx > sp.rx)
            {
                continue;
            }
            draw_line(sp.lx, row, sp.rx, row, c);
            fringe(sp.lx, sp.rx, row, c, c, sp.fl, sp.fr);
        }
    }
}

void Graphics::fill_repeating(int x1, int y1, int x2, int y2, const bool horizontal, const int period,
                               const int *stop_pos, const Color *stop_col, int nstops, const int radius)
{    if (stop_pos == nullptr || stop_col == nullptr || nstops < 2)
    {
        return;
    }
    if (render_mode_ == render_mode::wireframe)
    {
        draw_rect(x1, y1, x2, y2, stop_col[0]);  // S-1: bones only
        return;
    }
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;
    if (period <= 0)
    {
        return;
    }
    if (nstops > 6)
    {
        nstops = 6;
    }

    int r = inscribed_radius(right - left + 1, bottom - top + 1, radius);

    auto stripe = [&](const int t) {
        int m = t % period;
        if (m < 0)
        {
            m += period;
        }
        // last stop at or below m owns the pixel; below the first
        // stop wraps into the previous period's tail (which lerps
        // into this period's head)
        int seg = -1;
        for (int i = 0; i < nstops; ++i)
        {
            if (stop_pos[i] <= m)
            {
                seg = i;
            }
        }
        int lo = 0;
        int hi = 0;
        const Color *c0 = &stop_col[nstops - 1];
        const Color *c1 = &stop_col[0];
        if (seg < 0)
        {
            // m is already in [0, period); wrap into the previous
            // period's tail without shifting m (shifting would push the
            // lerp weight past 1 into garbage colors)
            lo = stop_pos[nstops - 1] - period;
            hi = stop_pos[0];
        }
        else if (seg >= nstops - 1)
        {
            lo = stop_pos[nstops - 1];
            hi = stop_pos[0] + period;
            c0 = &stop_col[nstops - 1];
            c1 = &stop_col[0];
        }
        else
        {
            lo = stop_pos[seg];
            hi = stop_pos[seg + 1];
            c0 = &stop_col[seg];
            c1 = &stop_col[seg + 1];
        }
        Color c = (hi <= lo) ? *c0 : lerp_color(*c0, *c1, m - lo, hi - lo);
        // no faint-stop gate here: on binary-alpha depths the authored
        // translucency is already lost in the color (any nonzero alpha
        // sets the bit), so every stop follows the standard binary
        // rule like all other paint — see the contract note
        return c;
    };

    if (r == 0)
    {
        // square fast path: the color rides one axis, so whole spans
        // paint at once (the translucent stops still blend per pixel
        // through draw_line -> draw_pixel)
        if (horizontal)
        {
            for (int col = left; col <= right; ++col)
            {
                draw_line(col, top, col, bottom, stripe(col - left));
            }
        }
        else
        {
            for (int row = top; row <= bottom; ++row)
            {
                draw_line(left, row, right, row, stripe(row - top));
            }
        }
        return;
    }
    auto fringe = [&](const int lx, const int rx, const int row, const Color &lc, const Color &rc, const int fl, const int fr) {
        if (fl > 0)
        {
            plot_aa(lx - 1, row, fl, lc);
        }
        if (fr > 0)
        {
            plot_aa(rx + 1, row, fr, rc);
        }
    };
    for (int row = top; row <= bottom; ++row)
    {
        const span_q sp = rounded_span_q(left, top, right, bottom, r, row);
        for (int col = sp.lx; col <= sp.rx; ++col)
        {
            draw_pixel(col, row, horizontal ? stripe(col - left) : stripe(row - top));
        }
        if (sp.fl > 0 || sp.fr > 0)
        {
            const Color lc = horizontal ? stripe(sp.lx - left) : stripe(row - top);
            const Color rc = horizontal ? stripe(sp.rx - left) : stripe(row - top);
            fringe(sp.lx, sp.rx, row, lc, rc, sp.fl, sp.fr);
        }
    }
}

void Graphics::draw_line_aa(int x1, int y1, int x2, int y2, const Color &colr,
                            const bool skip_first)
{
    if (x1 == x2 || y1 == y2)
    {
        if (skip_first)
        {
            // the joint pixel belongs to the previous segment: step one
            // past it, or draw nothing when the run collapses to it
            if (x1 == x2 && y1 == y2)
            {
                return;
            }
            if (x1 == x2)
            {
                const int step = y2 > y1 ? 1 : -1;
                draw_line(x1, y1 + step, x2, y2, colr);
                return;
            }
            const int step = x2 > x1 ? 1 : -1;
            draw_line(x1 + step, y1, x2, y2, colr);
            return;
        }
        draw_line(x1, y1, x2, y2, colr);  // axis-aligned runs need no coverage
        return;
    }

    // The skipped pixel is the CALLER's start (x1, y1) — the arc's prev
    // sample. The transpose/flip normalization below reorders the
    // endpoints, so the skip has to follow the start through the swaps;
    // skipping whatever lands at x1 afterwards dropped flat-region
    // samples whose neighbors never replotted them (cw/ccw arcs
    // rasterized differently).
    const int sx = x1;
    const int sy = y1;

    // transpose steep lines so x always drives the walk
    const bool steep = std::abs(y2 - y1) > std::abs(x2 - x1);
    if (steep)
    {
        std::swap(x1, y1);
        std::swap(x2, y2);
    }
    if (x2 < x1)
    {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    // the caller's start after normalization (steep swaps coordinates,
    // the flip swaps endpoints)
    const bool start_at_x1 = steep ? (x1 == sy && y1 == sx)
                                   : (x1 == sx && y1 == sy);
    const bool skip_x1 = skip_first && start_at_x1;
    const bool skip_x2 = skip_first && !start_at_x1;
    const int dx = x2 - x1;
    const int dy = y2 - y1;
    const auto plot = [&](const int cx, const int cy, const int cov)
    {
        if (steep)
        {
            plot_aa(cy, cx, cov, colr);
        }
        else
        {
            plot_aa(cx, cy, cov, colr);
        }
    };

    if (!skip_x1)
    {
        plot(x1, y1, 255);
    }
    if (!skip_x2)
    {
        plot(x2, y2, 255);
    }

    // exact rational walk (no fixed-point drift): ideal y = y1 +
    // dy*(cx-x1)/dx; pixel floor(y) gets 255-frac8, pixel floor(y)+1
    // gets frac8 -- Wu's two-pixel split with the remainder's weight
    int64_t p = 0;
    for (int cx = x1 + 1; cx < x2; ++cx)
    {
        p += dy;
        int64_t q = p / dx;
        if (p % dx < 0)
        {
            --q;  // floor division: the ideal line may dip below y1
        }
        const int64_t rem = p - q * dx;
        const int yy = y1 + static_cast<int>(q);
        const int frac8 = static_cast<int>(rem * 255 / dx);
        plot(cx, yy, 255 - frac8);
        plot(cx, yy + 1, frac8);
    }
}

void Graphics::draw_circle_aa(int x, int y, int radius, const Color &colr)
{
    if (radius <= 0)
    {
        draw_pixel(x, y, colr);
        return;
    }

    // one chord per column offset: the exact boundary between py and
    // py+1 splits the coverage by the remainder's fraction. px == py is
    // impossible for integer radii (r*r == 2*px*px has no solution), and
    // each point belongs to exactly one iteration, so nothing is drawn
    // twice and the blend never stacks
    const auto plot4 = [&](const int dx_, const int dy_, const int cov)
    {
        if (dx_ == 0)
        {
            plot_aa(x, y + dy_, cov, colr);
            plot_aa(x, y - dy_, cov, colr);
        }
        else if (dy_ == 0)
        {
            plot_aa(x + dx_, y, cov, colr);
            plot_aa(x - dx_, y, cov, colr);
        }
        else
        {
            plot_aa(x + dx_, y + dy_, cov, colr);
            plot_aa(x - dx_, y + dy_, cov, colr);
            plot_aa(x + dx_, y - dy_, cov, colr);
            plot_aa(x - dx_, y - dy_, cov, colr);
        }
    };
    for (int px = 0; px <= radius; ++px)
    {
        const int64_t t = 1LL * radius * radius - 1LL * px * px;
        const int py = static_cast<int>(isqrt_floor(t));
        const int64_t rem = t - 1LL * py * py;
        const int frac8 = static_cast<int>(rem * 255 / (2 * py + 1));
        plot4(px, py, 255 - frac8);
        if (frac8 > 0)
        {
            plot4(px, py + 1, frac8);
        }
    }
}

void Graphics::draw_arc_aa(int cx, int cy, int radius, int start_deg, int sweep_deg, const Color &colr)
{
    if (sweep_deg == 0)
    {
        return;
    }
    if (radius <= 0)
    {
        draw_pixel(cx, cy, colr);
        return;
    }
    if (sweep_deg >= 360 || sweep_deg <= -360)
    {
        // the full ring: the exact-chord circle AA covers every pixel
        // exactly once, without the polyline closure seam
        draw_circle_aa(cx, cy, radius, colr);
        return;
    }

    const int dir = sweep_deg > 0 ? 1 : -1;
    const int sweep = dir * sweep_deg;  // positive magnitude

    if constexpr (Color::per_channel_blend)
    {
        // Each translucent pixel belongs to one octant; shared diagonal
        // and axis pixels must not composite the same color twice.
        if (colr.a() > 0 && colr.a() < 0xFF)
        {
            const int from = norm_deg(start_deg);
            // math angle from the CSS-convention theta: 0 = +x,
            // positive toward +y (visually clockwise, y down), so
            // phi = theta - 90 (east: 90 -> 0; south: 180 -> 90).
            // (The mirrored form here used to swap north/south,
            // dropping the tangent pixel at the top of round rects.)
            const auto inside = [&](const int dx, const int dy) {
                const int phi = norm_deg(conic_theta(dx, dy) - 90);
                if (dir > 0)
                {
                    return norm_deg(phi - from) <= sweep;
                }
                return norm_deg(from - phi) <= sweep;
            };
            const auto put = [&](const int dx, const int dy,
                                 const int cov) {
                if (inside(dx, dy))
                {
                    plot_aa(cx + dx, cy + dy, cov, colr);
                }
            };
            const auto mirror = [&](const int x, const int y,
                                    const int cov) {
                if (cov <= 0)
                {
                    return;
                }
                put(x, y, cov);
                if (x != 0)
                {
                    put(-x, y, cov);
                }
                if (y != 0)
                {
                    put(x, -y, cov);
                    if (x != 0)
                    {
                        put(-x, -y, cov);
                    }
                }
            };
            for (int minor = 0; minor <= radius; ++minor)
            {
                const int64_t t = 1LL * radius * radius - 1LL * minor * minor;
                const int major = static_cast<int>(isqrt_floor(t));
                if (minor > major + 1)
                {
                    break;
                }
                const int64_t rem = t - 1LL * major * major;
                const int frac8 =
                    static_cast<int>(rem * 255 / (2LL * major + 1));
                const auto octants = [&](const int edge, const int cov) {
                    if (minor <= edge)
                    {
                        mirror(minor, edge, cov);
                    }
                    if (minor < edge)
                    {
                        mirror(edge, minor, cov);
                    }
                };
                octants(major, 255 - frac8);
                octants(major + 1, frac8);
            }
            return;
        }
    }

    // sample step keeps the chord length within ~1px: chord ~= radius *
    // step_deg * pi/180 -> step_deg = ceil(57.3 / radius), floored at 1
    // degree so large radii keep chords at or below one pixel
    int step = (radius + 56) / radius;
    if (step < 1)
    {
        step = 1;
    }
    const int nseg = (sweep + step - 1) / step;

    int prev_x = 0, prev_y = 0;
    arc_point(norm_deg(start_deg), cx, cy, radius, prev_x, prev_y);
    for (int k = 1; k <= nseg; ++k)
    {
        const int deg = norm_deg(start_deg + dir * (k * step < sweep ? k * step : sweep));
        int x = 0, y = 0;
        arc_point(deg, cx, cy, radius, x, y);
        // joints belong to the previous segment (k == 1 owns the arc
        // start); re-plotting them would stack a translucent color
        draw_line_aa(prev_x, prev_y, x, y, colr, k != 1);
        prev_x = x;
        prev_y = y;
    }
}

void zb::ui::core::point_on_circle(const int cx, const int cy, const int radius, const int deg, int *ox, int *oy)
{
    arc_point(norm_deg(deg), cx, cy, radius, *ox, *oy);
}

/** draw 8 pixels for circle */
void Graphics::draw_8pixels(int x, int y, int px, int py, const Color &colr)
{
    draw_pixel((x + px), (y + py), colr);
    draw_pixel((x + px), (y - py), colr);
    draw_pixel((x - px), (y + py), colr);
    draw_pixel((x - px), (y - py), colr);
    draw_pixel((x + py), (y + px), colr);
    draw_pixel((x + py), (y - px), colr);
    draw_pixel((x - py), (y + px), colr);
    draw_pixel((x - py), (y - px), colr);
}

/** fill the pixels within a circle by horizontal scanlines, one line per octant */
void Graphics::fill_triangle(
    int x1, int y1,
    int x2, int y2,
    int x3, int y3,
    const Color &colr)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_triangle(x1, y1, x2, y2, x3, y3, colr);  // S-1: bones only
        return;
    }
    // sort the vertices top to bottom; the long edge (a-c) spans the
    // whole triangle, the other two edges meet at the middle vertex b
    int xa = x1, ya = y1, xb = x2, yb = y2, xc = x3, yc = y3;
    const auto sort_vertices = [](int &xa, int &ya, int &xb, int &yb)
    {
        if (yb < ya)
        {
            std::swap(xa, xb);
            std::swap(ya, yb);
        }
    };
    sort_vertices(xa, ya, xb, yb);
    sort_vertices(xa, ya, xc, yc);
    sort_vertices(xb, yb, xc, yc);
    if (ya == yc)
    {
        return;  // degenerate: all vertices on one row
    }

    // x on the edge (x1,y1)-(x2,y2) at row y; y1 == y2 keeps the vertex x
    const auto edge_x = [](const int y, const int x1, const int y1, const int x2, const int y2) -> int
    {
        const int dy = y2 - y1;
        if (dy == 0)
        {
            return x1;
        }
        return x1 + (x2 - x1) * (y - y1) / dy;
    };

    // scanline fill: one draw_line per row between the long edge and the
    // active short edge, flat top/bottom triangles fall out naturally
    for (int y = ya; y <= yc; y++)
    {
        const int x_long = edge_x(y, xa, ya, xc, yc);
        const int x_other = y <= yb ? edge_x(y, xa, ya, xb, yb) : edge_x(y, xb, yb, xc, yc);
        if (x_long == x_other)
        {
            draw_pixel(x_long, y, colr);
        }
        else
        {
            draw_line(std::min(x_long, x_other), y, std::max(x_long, x_other), y, colr);
        }
    }
}

void Graphics::draw_incir_pixels(int x, int y, int px, int py, const Color &colr)
{
    draw_line((x - px), (y + py), (x + px), (y + py), colr);
    draw_line((x - px), (y - py), (x + px), (y - py), colr);
    draw_line((x - py), (y + px), (x + py), (y + px), colr);
    draw_line((x - py), (y - px), (x + py), (y - px), colr);
}

void Graphics::draw_ellipse(int cx, int cy, int rx, int ry, const Color &colr)
{
    // int64 accumulators: rx*rx overflows int from rx > 46340 and
    // 2*rx*rx already from rx > 32767 -- the midpoint scan corrupted
    // silently on huge radii
    const int64_t i64rx = rx, i64ry = ry;
    int x = rx, y = 0;
    const int64_t sx = i64rx * i64rx,
                  sy = i64ry * i64ry,
                  sx2 = 2 * sx,
                  sy2 = 2 * sy;
    int64_t dx = 2 * sy * x,
            dy = 2 * sx * y,
            d = sx - sy * i64rx;  // the old `+ sy * 0.25` truncated to 0

    while (dy < dx)
    {
        draw_pixel(cx + x, cy + y, colr);
        draw_pixel(cx + x, cy - y, colr);
        draw_pixel(cx - x, cy + y, colr);
        draw_pixel(cx - x, cy - y, colr);

        y++;
        if (d < 0)
        {
            dy += sx2;
            d += dy + sx;
        }
        else
        {
            x--;
            dx -= sy2;
            dy += sx2;
            d += dy - dx + sx;
        }
    }

    d = sx * ((int64_t)y * y + y) + sy * ((int64_t)x - 1) * (x - 1) - sy * sx;

    while (x >= 0)
    {
        draw_pixel(cx + x, cy + y, colr);
        draw_pixel(cx + x, cy - y, colr);
        draw_pixel(cx - x, cy + y, colr);
        draw_pixel(cx - x, cy - y, colr);

        x--;
        if (d > 0)
        {
            dx -= sy2;
            d += sy - dx;
        }
        else
        {
            y++;
            dx -= sy2;
            dy += sx2;
            d += dy - dx + sy;
        }
    }
}

void Graphics::fill_ellipse(int cx, int cy, int rx, int ry, const Color &colr)
{
    if (render_mode_ == render_mode::wireframe)
    {
        draw_ellipse(cx, cy, rx, ry, colr);  // S-1: bones only
        return;
    }
    if (rx <= 0 || ry <= 0)
    {
        return;
    }
    // clamp to the int64/isqrt domain: rx^2*ry^2 must stay below 2^62
    // (46340^2 < 2^31) or isqrt_u64's shift prologue never terminates.
    // An ellipse larger than any surface is clipped to the same pixels
    const int crx = rx > 46340 ? 46340 : rx;
    const int cry = ry > 46340 ? 46340 : ry;
    // scanline fill: one draw_line per row instead of testing every pixel.
    // USE_INTEGER_GEOMETRY (FPU-less targets) computes the half-width as
    // floor(isqrt(rx^2*ry^2 - rx^2*y^2)/ry) with 64-bit integer arithmetic,
    // exactly floor(rx*sqrt(1-(y/ry)^2)) -- same pixels as the float path.
#if defined(USE_INTEGER_GEOMETRY)
    const int64_t rx2 = 1LL * crx * crx;
    const int64_t ry2 = 1LL * cry * cry;
    const int64_t rx2ry2 = rx2 * ry2;
    for (int y = -cry; y <= cry; y++)
    {
        const int64_t term = rx2ry2 - rx2 * (1LL * y * y);
        const int dx = static_cast<int>(isqrt_u64(static_cast<uint64_t>(term)) / cry);
        draw_line(cx - dx, cy + y, cx + dx, cy + y, colr);
    }
#else
    const double rx2 = static_cast<double>(crx) * crx;
    const double ry2 = static_cast<double>(cry) * cry;
    for (int y = -cry; y <= cry; y++)
    {
        const double t = 1.0 - (static_cast<double>(y) * y) / ry2;
        if (t < 0.0)
        {
            continue;
        }
        const int dx = static_cast<int>(crx * std::sqrt(t));
        draw_line(cx - dx, cy + y, cx + dx, cy + y, colr);
    }
#endif
}

void Graphics::draw_bezier_curve(const impoint_t &p1, const impoint_t &p2, const Color &colr, float accuracy)
{
    // integer step count: the endpoint (t == 1) is always sampled exactly
    // and a non-positive accuracy cannot spin the loop forever
    int steps = 1;
    if (accuracy > 0.0f)
    {
        steps = static_cast<int>(1.0f / accuracy);
        if (steps < 1)
        {
            steps = 1;
        }
    }
    for (int i = 0; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        auto x = p1.x + (p2.x - p1.x) * t;
        auto y = p1.y + (p2.y - p1.y) * t;
        draw_pixel(x, y, colr);
    }
}

void Graphics::draw_bezier_curve(const impoint_t &p1, const impoint_t &p2, const impoint_t &p3, const Color &colr, float accuracy)
{
    int steps = 1;
    if (accuracy > 0.0f)
    {
        steps = static_cast<int>(1.0f / accuracy);
        if (steps < 1)
        {
            steps = 1;
        }
    }
    for (int i = 0; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        auto x = pow(1 - t, 2) * p1.x + 2 * (1 - t) * t * p2.x + pow(t, 2) * p3.x;
        auto y = pow(1 - t, 2) * p1.y + 2 * (1 - t) * t * p2.y + pow(t, 2) * p3.y;
        draw_pixel(x, y, colr);
    }
}

void Graphics::draw_bezier_curve(const impoint_t &p1, const impoint_t &p2, const impoint_t &p3, const impoint_t &p4, const Color &colr, float accuracy)
{
    int steps = 1;
    if (accuracy > 0.0f)
    {
        steps = static_cast<int>(1.0f / accuracy);
        if (steps < 1)
        {
            steps = 1;
        }
    }
    for (int i = 0; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        auto x = pow(1 - t, 3) * p1.x + 3 * pow(1 - t, 2) * t * p2.x + 3 * (1 - t) * pow(t, 2) * p3.x + pow(t, 3) * p4.x;
        auto y = pow(1 - t, 3) * p1.y + 3 * pow(1 - t, 2) * t * p2.y + 3 * (1 - t) * pow(t, 2) * p3.y + pow(t, 3) * p4.y;
        draw_pixel(x, y, colr);
    }
}