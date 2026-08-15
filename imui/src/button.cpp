#include "button.hpp"

namespace zb::ui
{
    void Button::press()
    {
        if (state_ != state::pressed)
        {
            mark_dirty();
        }
        state_ = state::pressed;
    }

    void Button::release()
    {
        if (state::pressed != state_)
        {
            return;
        }
        mark_dirty();
        state_ = state::normal;
        clicked();
    }

    void Button::cancel()
    {
        if (state_ != state::normal)
        {
            mark_dirty();
        }
        state_ = state::normal;
    }

    void Button::on_cancel()
    {
        cancel();
    }

    bool Button::on_input(const input::input_event &ev)
    {
        if (ev.type == input::input_type::mouse_left_down ||
            ev.type == input::input_type::touch_down)
        {
            press();
            return true;
        }
        if (ev.type == input::input_type::mouse_left_up ||
            ev.type == input::input_type::touch_up)
        {
            release();
            return true;
        }
        return false;
    }

    void Button::draw_at(core::Graphics &area) const
    {
        if (state::pressed == state_)
        {
            if (pressed_image.has_value())
            {
                area.draw_image(*pressed_image, 0, 0);
            }
            else
            {
                area.fill(pressed_color);
            }
        }
        if (show_border)
        {
            const auto s = get_size();
            area.draw_rect(0, 0, s.width - 1, s.height - 1, is_focused() ? focus_border : border);
        }
        draw_text(area);
    }
}
