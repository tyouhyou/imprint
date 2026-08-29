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
        mark_dirty();
        pressed_ = true;
    }

    void Checkbox::release()
    {
        if (!pressed_)
        {
            return;
        }
        mark_dirty();
        pressed_ = false;
        toggle();
    }

    void Checkbox::cancel()
    {
        if (pressed_)
        {
            mark_dirty();
        }
        pressed_ = false;
    }

    void Checkbox::set_checked(const bool c)
    {
        if (checked_ != c)
        {
            mark_dirty();
        }
        checked_ = c;
    }

    void Checkbox::toggle()
    {
        mark_dirty();
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

        const core::Color check = check_color.value_or(theme().accent);
        // the box; the border highlights while focused (like Button)
        const core::Color box_edge = is_focused() ? check
                                                  : box_color.value_or(theme().border);
        area.draw_rect(0, 0, box_size, box_size, box_edge);
        if (pressed_)
        {
            area.fill_rect(1, 1, box_size - 2, box_size - 2, check);
        }
        if (checked_)
        {
            // a simple two-stroke check mark
            area.draw_line(2, box_size * 5 / 8, box_size * 4 / 8, box_size - 3, check);
            area.draw_line(box_size * 4 / 8, box_size - 3, box_size - 3, 2, check);
        }
        // the label to the right of the box (the offset is a layout-time
        // value, kept current by the ctor and the box_size/text_gap setters)
        draw_text(area);
    }
}