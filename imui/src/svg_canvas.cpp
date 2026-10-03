#include "svg_canvas.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "text/bitmap_provider.hpp"
#include "theme.hpp"

namespace zb::ui
{
    namespace
    {
        // stroke geometry works in Q10 device pixels (1/1024 px): enough
        // precision for the 2x2 supersample grid (0.25 px = 256 exactly),
        // and every per-sample quantity stays inside int64 for any real
        // surface. Coordinates clamp to +-16000 px (the mapped viewBox
        // value can carry any parseable magnitude).
        constexpr int64_t kQ10 = 1024;
        constexpr int64_t kCoordMax = 16000;

        int64_t clamp_q10(const int64_t v)
        {
            if (v < -kCoordMax * kQ10)
            {
                return -kCoordMax * kQ10;
            }
            if (v > kCoordMax * kQ10)
            {
                return kCoordMax * kQ10;
            }
            return v;
        }

        // isqrt for the stroke scale (Q10 sqrt of a Q20 ratio): Newton
        // with an exact correction pass, no libm (the integer-math rule,
        // widget.cpp Gaussian precedent)
        int64_t isqrt64(int64_t v)
        {
            if (v <= 0)
            {
                return 0;
            }
            int64_t r = static_cast<int64_t>(1) << 31;
            do
            {
                r = (r + v / r) / 2;
            } while (r * r > v || (r + 1) * (r + 1) <= v);
            return r;
        }

        // one pixel from its 2x2 sample coverage: full coverage plots
        // the base color, partial coverage scales its alpha (32bpp) or
        // quantizes to the plot_aa half rule (16bpp binary alpha —
        // scaling the single-bit alpha would clear every fringe pixel)
        void plot_covered(core::Graphics &area, const int64_t px,
                          const int64_t py, const core::Color &base,
                          const int in)
        {
            if (in == 4)
            {
                area.draw_pixel(static_cast<int>(px), static_cast<int>(py),
                                base);
                return;
            }
            if constexpr (core::Color::per_channel_blend)
            {
                core::Color c = base;
                c.set_a(static_cast<uint8_t>(base.a() * in / 4));
                area.draw_pixel(static_cast<int>(px), static_cast<int>(py),
                                c);
            }
            else
            {
                if (in * 0xFF / 4 >= 128)
                {
                    area.draw_pixel(static_cast<int>(px),
                                    static_cast<int>(py), base);
                }
            }
        }

        // the shared flattener constants (code-contract §3.2)
        constexpr double kPathTol = 0.1;      // viewBox-unit chord tolerance
        constexpr int kPathMaxDepth = 12;     // <= 4096 emits per curve
        constexpr double kDegToRad = 0.017453292519943295;

        // sin/cos of degrees, libm-free: integer range reduction to
        // [0, 90) then a Taylor series (truncation <= ~3e-5 at the 90
        // degree boundary — sub-pixel at any realistic radius). Set-time
        // only; the embedded link has no libm trig.
        void sincos_deg(const double deg, double &s, double &c)
        {
            double x = deg;
            if (x < 0)
            {
                x = -x;
                // sin(-x) = -sin(x); cos stays even
            }
            long long turns = static_cast<long long>(x / 360.0);
            x -= static_cast<double>(turns) * 360.0;
            const int quad = static_cast<int>(x / 90.0);
            x -= static_cast<double>(quad) * 90.0;
            const double r = x * kDegToRad;
            const double r2 = r * r;
            double sn = r * (1.0 - r2 / 6.0 * (1.0 - r2 / 20.0 *
                             (1.0 - r2 / 42.0 * (1.0 - r2 / 72.0))));
            double cs = 1.0 - r2 / 2.0 * (1.0 - r2 / 12.0 *
                         (1.0 - r2 / 30.0 * (1.0 - r2 / 56.0)));
            switch (quad)
            {
                case 1: { const double t = sn; sn = cs; cs = -t; break; }
                case 2: sn = -sn; cs = -cs; break;
                case 3: { const double t = sn; sn = -cs; cs = t; break; }
                default: break;
            }
            if (deg < 0)
            {
                sn = -sn;
            }
            s = sn;
            c = cs;
        }
    }  // namespace

    // --- shared path flatteners (H-6; html.cpp calls these too) -------

    void svg_flatten_cubic(std::vector<std::pair<double, double>> &out,
                           const double x0, const double y0,
                           const double x1, const double y1,
                           const double x2, const double y2,
                           const double x3, const double y3,
                           const int depth)
    {
        // flatness without sqrt: the control points' squared
        // distance to the chord vs the tolerance (all doubles, no
        // overflow below any real coordinate magnitude)
        const double dx = x3 - x0;
        const double dy = y3 - y0;
        const double len2 = dx * dx + dy * dy;
        const double tol2 = kPathTol * kPathTol;
        bool flat = len2 <= tol2;
        if (!flat)
        {
            const double c1 = (x1 - x0) * dy - (y1 - y0) * dx;
            const double c2 = (x2 - x0) * dy - (y2 - y0) * dx;
            flat = c1 * c1 <= tol2 * len2 && c2 * c2 <= tol2 * len2;
        }
        if (flat || depth >= kPathMaxDepth)
        {
            out.emplace_back(x3, y3);
            return;
        }
        const double x01 = (x0 + x1) / 2, y01 = (y0 + y1) / 2;
        const double x12 = (x1 + x2) / 2, y12 = (y1 + y2) / 2;
        const double x23 = (x2 + x3) / 2, y23 = (y2 + y3) / 2;
        const double xa = (x01 + x12) / 2, ya = (y01 + y12) / 2;
        const double xb = (x12 + x23) / 2, yb = (y12 + y23) / 2;
        const double xm = (xa + xb) / 2, ym = (ya + yb) / 2;
        svg_flatten_cubic(out, x0, y0, x01, y01, xa, ya, xm, ym, depth + 1);
        svg_flatten_cubic(out, xm, ym, xb, yb, x23, y23, x3, y3, depth + 1);
    }

    void svg_flatten_quad(std::vector<std::pair<double, double>> &out,
                          const double x0, const double y0,
                          const double x1, const double y1,
                          const double x, const double y)
    {
        // quadratic -> cubic elevation, one flattener
        const double c1x = x0 + 2.0 / 3 * (x1 - x0);
        const double c1y = y0 + 2.0 / 3 * (y1 - y0);
        const double c2x = x + 2.0 / 3 * (x1 - x);
        const double c2y = y + 2.0 / 3 * (y1 - y);
        svg_flatten_cubic(out, x0, y0, c1x, c1y, c2x, c2y, x, y, 0);
    }

    void SvgCanvas::set_transform(const double tx, const double ty,
                                  const double deg, const double sx,
                                  const double sy)
    {
        mark_dirty();
        double a = sx, b = 0.0, c = 0.0, d = sy, e = tx, f = ty;
        if (deg != 0.0)
        {
            double s = 0.0, co = 0.0;
            sincos_deg(deg, s, co);
            // R · [a c; b d], the SVG rotate direction (y-down,
            // positive = visually clockwise)
            const double na = co * a - s * b;
            const double nb = s * a + co * b;
            const double nc = co * c - s * d;
            const double nd = s * c + co * d;
            a = na;
            b = nb;
            c = nc;
            d = nd;
        }
        tf_a_ = a;
        tf_b_ = b;
        tf_c_ = c;
        tf_d_ = d;
        tf_e_ = e;
        tf_f_ = f;
        // an identity affine clears the flag: the draw paths keep their
        // exact pre-transform code (byte-identical renders)
        tf_active_ = !(a == 1.0 && b == 0.0 && c == 0.0 && d == 1.0 &&
                       e == 0.0 && f == 0.0);
    }

    void SvgCanvas::clear_transform()
    {
        mark_dirty();
        tf_active_ = false;
    }

    void SvgCanvas::apply_tf(double &x, double &y) const
    {
        const double nx = tf_a_ * x + tf_c_ * y + tf_e_;
        y = tf_b_ * x + tf_d_ * y + tf_f_;
        x = nx;
    }

    int64_t SvgCanvas::tf_len_q10() const
    {
        if (!tf_active_)
        {
            return kQ10;
        }
        const double det = tf_a_ * tf_d_ - tf_b_ * tf_c_;
        double ad = det < 0 ? -det : det;
        if (ad > 1e9)
        {
            ad = 1e9;  // keep the Q10 product inside int64
        }
        return isqrt64(static_cast<int64_t>(ad * kQ10 * kQ10));
    }

    void SvgCanvas::add_draw_path(const DrawPath &p)
    {
        // flatten at add time (the parse-time-flattening precedent):
        // per-subpath polylines, one Path item each, `close` marks it
        // closed and the next move/line starts a fresh subpath
        std::vector<std::pair<double, double>> pts;
        bool sub_open = false;
        bool sub_closed = false;
        double cx = 0.0, cy = 0.0;
        double sx = 0.0, sy = 0.0;  // subpath start: close returns here
        const auto flush = [&] {
            if (sub_open && pts.size() >= 2)
            {
                Path item;
                item.pts = std::move(pts);
                item.closed = sub_closed;
                item.has_fill = p.has_fill;
                item.fill = p.fill;
                item.has_stroke = p.has_stroke;
                item.color = p.stroke;  // ignored while !has_stroke
                item.width = p.width;
                item.round_caps = p.round_caps;
                paths_.push_back(std::move(item));
            }
            pts.clear();
            sub_open = false;
            sub_closed = false;
        };
        for (const PathCmd &cmd : p.cmds)
        {
            switch (cmd.op)
            {
                case PathCmd::Op::move:
                    flush();
                    cx = cmd.x;
                    cy = cmd.y;
                    sx = cx;
                    sy = cy;
                    pts.emplace_back(cx, cy);
                    sub_open = true;
                    break;
                case PathCmd::Op::line:
                    if (!sub_open)
                    {
                        // an implicit subpath start at the current point
                        sx = cx;
                        sy = cy;
                        pts.emplace_back(cx, cy);
                        sub_open = true;
                    }
                    cx = cmd.x;
                    cy = cmd.y;
                    pts.emplace_back(cx, cy);
                    break;
                case PathCmd::Op::cubic:
                    if (!sub_open)
                    {
                        sx = cx;
                        sy = cy;
                        pts.emplace_back(cx, cy);
                        sub_open = true;
                    }
                    svg_flatten_cubic(pts, cx, cy, cmd.x1, cmd.y1,
                                      cmd.x2, cmd.y2, cmd.x, cmd.y, 0);
                    cx = cmd.x;
                    cy = cmd.y;
                    break;
                case PathCmd::Op::quad:
                    if (!sub_open)
                    {
                        sx = cx;
                        sy = cy;
                        pts.emplace_back(cx, cy);
                        sub_open = true;
                    }
                    svg_flatten_quad(pts, cx, cy, cmd.x1, cmd.y1,
                                     cmd.x, cmd.y);
                    cx = cmd.x;
                    cy = cmd.y;
                    break;
                case PathCmd::Op::close:
                    sub_closed = true;
                    flush();
                    // SVG Z semantics: the current point returns to the
                    // subpath start
                    cx = sx;
                    cy = sy;
                    break;
            }
        }
        flush();
        mark_dirty();
    }

    void SvgCanvas::set_view_box(const int x, const int y, const int w, const int h)
    {
        mark_dirty();
        vb_x_ = x;
        vb_y_ = y;
        vb_w_ = w;
        vb_h_ = h;
        mark_dirty();
        mark_layout_dirty();  // measure() reads the viewBox
    }

    void SvgCanvas::add_line(const Line &l)
    {
        lines_.push_back(l);
        mark_dirty();
    }

    void SvgCanvas::add_path(const Path &p)
    {
        paths_.push_back(p);
        mark_dirty();
    }

    void SvgCanvas::add_shape(const Shape &s)
    {
        shapes_.push_back(s);
        mark_dirty();
    }

    void SvgCanvas::add_text(const Text &t)
    {
        texts_.push_back(t);
        mark_dirty();
    }

    void SvgCanvas::clear_vectors()
    {
        lines_.clear();
        paths_.clear();
        shapes_.clear();
        texts_.clear();
        mark_dirty();
    }

    core::imsize_t SvgCanvas::measure() const
    {
        if (vb_w_ > 0 && vb_h_ > 0)
        {
            return {vb_w_, vb_h_};
        }
        return {64, 64};
    }

    int SvgCanvas::map_x(const int vx) const
    {
        return static_cast<int>(map_fx(vx));
    }

    int SvgCanvas::map_y(const int vy) const
    {
        return static_cast<int>(map_fy(vy));
    }

    double SvgCanvas::map_fx(const double vx) const
    {
        if (vb_w_ <= 0)
        {
            return vx;
        }
        const auto s = get_size();
        return (vx - vb_x_) * s.width / vb_w_;
    }

    double SvgCanvas::map_fy(const double vy) const
    {
        if (vb_h_ <= 0)
        {
            return vy;
        }
        const auto s = get_size();
        return (vy - vb_y_) * s.height / vb_h_;
    }

    // one stroke rasterized directly: per-pixel coverage of the device
    // capsule (stroke band + round caps) through 2x2 supersampling, the
    // coverage scaling the color's own alpha. Parallel AA hairlines
    // double-blend where their fringes overlap (a ladder pattern on
    // translucent strokes) and a scanline quad jogs on shallow angles,
    // so the stroke is sampled as one shape instead. Sub-1.5px strokes
    // stay the single AA hairline. Integer-only math (Q10 fixed point;
    // the widget.cpp Gaussian precedent): no FPU, no libm, and desktop
    // and NDS plot identical pixels.
    void SvgCanvas::draw_line_stroke(core::Graphics &area, const Line &l) const
    {
        const auto s = get_size();
        // viewBox -> device, Q10, rounded half toward +infinity; without
        // a viewBox coordinates are already device pixels
        const auto map_q10 = [&](const int vx, const int w, const int vb_x,
                                 const int vb_w) -> int64_t {
            if (vb_w <= 0)
            {
                return clamp_q10(static_cast<int64_t>(vx) * kQ10);
            }
            return clamp_q10(((static_cast<int64_t>(vx) - vb_x) * w * kQ10 +
                              vb_w / 2) /
                             vb_w);
        };
        int64_t ax, ay, bx, by;
        if (!tf_active_)
        {
            ax = map_q10(l.x1, s.width, vb_x_, vb_w_);
            ay = map_q10(l.y1, s.height, vb_y_, vb_h_);
            bx = map_q10(l.x2, s.width, vb_x_, vb_w_);
            by = map_q10(l.y2, s.height, vb_y_, vb_h_);
        }
        else
        {
            // the transform mixes the axes: map through doubles, apply
            // the affine, then round into Q10
            const double dsx = vb_w_ > 0
                                   ? static_cast<double>(s.width) / vb_w_
                                   : 1.0;
            const double dsy = vb_h_ > 0
                                   ? static_cast<double>(s.height) / vb_h_
                                   : 1.0;
            double axd = (l.x1 - vb_x_) * dsx;
            double ayd = (l.y1 - vb_y_) * dsy;
            apply_tf(axd, ayd);
            double bxd = (l.x2 - vb_x_) * dsx;
            double byd = (l.y2 - vb_y_) * dsy;
            apply_tf(bxd, byd);
            ax = clamp_q10(static_cast<int64_t>(
                axd * kQ10 + (axd >= 0 ? 0.5 : -0.5)));
            ay = clamp_q10(static_cast<int64_t>(
                ayd * kQ10 + (ayd >= 0 ? 0.5 : -0.5)));
            bx = clamp_q10(static_cast<int64_t>(
                bxd * kQ10 + (bxd >= 0 ? 0.5 : -0.5)));
            by = clamp_q10(static_cast<int64_t>(
                byd * kQ10 + (byd >= 0 ? 0.5 : -0.5)));
        }

        // device stroke width = viewBox width * the geometric mean of
        // the two axis scales: sqrt(W*H / (vb_w*vb_h)) in Q10, clamped
        // to the coordinate range (a pathological ratio must not
        // overflow the squared radius below). The width is rounded into
        // Q10, not truncated first: a fractional stroke-width must
        // survive to the hairline threshold (html-path.md)
        int64_t dev_w = static_cast<int64_t>(
            (l.width > 0.0 ? l.width : 1.0) * kQ10 + 0.5);
        if (vb_w_ > 0 && vb_h_ > 0)
        {
            const int64_t ratio = static_cast<int64_t>(s.width) * s.height *
                                      (kQ10 * kQ10) /
                                  (static_cast<int64_t>(vb_w_) * vb_h_);
            dev_w = dev_w * isqrt64(ratio) / kQ10;
        }
        const int64_t lenq = tf_len_q10();
        if (lenq != kQ10)
        {
            dev_w = dev_w * lenq / kQ10;
        }
        if (dev_w > kCoordMax * kQ10)
        {
            dev_w = kCoordMax * kQ10;
        }
        if (dev_w < 3 * kQ10 / 2)
        {
            const auto round10 = [](const int64_t v) {
                return static_cast<int>((v + kQ10 / 2) / kQ10);
            };
            area.draw_line_aa(round10(ax), round10(ay), round10(bx),
                              round10(by), l.color);
            return;
        }

        const int64_t dx = bx - ax, dy = by - ay;
        const int64_t seg2 = dx * dx + dy * dy;
        const int64_t r2 = (dev_w / 2) * (dev_w / 2);
        // a collapsed segment has no band: round caps still draw the
        // end disc, butt caps draw nothing (per SVG)
        if (seg2 == 0)
        {
            if (l.round_caps)
            {
                area.fill_circle_aa(static_cast<int>((ax + kQ10 / 2) / kQ10),
                                    static_cast<int>((ay + kQ10 / 2) / kQ10),
                                    static_cast<int>(dev_w / 2 / kQ10),
                                    l.color);
            }
            return;
        }
        const int64_t x0 = (ax < bx ? ax : bx) - dev_w;
        const int64_t x1 = (ax < bx ? bx : ax) + dev_w;
        const int64_t y0 = (ay < by ? ay : by) - dev_w;
        const int64_t y1 = (ay < by ? by : ay) + dev_w;
        const bool blend = l.color.a() < 255;
        const bool bak = area.is_alpha_enabled();
        if (blend)
        {
            area.enable_alpha(true);
        }
        core::Color c = l.color;
        for (int64_t py = (y0 / kQ10) - 1; py <= (y1 / kQ10) + 1; ++py)
        {
            for (int64_t px = (x0 / kQ10) - 1; px <= (x1 / kQ10) + 1; ++px)
            {
                int in = 0;
                for (const int64_t off_y : {kQ10 / 4, 3 * kQ10 / 4})
                {
                    for (const int64_t off_x : {kQ10 / 4, 3 * kQ10 / 4})
                    {
                        const int64_t qx = px * kQ10 + off_x - ax;
                        const int64_t qy = py * kQ10 + off_y - ay;
                        const int64_t dot = qx * dx + qy * dy;
                        if (!l.round_caps && (dot < 0 || dot > seg2))
                        {
                            // butt cap: the projection stays inside
                            continue;
                        }
                        // clamped projection in Q10 (t = dot/seg2), then
                        // the squared distance to it against r2
                        int64_t t = (dot * kQ10) / seg2;
                        if (t < 0)
                        {
                            t = 0;
                        }
                        else if (t > kQ10)
                        {
                            t = kQ10;
                        }
                        int64_t ex, ey;
                        if (dy == 0 && t > 0 && t < kQ10)
                        {
                            // axis-parallel interior: the foot of the
                            // perpendicular is the sample itself. The t
                            // path below rounds the projection into Q10
                            // and puts up to len/1024 px of noise on
                            // ex/ey, which flips the exact-boundary
                            // samples of a fractional stroke-width out
                            // of the closed band (<= r2)
                            ex = 0;
                            ey = qy;
                        }
                        else if (dx == 0 && t > 0 && t < kQ10)
                        {
                            ex = qx;
                            ey = 0;
                        }
                        else
                        {
                            ex = qx - ((t * dx) / kQ10);
                            ey = qy - ((t * dy) / kQ10);
                        }
                        if (ex * ex + ey * ey <= r2)
                        {
                            ++in;
                        }
                    }
                }
                if (in == 0)
                {
                    continue;
                }
                if (in == 4)
                {
                    area.draw_pixel(static_cast<int>(px), static_cast<int>(py),
                                    c);
                    continue;
                }
                if constexpr (core::Color::per_channel_blend)
                {
                    c.set_a(static_cast<uint8_t>(c.a() * in / 4));
                    area.draw_pixel(static_cast<int>(px), static_cast<int>(py),
                                    c);
                    c.set_a(l.color.a());
                }
                else
                {
                    // binary alpha (16bpp): coverage quantizes to
                    // plot/skip at half, the plot_aa rule — c.a() reads
                    // back the single bit, so scaling it would clear
                    // every fringe pixel; the full color plots instead
                    if (in * 0xFF / 4 >= 128)
                    {
                        area.draw_pixel(static_cast<int>(px),
                                        static_cast<int>(py), c);
                    }
                }
            }
        }
        if (blend)
        {
            area.enable_alpha(bak);
        }
    }

    // one flattened polyline through whole-polyline capsule coverage:
    // per-pixel 2x2 supersampling, but the per-sample distance is the
    // minimum over every segment of the stroke (and its end points for
    // round caps), so overlapping capsules never double-blend — a
    // translucent glow stroke stays seamless through its joints, the
    // property per-segment stroking cannot give. Integer-only per
    // pixel (Q10 fixed point, the draw_line_stroke rules); the points
    // map once through doubles (parse-time fractions) into the Q10
    // device space. The scratch buffers follow the widget.cpp Gaussian
    // static-scratch precedent: grown as needed, reused across draws.
    void SvgCanvas::draw_path_stroke(core::Graphics &area, const Path &p) const
    {
        stroke_points(area, p.pts.data(), p.pts.size(), p.closed,
                      p.round_caps, p.color, p.width);
    }

    void SvgCanvas::stroke_points(
        core::Graphics &area, const std::pair<double, double> *pts,
        const size_t n, const bool closed, const bool round_caps,
        const core::Color &color, const double width) const
    {
        const auto s = get_size();
        const bool caps = round_caps && !closed;
        if (n == 0 || (n == 1 && !caps))
        {
            return;  // a bare M draws a dot only under round caps
        }

        // viewBox -> device, Q10, half away from zero (the parse-time
        // points carry fractions the Line integer mapper never sees)
        const double sx = vb_w_ > 0 ? static_cast<double>(s.width) / vb_w_ : 1.0;
        const double sy = vb_h_ > 0 ? static_cast<double>(s.height) / vb_h_ : 1.0;
        static std::vector<int64_t> qx, qy, sminx, smaxx, sminy, smaxy;
        qx.resize(n);
        qy.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            double dx = (pts[i].first - vb_x_) * sx;
            double dy = (pts[i].second - vb_y_) * sy;
            if (tf_active_)
            {
                apply_tf(dx, dy);
            }
            qx[i] = clamp_q10(static_cast<int64_t>(
                dx * kQ10 + (dx >= 0 ? 0.5 : -0.5)));
            qy[i] = clamp_q10(static_cast<int64_t>(
                dy * kQ10 + (dy >= 0 ? 0.5 : -0.5)));
        }

        // device stroke width = viewBox width * the geometric mean of
        // the two axis scales (the draw_line_stroke formula); rounded
        // into Q10 so a fractional stroke-width survives
        int64_t dev_w = static_cast<int64_t>(
            (width > 0.0 ? width : 1.0) * kQ10 + 0.5);
        if (vb_w_ > 0 && vb_h_ > 0)
        {
            const int64_t ratio = static_cast<int64_t>(s.width) * s.height *
                                      (kQ10 * kQ10) /
                                  (static_cast<int64_t>(vb_w_) * vb_h_);
            dev_w = dev_w * isqrt64(ratio) / kQ10;
        }
        // the user affine scales the band by sqrt(|det|); identity
        // skips (byte-identical renders)
        const int64_t lenq = tf_len_q10();
        if (lenq != kQ10)
        {
            dev_w = dev_w * lenq / kQ10;
        }
        if (dev_w > kCoordMax * kQ10)
        {
            dev_w = kCoordMax * kQ10;
        }

        // segment list: the polyline plus the closing segment for Z
        size_t segs = n > 1 ? n - 1 : 0;
        if (closed && n > 2)
        {
            ++segs;
        }

        // sub-1.5px strokes stay AA hairlines, the draw_line_stroke
        // policy (the supersampled band of a half-pixel radius would
        // quantize to a faint spine); joints may double-blend a
        // translucent hairline, the accepted line fallback trade-off
        if (dev_w < 3 * kQ10 / 2)
        {
            const auto round10 = [](const int64_t v) {
                return static_cast<int>((v + kQ10 / 2) / kQ10);
            };
            for (size_t i = 0; i < segs; ++i)
            {
                const size_t j = (i + 1) % n;
                area.draw_line_aa(round10(qx[i]), round10(qy[i]),
                                  round10(qx[j]), round10(qy[j]), color);
            }
            if (caps)
            {
                area.fill_circle_aa(round10(qx[0]), round10(qy[0]), 1,
                                    color);
                area.fill_circle_aa(round10(qx[n - 1]), round10(qy[n - 1]),
                                    1, color);
            }
            return;
        }

        const int64_t r = dev_w / 2;
        const int64_t r2 = r * r;

        // per-segment AABBs (inflated by the radius) gate the inner
        // distance test
        sminx.resize(segs);
        smaxx.resize(segs);
        sminy.resize(segs);
        smaxy.resize(segs);
        for (size_t i = 0; i < segs; ++i)
        {
            const size_t j = (i + 1) % n;
            const int64_t ax = qx[i], ay = qy[i];
            const int64_t bx = qx[j], by = qy[j];
            sminx[i] = std::min(ax, bx) - r;
            smaxx[i] = std::max(ax, bx) + r;
            sminy[i] = std::min(ay, by) - r;
            smaxy[i] = std::max(ay, by) + r;
        }

        int64_t minx = qx[0], maxx = qx[0], miny = qy[0], maxy = qy[0];
        for (size_t i = 1; i < n; ++i)
        {
            minx = std::min(minx, qx[i]);
            maxx = std::max(maxx, qx[i]);
            miny = std::min(miny, qy[i]);
            maxy = std::max(maxy, qy[i]);
        }
        const int64_t x0 = minx - dev_w;
        const int64_t x1 = maxx + dev_w;
        const int64_t y0 = miny - dev_w;
        const int64_t y1 = maxy + dev_w;

        const bool blend = color.a() < 255;
        const bool bak = area.is_alpha_enabled();
        if (blend)
        {
            area.enable_alpha(true);
        }
        core::Color c = color;
        for (int64_t py = (y0 / kQ10) - 1; py <= (y1 / kQ10) + 1; ++py)
        {
            for (int64_t px = (x0 / kQ10) - 1; px <= (x1 / kQ10) + 1; ++px)
            {
                int in = 0;
                for (const int64_t off_y : {kQ10 / 4, 3 * kQ10 / 4})
                {
                    for (const int64_t off_x : {kQ10 / 4, 3 * kQ10 / 4})
                    {
                        const int64_t sxp = px * kQ10 + off_x;
                        const int64_t syp = py * kQ10 + off_y;
                        // min squared distance over the segments. The
                        // clamped projection rounds every joint (the
                        // shared endpoint disc — round joins, and no
                        // double-blend under translucency); the two
                        // exposed ends of an open butt-capped path
                        // reject their beyond-the-end samples instead
                        // (SVG: butt cap, no end disc)
                        int64_t best = r2 + 1;
                        for (size_t i = 0; i < segs; ++i)
                        {
                            if (sxp < sminx[i] || sxp > smaxx[i] ||
                                syp < sminy[i] || syp > smaxy[i])
                            {
                                continue;
                            }
                            const size_t j = (i + 1) % n;
                            const int64_t dx = qx[j] - qx[i];
                            const int64_t dy = qy[j] - qy[i];
                            const int64_t seg2 = dx * dx + dy * dy;
                            if (seg2 == 0)
                            {
                                continue;  // rounded away: no band
                            }
                            const int64_t dot =
                                (sxp - qx[i]) * dx + (syp - qy[i]) * dy;
                            if (!round_caps && !closed &&
                                ((i == 0 && dot < 0) ||
                                 (i == segs - 1 && dot > seg2)))
                            {
                                continue;
                            }
                            int64_t t = (dot * kQ10) / seg2;
                            if (t < 0)
                            {
                                t = 0;
                            }
                            else if (t > kQ10)
                            {
                                t = kQ10;
                            }
                            int64_t ex, ey;
                            if (dy == 0 && t > 0 && t < kQ10)
                            {
                                // axis-parallel interior, the
                                // draw_line_stroke exactness rule: the
                                // t path's Q10 projection noise must not
                                // push a closed-band boundary sample out
                                ex = 0;
                                ey = syp - qy[i];
                            }
                            else if (dx == 0 && t > 0 && t < kQ10)
                            {
                                ex = sxp - qx[i];
                                ey = 0;
                            }
                            else
                            {
                                ex = sxp - qx[i] - ((t * dx) / kQ10);
                                ey = syp - qy[i] - ((t * dy) / kQ10);
                            }
                            const int64_t d2 = ex * ex + ey * ey;
                            if (d2 < best)
                            {
                                best = d2;
                            }
                        }
                        if (n == 1)
                        {
                            // the bare-M dot (round caps only; butt
                            // draws nothing and returned above)
                            const int64_t ex = sxp - qx[0];
                            const int64_t ey = syp - qy[0];
                            if (ex * ex + ey * ey < best)
                            {
                                best = ex * ex + ey * ey;
                            }
                        }
                        if (best <= r2)
                        {
                            ++in;
                        }
                    }
                }
                if (in == 0)
                {
                    continue;
                }
                if (in == 4)
                {
                    area.draw_pixel(static_cast<int>(px), static_cast<int>(py),
                                    c);
                    continue;
                }
                if constexpr (core::Color::per_channel_blend)
                {
                    c.set_a(static_cast<uint8_t>(c.a() * in / 4));
                    area.draw_pixel(static_cast<int>(px), static_cast<int>(py),
                                    c);
                    c.set_a(color.a());
                }
                else
                {
                    // binary alpha (16bpp): the plot_aa half-coverage
                    // rule, the draw_line_stroke policy
                    if (in * 0xFF / 4 >= 128)
                    {
                        area.draw_pixel(static_cast<int>(px),
                                        static_cast<int>(py), c);
                    }
                }
            }
        }
        if (blend)
        {
            area.enable_alpha(bak);
        }
    }

    // the static-shape fill: per-pixel 2x2 supersampled coverage (the
    // stroke loop policy). The inside test per kind — rect = box
    // comparisons; ellipse = the implicit equation through the
    // axis-stretched center/radii, exact because the viewBox stretch is
    // affine (an affine image of an ellipse is an ellipse). Integer Q10
    // per pixel; the ellipse ratio form (dx/rxd)^2 + (dy/ryd)^2 <= 1
    // keeps every intermediate inside int64 (dx/rxd <= the bbox slack,
    // and a sample with |dx/rxd| > 1 is already outside).
    void SvgCanvas::draw_shape_fill(core::Graphics &area,
                                    const Shape &s) const
    {
        if (!s.has_fill || s.rx <= 0.0 || s.ry <= 0.0)
        {
            return;
        }
        const auto sz = get_size();
        const double sx = vb_w_ > 0 ? static_cast<double>(sz.width) / vb_w_
                                    : 1.0;
        const double sy = vb_h_ > 0 ? static_cast<double>(sz.height) / vb_h_
                                    : 1.0;
        const auto map_q10 = [&](const double v, const double scale) {
            const double d = v * scale;
            return clamp_q10(static_cast<int64_t>(
                d * kQ10 + (d >= 0 ? 0.5 : -0.5)));
        };
        if (tf_active_ && (tf_b_ != 0.0 || tf_c_ != 0.0))
        {
            // rotation: the axis-aligned implicit tests are invalid —
            // fill the flattened outline through the even-odd path
            // (static scratch: zero allocation on the paint path, §8)
            static std::vector<std::pair<double, double>> outline;
            build_outline(s, outline);
            static std::vector<int64_t> fx, fy;
            fx.resize(outline.size());
            fy.resize(outline.size());
            for (size_t i = 0; i < outline.size(); ++i)
            {
                double dx = (outline[i].first - vb_x_) * sx;
                double dy = (outline[i].second - vb_y_) * sy;
                apply_tf(dx, dy);
                fx[i] = clamp_q10(static_cast<int64_t>(
                    dx * kQ10 + (dx >= 0 ? 0.5 : -0.5)));
                fy[i] = clamp_q10(static_cast<int64_t>(
                    dy * kQ10 + (dy >= 0 ? 0.5 : -0.5)));
            }
            draw_evenodd_fill(area, fx, fy, s.fill);
            return;
        }
        int64_t cx = map_q10((s.cx - vb_x_), sx);
        int64_t cy = map_q10((s.cy - vb_y_), sy);
        int64_t rxd = map_q10(s.rx, sx);
        int64_t ryd = map_q10(s.ry, sy);
        if (tf_active_)
        {
            // translate/scale only: the mapped figure stays an
            // axis-aligned ellipse/box — scale the center and the
            // half-extents (mirroring folds into the magnitude).
            // cx/cy are Q10 device px, the affine translate is in px:
            // push it into Q10 here
            const double ncx = tf_a_ * cx + tf_c_ * cy + tf_e_ * kQ10;
            const double ncy = tf_b_ * cx + tf_d_ * cy + tf_f_ * kQ10;
            cx = clamp_q10(static_cast<int64_t>(
                ncx + (ncx >= 0 ? 0.5 : -0.5)));
            cy = clamp_q10(static_cast<int64_t>(
                ncy + (ncy >= 0 ? 0.5 : -0.5)));
            const double ax = tf_a_ < 0 ? -tf_a_ : tf_a_;
            const double ad = tf_d_ < 0 ? -tf_d_ : tf_d_;
            rxd = static_cast<int64_t>(ax * rxd);
            ryd = static_cast<int64_t>(ad * ryd);
        }
        if (rxd == 0 || ryd == 0)
        {
            return;  // sub-rounding degenerate: nothing to fill
        }

        const int64_t x0 = cx - rxd - kQ10;
        const int64_t x1 = cx + rxd + kQ10;
        const int64_t y0 = cy - ryd - kQ10;
        const int64_t y1 = cy + ryd + kQ10;
        const bool blend = s.fill.a() < 255;
        const bool bak = area.is_alpha_enabled();
        if (blend)
        {
            area.enable_alpha(true);
        }
        for (int64_t py = (y0 / kQ10) - 1; py <= (y1 / kQ10) + 1; ++py)
        {
            for (int64_t px = (x0 / kQ10) - 1; px <= (x1 / kQ10) + 1; ++px)
            {
                int in = 0;
                for (const int64_t off_y : {kQ10 / 4, 3 * kQ10 / 4})
                {
                    for (const int64_t off_x : {kQ10 / 4, 3 * kQ10 / 4})
                    {
                        const int64_t adx = px * kQ10 + off_x - cx;
                        const int64_t ady = py * kQ10 + off_y - cy;
                        bool inside = false;
                        if (s.kind == Shape::Kind::rect)
                        {
                            inside = adx <= rxd && adx >= -rxd &&
                                     ady <= ryd && ady >= -ryd;
                        }
                        else
                        {
                            const int64_t a = (adx < 0 ? -adx : adx) * kQ10 /
                                              rxd;
                            if (a <= kQ10)
                            {
                                const int64_t b = (ady < 0 ? -ady : ady) *
                                                  kQ10 / ryd;
                                inside = b <= kQ10 &&
                                         a * a + b * b <= kQ10 * kQ10;
                            }
                        }
                        if (inside)
                        {
                            ++in;
                        }
                    }
                }
                if (in != 0)
                {
                    plot_covered(area, px, py, s.fill, in);
                }
            }
        }
        if (blend)
        {
            area.enable_alpha(bak);
        }
    }

    // even-odd fill over device-space Q10 points, the closing segment
    // implied (SVG fills the implicitly closed region of a polyline
    // too). Per sample: the crossing count of the horizontal ray
    // against every edge, the classic half-open rule (a vertex on the
    // ray belongs to the lower edge). Q10 products stay inside int64
    // (both factors are clamped coordinates).
    void SvgCanvas::draw_evenodd_fill(core::Graphics &area,
                                      const std::vector<int64_t> &qx,
                                      const std::vector<int64_t> &qy,
                                      const core::Color &color) const
    {
        const size_t n = qx.size();
        if (n < 3)
        {
            return;  // a point or segment has no interior
        }
        int64_t minx = qx[0], maxx = qx[0], miny = qy[0], maxy = qy[0];
        for (size_t i = 1; i < n; ++i)
        {
            minx = std::min(minx, qx[i]);
            maxx = std::max(maxx, qx[i]);
            miny = std::min(miny, qy[i]);
            maxy = std::max(maxy, qy[i]);
        }
        const bool blend = color.a() < 255;
        const bool bak = area.is_alpha_enabled();
        if (blend)
        {
            area.enable_alpha(true);
        }
        for (int64_t py = (miny / kQ10) - 1; py <= (maxy / kQ10) + 1; ++py)
        {
            for (int64_t px = (minx / kQ10) - 1; px <= (maxx / kQ10) + 1;
                 ++px)
            {
                int in = 0;
                for (const int64_t off_y : {kQ10 / 4, 3 * kQ10 / 4})
                {
                    for (const int64_t off_x : {kQ10 / 4, 3 * kQ10 / 4})
                    {
                        const int64_t sxp = px * kQ10 + off_x;
                        const int64_t syp = py * kQ10 + off_y;
                        bool inside = false;
                        for (size_t i = 0; i < n; ++i)
                        {
                            const size_t j = (i + 1) % n;
                            const int64_t y1 = qy[i], y2 = qy[j];
                            if ((y1 > syp) == (y2 > syp))
                            {
                                continue;  // no crossing at this height
                            }
                            const int64_t x1 = qx[i], x2 = qx[j];
                            const int64_t xi =
                                x1 + (syp - y1) * (x2 - x1) / (y2 - y1);
                            if (sxp < xi)
                            {
                                inside = !inside;
                            }
                        }
                        if (inside)
                        {
                            ++in;
                        }
                    }
                }
                if (in != 0)
                {
                    plot_covered(area, px, py, color, in);
                }
            }
        }
        if (blend)
        {
            area.enable_alpha(bak);
        }
    }

    // the static-shape outline closed into a polyline riding the path
    // stroke machinery: rect = its four corners; circle/ellipse = a
    // draw-time N-gon flattening — the segment count derives from the
    // device-space perimeter (pi*(a+b), chord ~2px, clamped 16..256)
    // and the unit-circle vertices come from the exact rotation
    // recurrence, its step from a libm-free small-angle series (the
    // embedded link has no libm trig). Plain IEEE double, so every
    // platform flattens alike; the scratch buffer follows the
    // draw_path_stroke static-scratch precedent (allocation-free per
    // draw).
    // the shape outline in viewBox units: rect = its four corners;
    // circle/ellipse = an N-gon whose segment count derives from the
    // device-space perimeter (pi*(a+b), chord ~2px, clamped 16..256,
    // scaled by sqrt(|det|) of the user affine) generated through the
    // exact rotation recurrence. The step comes from a libm-free
    // small-angle series (the embedded link has no libm trig) and is
    // plain IEEE double, so every platform flattens alike; the scratch
    // buffer follows the draw_path_stroke static-scratch precedent
    // (allocation-free per draw).
    void SvgCanvas::build_outline(const Shape &s,
                                  std::vector<std::pair<double, double>> &out)
        const
    {
        out.clear();
        if (s.kind == Shape::Kind::rect)
        {
            out.emplace_back(s.cx - s.rx, s.cy - s.ry);
            out.emplace_back(s.cx + s.rx, s.cy - s.ry);
            out.emplace_back(s.cx + s.rx, s.cy + s.ry);
            out.emplace_back(s.cx - s.rx, s.cy + s.ry);
            return;
        }
        const auto sz = get_size();
        const double sx = vb_w_ > 0
                              ? static_cast<double>(sz.width) / vb_w_
                              : 1.0;
        const double sy = vb_h_ > 0
                              ? static_cast<double>(sz.height) / vb_h_
                              : 1.0;
        constexpr double kPi = 3.14159265358979323846;
        double perim = kPi * (s.rx * sx + s.ry * sy);  // exact for a
        const double len =                             // circle, ~9% shy
            static_cast<double>(tf_len_q10()) / kQ10;  // at the extreme
        perim *= len;
        int segs = static_cast<int>(perim / 2.0);
        if (segs < 16)
        {
            segs = 16;
        }
        else if (segs > 256)
        {
            segs = 256;
        }
        const double dt = 2.0 * kPi / static_cast<double>(segs);
        // rotation step, libm-free: a small-angle series (dt <= 2*pi/16,
        // truncation ~1e-7) keeps USE_INTEGER_GEOMETRY targets off the
        // libm trig link while staying plain IEEE double — every
        // platform computes the same vertices
        const double dt2 = dt * dt;
        const double sd =
            dt * (1.0 - dt2 / 6.0 * (1.0 - dt2 / 20.0 * (1.0 - dt2 / 42.0)));
        const double cd = 1.0 - dt2 / 2.0 * (1.0 - dt2 / 12.0 * (1.0 - dt2 / 30.0));
        double ux = 1.0, uy = 0.0;
        for (int i = 0; i < segs; ++i)
        {
            out.emplace_back(s.cx + s.rx * ux, s.cy + s.ry * uy);
            const double nx = ux * cd - uy * sd;
            uy = ux * sd + uy * cd;
            ux = nx;
        }
    }

    void SvgCanvas::draw_shape_stroke(core::Graphics &area,
                                      const Shape &s) const
    {
        if (!s.has_stroke || s.rx <= 0.0 || s.ry <= 0.0)
        {
            return;
        }
        static std::vector<std::pair<double, double>> scratch;
        build_outline(s, scratch);
        stroke_points(area, scratch.data(), scratch.size(), true, false,
                      s.stroke, s.stroke_width);
    }

    void SvgCanvas::draw_at(core::Graphics &area) const
    {
        // the documented draw order: static shapes, lines, paths,
        // texts; within one item fill precedes stroke (SVG paint order)
        for (const Shape &sh : shapes_)
        {
            if (sh.has_fill)
            {
                draw_shape_fill(area, sh);
            }
            if (sh.has_stroke)
            {
                draw_shape_stroke(area, sh);
            }
        }
        for (const Line &l : lines_)
        {
            draw_line_stroke(area, l);
        }
        for (const Path &p : paths_)
        {
            if (p.has_fill)
            {
                // even-odd fill over the mapped points, the closing
                // segment implied (SVG fills the implicitly closed
                // region of an open polyline too); same mapping as
                // stroke_points, own scratch buffers
                const auto s = get_size();
                const double sx = vb_w_ > 0
                                      ? static_cast<double>(s.width) / vb_w_
                                      : 1.0;
                const double sy = vb_h_ > 0
                                      ? static_cast<double>(s.height) / vb_h_
                                      : 1.0;
                static std::vector<int64_t> fx, fy;
                fx.resize(p.pts.size());
                fy.resize(p.pts.size());
                for (size_t i = 0; i < p.pts.size(); ++i)
                {
                    double dx = (p.pts[i].first - vb_x_) * sx;
                    double dy = (p.pts[i].second - vb_y_) * sy;
                    if (tf_active_)
                    {
                        apply_tf(dx, dy);
                    }
                    fx[i] = clamp_q10(static_cast<int64_t>(
                        dx * kQ10 + (dx >= 0 ? 0.5 : -0.5)));
                    fy[i] = clamp_q10(static_cast<int64_t>(
                        dy * kQ10 + (dy >= 0 ? 0.5 : -0.5)));
                }
                draw_evenodd_fill(area, fx, fy, p.fill);
            }
            if (p.has_stroke)
            {
                draw_path_stroke(area, p);
            }
        }
        for (const Text &t : texts_)
        {
            if (t.text.empty())
            {
                continue;
            }
            const int len = static_cast<int>(t.text.size());
            // a declared font-size scales with the viewBox (browser: the
            // font-size rides the CTM); it resolves against the process
            // font family like every widget seam, the bitmap fallback
            // staying the documented degradation when none is installed
            const auto sz = get_size();
            double sy = vb_h_ > 0
                            ? static_cast<double>(sz.height) / vb_h_
                            : 1.0;
            // the canvas transform scales the upright glyphs by
            // sqrt(|det|) (code-contract §3.2: no rotated glyphs)
            const double tlen = static_cast<double>(tf_len_q10()) / kQ10;
            sy *= tlen;
            const int px =
                t.font_size > 0.0
                    ? static_cast<int>(t.font_size * sy + 0.5)
                    : 0;
            const GlyphProvider *provider = nullptr;
#if defined(IMCORE_HAS_TTF_RUNTIME)
            zb::SharedPtr<GlyphProvider> held;
            if (px >= 1 && px <= 128 && has_font_family())
            {
                held = font_family().provider_for(px);
                provider = held.get();
            }
#endif
            // the baseline point maps through the full affine (the
            // glyphs stay upright, the documented deviation)
            double tx = map_fx(t.x);
            double ty = map_fy(t.y);
            if (tf_active_)
            {
                apply_tf(tx, ty);
            }
            int pen = static_cast<int>(tx);
            int baseline = static_cast<int>(ty);
            if (provider != nullptr)
            {
                // §2.4 applies at the svg seam too: the item-level
                // provider is the run's main provider per code unit,
                // uncovered units fall to the 5x7 bitmap provider, still
                // missing units are skipped. The raw measure/write fast
                // path dropped code units the TTF lacks (glyph index 0
                // measures 0 and rasterizes empty) and mis-measured
                // mixed-script runs; the widget seam behaves the same
                // for the identical string.
                bool all_covered = true;
                for (int i = 0; i < len; ++i)
                {
                    if (!provider->covers(t.text[i]))
                    {
                        all_covered = false;
                        break;
                    }
                }
                if (all_covered)
                {
                    const int adv = provider->measure(t.text.data(), len).width;
                    if (t.anchor == 1)
                    {
                        pen -= adv / 2;
                    }
                    else if (t.anchor == 2)
                    {
                        pen -= adv;
                    }
                    provider->write(area, t.text.data(), len, pen, baseline,
                                    t.has_color ? t.color : theme().text);
                    continue;
                }
                // mixed run: the chain per code unit (contiguous covered
                // spans still batch per unit through the provider)
                const BitmapProvider bitmap_fallback{};
                const auto pick = [&](const char16_t ch) -> const GlyphProvider *
                {
                    if (provider->covers(ch))
                    {
                        return provider;
                    }
                    return bitmap_fallback.covers(ch) ? &bitmap_fallback : nullptr;
                };
                int adv = 0;
                for (int i = 0; i < len; ++i)
                {
                    if (const GlyphProvider *p = pick(t.text[i]))
                    {
                        adv += p->measure(t.text.data() + i, 1).width;
                    }
                }
                if (t.anchor == 1)
                {
                    pen -= adv / 2;
                }
                else if (t.anchor == 2)
                {
                    pen -= adv;
                }
                const core::Color col = t.has_color ? t.color : theme().text;
                for (int i = 0; i < len; ++i)
                {
                    if (const GlyphProvider *p = pick(t.text[i]))
                    {
                        p->write(area, t.text.data() + i, 1, pen, baseline, col);
                        pen += p->measure(t.text.data() + i, 1).width;
                    }
                    // an uncovered unit keeps the pen position (its slot
                    // is empty; the string never reflows mid-run)
                }
                continue;
            }
            const int adv = advance_of(t.text.data(), len);
            if (t.anchor == 1)
            {
                pen -= adv / 2;
            }
            else if (t.anchor == 2)
            {
                pen -= adv;
            }
            // themed items read theme().text live (no widget color is
            // consulted: svg text never inherits, per the subset contract)
            draw_text_at(area, t.text.data(), len,
                         pen, baseline,
                         t.has_color ? t.color : theme().text);
        }
    }
}  // namespace zb::ui
