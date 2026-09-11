#include "knob.hpp"
#include "theme.hpp"

namespace zb::ui
{
    void Knob::set_range(const int mn, const int mx)
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

    void Knob::set_value(const int v)
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

    int Knob::value_deg() const
    {
        const int den = max - min;
        if (den <= 0)
        {
            return -135;
        }
        const int num = value - min;
        return -135 + num * 270 / den;
    }

    bool Knob::apply_step(const int dir)
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

    void Knob::apply_delta(const int n)
    {
        int v = value + n;
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
            return;
        }
        mark_dirty();
        value = v;
        changed(v);
    }

    bool Knob::hit(const int x, const int y) const
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

    bool Knob::on_input(const zb::input::input_event &ev)
    {
        switch (ev.type)
        {
        case zb::input::input_type::mouse_left_down:
        case zb::input::input_type::touch_down:
            captured_drag_ = true;
            drag_budget_ = 0;
            last_y_ = ev.y;
            return true;

        case zb::input::input_type::mouse_move:
        case zb::input::input_type::touch_move:
        {
            if (!captured_drag_)
            {
                return false;
            }
            const int dy = ev.y - last_y_;
            last_y_ = ev.y;
            if (dy == 0)
            {
                return false;
            }
            const auto s = get_size();
            const int span = s.height > 0 ? s.height : 1;
            // one full widget height = whole range; drag up (dy<0) raises
            const int scale = (max - min) * 256 / span;
            drag_budget_ += -dy * scale;
            const int n = drag_budget_ / 256;
            if (n != 0)
            {
                drag_budget_ -= n * 256;
                apply_delta(n);
            }
            return true;
        }

        case zb::input::input_type::mouse_left_up:
        case zb::input::input_type::touch_up:
            captured_drag_ = false;
            return true;

        case zb::input::input_type::mouse_wheel:
            return apply_step(ev.delta > 0 ? 1 : -1);

        case zb::input::input_type::key_down:
            if (ev.key == static_cast<int>(zb::input::key_code::up) ||
                ev.key == static_cast<int>(zb::input::key_code::right))
            {
                return apply_step(1);
            }
            if (ev.key == static_cast<int>(zb::input::key_code::down) ||
                ev.key == static_cast<int>(zb::input::key_code::left))
            {
                return apply_step(-1);
            }
            return false;

        default:
            return false;
        }
    }

    void Knob::draw_at(core::Graphics &area) const
    {
        const auto ks = get_size();
        const int w = ks.width;
        const int h = ks.height;
        const int radius = (w < h ? w : h) / 2 - 1;
        if (radius < 2)
        {
            return;
        }
        const int cx = w / 2;
        const int cy = h / 2;

        const core::Color face_c = face_color.value_or(theme().field_bg);
        const core::Color ring_c = ring_color.value_or(theme().border);
        const core::Color ptr_c  = pointer_color.value_or(theme().accent);

        // face disc + ring outline
        area.fill_circle(cx, cy, radius, face_c);
        area.draw_circle(cx, cy, radius, ring_c);

        // pointer line from centre to the arc rim
        int px = 0, py = 0;
        core::point_on_circle(cx, cy, radius - 4, value_deg(), &px, &py);
        area.draw_line(cx, cy, px, py, ptr_c);

        // hub dot
        area.fill_circle(cx, cy, 2, ring_c);

        // keyboard focus ring
        if (is_focused())
        {
            area.draw_circle(cx, cy, radius, theme().focus_mark);
        }
    }
}
