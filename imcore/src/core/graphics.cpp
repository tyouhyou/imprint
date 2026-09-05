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
#if defined(USE_INTEGER_GEOMETRY)
        return static_cast<int>(isqrt_u64(static_cast<uint64_t>(r * r - dy * dy)));
#else
        return static_cast<int>(std::sqrt(static_cast<double>(r * r - dy * dy)));
#endif
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
}  // namespace

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

    int nJudgeX = -nIy;
    int nJudgeY = -nIx;
    int x = x1;
    int y = y1;

    nInc--;
    int nTwoIx = 2 * nIx;
    int nTwoIy = 2 * nIy;

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

void Graphics::draw_rect(int x1, int y1, int x2, int y2, const Color &colr)
{
    draw_line(x1, y1, x1, y2, colr);
    draw_line(x1, y1, x2, y1, colr);
    draw_line(x1, y2, x2, y2, colr);
    draw_line(x2, y1, x2, y2, colr);
}

void Graphics::fill_round_rect(int x1, int y1, int x2, int y2, int radius, const Color &colr)
{
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    int r = radius < 0 ? 0 : radius;
    const int half = std::min(right - left, bottom - top) / 2;
    if (r > half)
    {
        r = half;
    }
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

    int r = radius < 0 ? 0 : radius;
    const int half = std::min(right - left, bottom - top) / 2;
    if (r > half)
    {
        r = half;
    }
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

void Graphics::fill_gradient(int x1, int y1, int x2, int y2, const Color &from, const Color &to, const bool horizontal)
{
    const int left = x1 < x2 ? x1 : x2;
    const int right = x1 < x2 ? x2 : x1;
    const int top = y1 < y2 ? y1 : y2;
    const int bottom = y1 < y2 ? y2 : y1;

    if (horizontal)
    {
        const int steps = right - left;
        for (int col = left; col <= right; ++col)
        {
            const Color c = steps == 0 ? from : lerp_color(from, to, col - left, steps);
            draw_line(col, top, col, bottom, c);
        }
    }
    else
    {
        const int steps = bottom - top;
        for (int row = top; row <= bottom; ++row)
        {
            const Color c = steps == 0 ? from : lerp_color(from, to, row - top, steps);
            draw_line(left, row, right, row, c);
        }
    }
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