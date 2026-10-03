#include "slider.hpp"
#include <cstdint>

#include "logging.hpp"

namespace zb::ui
{
    void Slider::set_range(const int mn, const int mx)
    {
        if (mn == min && mx == max)
        {
            return;  // a slider fed its current range must not repaint
        }
        mark_dirty();
        min = mn;
        max = mx;
        if (max < min)
        {
            LW << "slider range reversed; clamping to a point";
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

    void Slider::set_value(const int v)
    {
        int nv = v;
        if (nv < min)
        {
            nv = min;
        }
        if (nv > max)
        {
            nv = max;
        }
        if (nv == value)
        {
            return;  // a slider pinned at its value must not repaint
        }
        value = nv;
        mark_dirty();
    }

    int Slider::value_from_x(const int x) const
    {
        const auto s = get_size();
        const int span = s.width - thumb_w;
        if (span <= 0 || max <= min)
        {
            return min;
        }
        // clamp the pointer to the widget, then map the thumb center
        const int cx = x < 0 ? 0 : (x > s.width - 1 ? s.width - 1 : x);
        // int64 intermediates: a wide range overflows signed int
        int v = min + static_cast<int>(
                           static_cast<int64_t>(cx - thumb_w / 2) * (max - min) /
                           span);
        if (v < min)
        {
            v = min;
        }
        if (v > max)
        {
            v = max;
        }
        return v;
    }

    bool Slider::apply_step(const int dir)
    {
        int v = value + step * dir;
        if (v < min)
        {
            v = min;
        }
        if (v > max)
        {
            v = max;
        }
        if (v == value)
        {
            return false;
        }
        mark_dirty();
        value = v;
        changed(v);
        return true;
    }

    bool Slider::on_input(const zb::input::input_event &ev)
    {
        switch (ev.type)
        {
        case zb::input::input_type::mouse_left_down:
        case zb::input::input_type::touch_down:
        {
            const auto pos = get_absolute_position();
            const int v = value_from_x(ev.x - pos.x);
            if (v != value)
            {
                mark_dirty();
                value = v;
                changed(v);
            }
            return true;  // the press is claimed either way
        }
        case zb::input::input_type::mouse_move:
        case zb::input::input_type::touch_move:
        {
            const auto pos = get_absolute_position();
            const int v = value_from_x(ev.x - pos.x);
            if (v != value)
            {
                mark_dirty();
                value = v;
                changed(v);
                return true;  // a change: repaint gate
            }
            return false;
        }
        case zb::input::input_type::mouse_left_up:
        case zb::input::input_type::touch_up:
            return true;
        case zb::input::input_type::mouse_wheel:
            if (ev.delta == 0)
            {
                return false;  // a normalized zero delta is not a step
            }
            return apply_step(ev.delta > 0 ? 1 : -1);
        case zb::input::input_type::key_down:
            if (ev.key == static_cast<int>(zb::input::key_code::left))
            {
                return apply_step(-1);
            }
            if (ev.key == static_cast<int>(zb::input::key_code::right))
            {
                return apply_step(1);
            }
            return false;
        default:
            return false;
        }
    }

    void Slider::draw_at(core::Graphics &area) const
    {
        const auto s = get_size();
        const int cy = s.height / 2;

        // track
        const int track_y0 = cy - track_h / 2;
        area.fill_rect(0, track_y0, s.width - 1, track_y0 + track_h - 1,
                       track_color.value_or(theme().border));

        // thumb: center travels [thumb_w/2 .. width-1-thumb_w/2]
        const int span = s.width - thumb_w;
        const int vx = (max > min)
                           ? thumb_w / 2 + static_cast<int>(
                                 static_cast<int64_t>(value - min) * span /
                                 (max - min))
                           : thumb_w / 2;
        const int half = thumb_w / 2;
        const int thumb_h = track_h + 8;
        const int ty0 = cy - thumb_h / 2;
        area.fill_rect(vx - half, ty0, vx + half - 1, ty0 + thumb_h - 1,
                       thumb_color.value_or(theme().accent));
        if (is_focused())
        {
            area.draw_rect(vx - half, ty0, vx + half - 1, ty0 + thumb_h - 1,
                           theme().focus_mark);
        }
    }
}