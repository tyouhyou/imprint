#include "gauge_dial.hpp"
#include "theme.hpp"

namespace zb::ui
{
    void GaugeDial::set_range(const int mn, const int mx)
    {
        mark_dirty();
        min = mn;
        max = mx;
        if (max < min)
        {
            max = min;
        }
        if (value < min)
        {
            value = min;
        }
        if (value > max)
        {
            value = max;
        }
    }

    void GaugeDial::set_value(const int v)
    {
        if (v == value)
        {
            return;
        }
        mark_dirty();
        value = v;
        if (value < min)
        {
            value = min;
        }
        if (value > max)
        {
            value = max;
        }
    }

    void GaugeDial::set_start_deg(const int d)
    {
        if (d == start_deg)
        {
            return;
        }
        mark_dirty();
        start_deg = d;
    }

    void GaugeDial::set_sweep_deg(const int d)
    {
        if (d == sweep_deg)
        {
            return;
        }
        mark_dirty();
        sweep_deg = d;
    }

    int GaugeDial::value_deg() const
    {
        const int den = max - min;
        if (den <= 0)
        {
            return start_deg;
        }
        const int num = value - min;
        return start_deg + num * sweep_deg / den;
    }

    bool GaugeDial::hit(const int x, const int y) const
    {
        if (!is_visible())
        {
            return false;
        }
        const auto s = get_size();
        const int w = s.width < s.height ? s.width : s.height;
        const int r = w / 2 - 1;
        if (r < 0)
        {
            return false;
        }
        const int dx = x - s.width / 2;
        const int dy = y - s.height / 2;
        return dx * dx + dy * dy <= r * r;
    }

    void GaugeDial::draw_at(core::Graphics &area) const
    {
        const auto s = get_size();
        const int w = s.width;
        const int h = s.height;
        const int radius = (w < h ? w : h) / 2;
        if (radius < 4)
        {
            return;
        }
        const int cx = w / 2;
        const int cy = h / 2;

        const core::Color border_c = track_color.value_or(theme().border);
        const core::Color accent   = arc_color.value_or(theme().accent);
        const core::Color needle_c = needle_color.value_or(accent);
        const core::Color tick_c   = tick_color.value_or(border_c);
        const core::Color face_c   = face_color.value_or(theme().background);

        // face disc
        area.fill_circle(cx, cy, radius - 1, face_c);
        area.draw_circle(cx, cy, radius - 1, border_c);

        const int arc_r = radius - 10; // ticks live between arc and rim

        // track arc (full sweep, background colour)
        area.draw_arc_aa(cx, cy, arc_r, start_deg, sweep_deg, border_c);

        // value arc (from start to current)
        const int vd = value_deg();
        if (vd != start_deg)
        {
            area.draw_arc_aa(cx, cy, arc_r, start_deg, vd - start_deg, accent);
        }

        // radial ticks every 30° over the sweep
        for (int d = 0; d <= sweep_deg; d += 30)
        {
            const int deg = start_deg + d;
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            core::point_on_circle(cx, cy, radius - 3, deg, &x1, &y1);
            core::point_on_circle(cx, cy, radius - 9, deg, &x2, &y2);
            area.draw_line(x1, y1, x2, y2, tick_c);
        }

        // needle
        int nx = 0, ny = 0;
        core::point_on_circle(cx, cy, radius - 6, vd, &nx, &ny);
        area.draw_line(cx, cy, nx, ny, needle_c);

        // hub
        area.fill_circle(cx, cy, 2, needle_c);
    }
}
