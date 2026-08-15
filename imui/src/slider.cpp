#include "slider.hpp"

#include "logging.hpp"

namespace zb::ui
{
    void Slider::set_range(const int mn, const int mx)
    {
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
        int v = min + (cx - thumb_w / 2) * (max - min) / span;
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
            value = value_from_x(ev.x - pos.x);
            changed(value);
            return true;
        }
        case zb::input::input_type::mouse_move:
        case zb::input::input_type::touch_move:
        {
            const auto pos = get_absolute_position();
            const int v = value_from_x(ev.x - pos.x);
            if (v != value)
            {
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
        area.fill_rect(0, track_y0, s.width - 1, track_y0 + track_h - 1, track_color);

        // thumb: center travels [thumb_w/2 .. width-1-thumb_w/2]
        const int span = s.width - thumb_w;
        const int vx = (max > min)
                           ? thumb_w / 2 + (value - min) * span / (max - min)
                           : thumb_w / 2;
        const int half = thumb_w / 2;
        const int thumb_h = track_h + 8;
        const int ty0 = cy - thumb_h / 2;
        area.fill_rect(vx - half, ty0, vx + half - 1, ty0 + thumb_h - 1, thumb_color);
        if (is_focused())
        {
            area.draw_rect(vx - half, ty0, vx + half - 1, ty0 + thumb_h - 1, core::colors::Red);
        }
    }
}