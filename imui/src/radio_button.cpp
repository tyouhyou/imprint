#include "radio_button.hpp"

namespace zb::ui
{
    core::imsize_t RadioButton::measure() const
    {
        if (get_text().empty())
        {
            return {circle_size, circle_size};
        }
        const int w = circle_size + text_gap + text_advance();
        const int h = circle_size > text_height() ? circle_size : text_height();
        return {w, h};
    }

    void RadioButton::press()
    {
        mark_dirty();
        pressed_ = true;
    }

    void RadioButton::release()
    {
        if (!pressed_)
        {
            return;
        }
        mark_dirty();
        pressed_ = false;
        select();
    }

    void RadioButton::cancel()
    {
        if (pressed_)
        {
            mark_dirty();
        }
        pressed_ = false;
    }

    void RadioButton::set_checked(const bool c)
    {
        if (c && !checked_)
        {
            mark_dirty();
            checked_ = true;
            notify_siblings_group_selection(group_, this);
        }
        else if (!c && checked_)
        {
            mark_dirty();
            checked_ = false;
        }
    }

    void RadioButton::select()
    {
        if (checked_)
        {
            return;
        }
        set_checked(true);
        changed();
    }

    void RadioButton::on_cancel()
    {
        cancel();
    }

    void RadioButton::on_activate()
    {
        select();
    }

    bool RadioButton::on_input(const zb::input::input_event &ev)
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

    void RadioButton::on_group_selected(const int group, const Widget *selected)
    {
        if (selected != this && group == group_ && checked_)
        {
            mark_dirty();  // the sibling unchecks: its visuals change
            checked_ = false;
        }
    }

    void RadioButton::draw_at(core::Graphics &area) const
    {
        const auto center = circle_size / 2;
        const auto radius = circle_size / 2 - 1;

        // the ring highlights while focused (like the Button border)
        area.draw_ellipse(center, center, radius, radius, is_focused() ? dot_color : circle_color);
        if (checked_)
        {
            const auto dot = radius / 2;
            area.fill_ellipse(center, center, dot, dot, dot_color);
        }
        // the label to the right of the circle (the offset is a
        // layout-time value, kept current by the ctor and the
        // circle_size/text_gap setters)
        draw_text(area);
    }
}