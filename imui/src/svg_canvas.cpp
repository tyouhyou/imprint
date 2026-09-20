#include "svg_canvas.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

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
    }  // namespace

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

    void SvgCanvas::add_text(const Text &t)
    {
        texts_.push_back(t);
        mark_dirty();
    }

    void SvgCanvas::clear_vectors()
    {
        lines_.clear();
        paths_.clear();
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
        const int64_t ax = map_q10(l.x1, s.width, vb_x_, vb_w_);
        const int64_t ay = map_q10(l.y1, s.height, vb_y_, vb_h_);
        const int64_t bx = map_q10(l.x2, s.width, vb_x_, vb_w_);
        const int64_t by = map_q10(l.y2, s.height, vb_y_, vb_h_);

        // device stroke width = viewBox width * the geometric mean of
        // the two axis scales: sqrt(W*H / (vb_w*vb_h)) in Q10, clamped
        // to the coordinate range (a pathological ratio must not
        // overflow the squared radius below)
        int64_t dev_w = static_cast<int64_t>(l.width > 0.0 ? l.width : 1.0) *
                        kQ10;
        if (vb_w_ > 0 && vb_h_ > 0)
        {
            const int64_t ratio = static_cast<int64_t>(s.width) * s.height *
                                      (kQ10 * kQ10) /
                                  (static_cast<int64_t>(vb_w_) * vb_h_);
            dev_w = dev_w * isqrt64(ratio) / kQ10;
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
                        const int64_t ex = qx - ((t * dx) / kQ10);
                        const int64_t ey = qy - ((t * dy) / kQ10);
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
        const auto s = get_size();
        const size_t n = p.pts.size();
        const bool caps = p.round_caps && !p.closed;
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
            const double dx = (p.pts[i].first - vb_x_) * sx;
            const double dy = (p.pts[i].second - vb_y_) * sy;
            qx[i] = clamp_q10(static_cast<int64_t>(
                dx * kQ10 + (dx >= 0 ? 0.5 : -0.5)));
            qy[i] = clamp_q10(static_cast<int64_t>(
                dy * kQ10 + (dy >= 0 ? 0.5 : -0.5)));
        }

        // device stroke width = viewBox width * the geometric mean of
        // the two axis scales (the draw_line_stroke formula)
        int64_t dev_w = static_cast<int64_t>(p.width > 0.0 ? p.width : 1.0) *
                        kQ10;
        if (vb_w_ > 0 && vb_h_ > 0)
        {
            const int64_t ratio = static_cast<int64_t>(s.width) * s.height *
                                      (kQ10 * kQ10) /
                                  (static_cast<int64_t>(vb_w_) * vb_h_);
            dev_w = dev_w * isqrt64(ratio) / kQ10;
        }
        if (dev_w > kCoordMax * kQ10)
        {
            dev_w = kCoordMax * kQ10;
        }

        // segment list: the polyline plus the closing segment for Z
        size_t segs = n > 1 ? n - 1 : 0;
        if (p.closed && n > 2)
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
                                  round10(qx[j]), round10(qy[j]), p.color);
            }
            if (caps)
            {
                area.fill_circle_aa(round10(qx[0]), round10(qy[0]), 1,
                                    p.color);
                area.fill_circle_aa(round10(qx[n - 1]), round10(qy[n - 1]),
                                    1, p.color);
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

        const bool blend = p.color.a() < 255;
        const bool bak = area.is_alpha_enabled();
        if (blend)
        {
            area.enable_alpha(true);
        }
        core::Color c = p.color;
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
                            if (!p.round_caps && !p.closed &&
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
                            const int64_t ex =
                                sxp - qx[i] - ((t * dx) / kQ10);
                            const int64_t ey =
                                syp - qy[i] - ((t * dy) / kQ10);
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
                    c.set_a(p.color.a());
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

    void SvgCanvas::draw_at(core::Graphics &area) const
    {
        for (const Line &l : lines_)
        {
            draw_line_stroke(area, l);
        }
        for (const Path &p : paths_)
        {
            draw_path_stroke(area, p);
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
            const double sy = vb_h_ > 0
                                  ? static_cast<double>(sz.height) / vb_h_
                                  : 1.0;
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
            int pen = map_x(t.x);
            int baseline = map_y(t.y);
            if (provider != nullptr)
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
