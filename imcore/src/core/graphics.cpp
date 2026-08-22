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

Graphics::Graphics(const uint32_t &width, const uint32_t &height, void *data)
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

Graphics::ptr Graphics::clone(const int &x, const int &y, const int32_t &width, const int32_t &height) const
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

Graphics::ClipGuard Graphics::clip_safe(const int &x, const int &y, const int32_t &width, const int32_t &height)
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

void Graphics::set_draw_area(const int &x, const int &y, const int &width, const int &height)
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
    const int &img_width,      // bitmap widht
    const int &img_height,     // bitmap height
    const int &img_row_stride, // pixel amount in one row
    const int &start_x,        // x coordinate in the graphic where starting to draw bitmap
    const int &start_y         // y coordinate in the graphic where starting to draw bitmap
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
    const int &start_x,
    const int &start_y)
{
    const int stride = img.row_stride > 0 ? img.row_stride : img.width;
    draw_image(img.pixels, img.width, img.height, stride, start_x, start_y);
}

void Graphics::fill(const Color &colr)
{
    auto draw_width = draw_area.end_x - draw_area.start_x + 1;
    for (int row = draw_area.start_y; row <= draw_area.end_y; row++)
    {
        std::fill_n(pixels + (imsize.width * row + draw_area.start_x), draw_width, colr);
    }
}

void Graphics::draw_pixel(const int &x, const int &y, const Color &colr)
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
    const uint32_t alpha = front_color.rgb.a;
    if (alpha <= 0)
    {
        return back_color;
    }
    if constexpr (ImColor_Depth == 16)
    {
        // color16 carries a single alpha bit: opacity is binary. The
        // 8-bit blend math below would treat the bit as a 1/255 weight
        // and make every covered pixel nearly transparent
        return front_color;
    }
    if (alpha >= 0xFF)
    {
        return front_color;
    }

    const uint32_t inv_alpha = 0xFF - alpha;
    Color rst{};
    rst.rgb.r = (uint8_t)((front_color.rgb.r * alpha + back_color.rgb.r * inv_alpha) / 0xFF);
    rst.rgb.g = (uint8_t)((front_color.rgb.g * alpha + back_color.rgb.g * inv_alpha) / 0xFF);
    rst.rgb.b = (uint8_t)((front_color.rgb.b * alpha + back_color.rgb.b * inv_alpha) / 0xFF);
    rst.rgb.a = (uint8_t)(alpha + back_color.rgb.a * inv_alpha / 0xFF); // source-over alpha
    return rst;
}

void Graphics::fill_rect(const int &x1, const int &y1, const int &x2, const int &y2, const Color &colr)
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

void Graphics::draw_line(const int &x1, const int &y1, const int &x2, const int &y2, const Color &colr)
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

void Graphics::draw_circle(const int &x, const int &y, const int &radius, const Color &colr)
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

void Graphics::fill_circle(const int &x, const int &y, const int &radius, const Color &colr)
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

void Graphics::draw_rect(const int &x1, const int &y1, const int &x2, const int &y2, const Color &colr)
{
    draw_line(x1, y1, x1, y2, colr);
    draw_line(x1, y1, x2, y1, colr);
    draw_line(x1, y2, x2, y2, colr);
    draw_line(x2, y1, x2, y2, colr);
}

/** draw 8 pixels for circle */
void Graphics::draw_8pixels(const int &x, const int &y, const int &px, const int &py, const Color &colr)
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
    const int &x1, const int &y1,
    const int &x2, const int &y2,
    const int &x3, const int &y3,
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

void Graphics::draw_incir_pixels(const int &x, const int &y, const int &px, const int &py, const Color &colr)
{
    draw_line((x - px), (y + py), (x + px), (y + py), colr);
    draw_line((x - px), (y - py), (x + px), (y - py), colr);
    draw_line((x - py), (y + px), (x + py), (y + px), colr);
    draw_line((x - py), (y - px), (x + py), (y - px), colr);
}

void Graphics::draw_ellipse(const int &cx, const int &cy, const int &rx, const int &ry, const Color &colr)
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

void Graphics::fill_ellipse(const int &cx, const int &cy, const int &rx, const int &ry, const Color &colr)
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

void Graphics::draw_bezier_curve(const impoint_t &p1, const impoint_t &p2, const Color &colr, const float &accuracy)
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

void Graphics::draw_bezier_curve(const impoint_t &p1, const impoint_t &p2, const impoint_t &p3, const Color &colr, const float &accuracy)
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

void Graphics::draw_bezier_curve(const impoint_t &p1, const impoint_t &p2, const impoint_t &p3, const impoint_t &p4, const Color &colr, const float &accuracy)
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