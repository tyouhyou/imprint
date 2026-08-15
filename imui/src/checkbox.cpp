#include "checkbox.hpp"

namespace zb::ui
{
    core::imsize_t Checkbox::measure() const
    {
        if (get_text().empty())
        {
            return {box_size, box_size};
        }
        const int w = box_size + text_gap + text_advance();
        const int h = box_size > text_height() ? box_size : text_height();
        return {w, h};
    }

    void Checkbox::press()
    {
        pressed_ = true;
    }

    void Checkbox::release()
    {
        if (!pressed_)
        {
            return;
        }
        pressed_ = false;
        toggle();
    }

    void Checkbox::cancel()
    {
        pressed_ = false;
    }

    void Checkbox::set_checked(const bool c)
    {
        checked_ = c;
    }

    void Checkbox::toggle()
    {
        checked_ = !checked_;
        changed(checked_);
    }

    void Checkbox::on_cancel()
    {
        cancel();
    }

    void Checkbox::on_activate()
    {
        toggle();
    }

    bool Checkbox::on_input(const zb::input::input_event &ev)
    {
        if (ev.type == zb::input::input_type::mouse_left_down ||
            ev.type == zb::input::input_type::touch_down)
        {
            press();
            return true;
        }
        if (ev.type == zb::input::input_type::mouse_left_up ||
            ev.type == zb::input::input_type::touch_up)
        {
            release();
            return true;
        }
        return false;
    }

    void Checkbox::draw_at(core::Graphics &area) const
    {
        const auto s = get_size();
        (void)s;

        // the box; the border highlights while focused (like Button)
        const auto box_edge = is_focused() ? check_color : box_color;
        area.draw_rect(0, 0, box_size, box_size, box_edge);
        if (pressed_)
        {
            area.fill_rect(1, 1, box_size - 2, box_size - 2, check_color);
        }
        if (checked_)
        {
            // a simple two-stroke check mark
            area.draw_line(2, box_size * 5 / 8, box_size * 4 / 8, box_size - 3, check_color);
            area.draw_line(box_size * 4 / 8, box_size - 3, box_size - 3, 2, check_color);
        }
        // the label to the right of the box (draw_text applies the offset)
        set_text_offset(core::impoint_t{box_size + text_gap, 0});
        draw_text(area);
    }
}