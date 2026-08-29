#include "button.hpp"

namespace zb::ui
{
    namespace
    {
        constexpr int kSidePadding = 8;
        constexpr int kVerticalPadding = 6;
    }

    core::imsize_t Button::measure() const
    {
        const int w = (get_text().empty() ? 0 : text_advance()) + 2 * kSidePadding + 2;
        const int h = text_height() + 2 * kVerticalPadding + 2;
        return {w, h};
    }

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
                area.fill(pressed_color.value_or(theme().accent));
            }
        }
        if (show_border)
        {
            const auto s = get_size();
            const core::Color edge = is_focused()
                                         ? focus_border.value_or(theme().accent)
                                         : border.value_or(theme().border);
            area.draw_rect(0, 0, s.width - 1, s.height - 1, edge);
        }
        draw_text(area);
    }
}
