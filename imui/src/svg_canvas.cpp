#include "svg_canvas.hpp"

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

    void SvgCanvas::add_text(const Text &t)
    {
        texts_.push_back(t);
        mark_dirty();
    }

    void SvgCanvas::clear_vectors()
    {
        lines_.clear();
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
                c.set_a(static_cast<uint8_t>(c.a() * in / 4));
                area.draw_pixel(static_cast<int>(px), static_cast<int>(py), c);
                c.set_a(l.color.a());
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
