#include "toggle_switch.hpp"

#include "logging.hpp"

namespace zb::ui
{
    void ToggleSwitch::set_track_size(const core::imsize_t &s)
    {
        // the knob radius derives as (height - 6) / 2 and must stay
        // >= 0, and the pill must fit the dot at both ends
        const int h = s.height < 6 ? 6 : s.height;
        const int w = s.width < h ? h : s.width;
        if (w != s.width || h != s.height)
        {
            LW << "toggle: track degenerates below a 6 px knob; clamping";
        }
        if (track_size_.width != w || track_size_.height != h)
        {
            mark_dirty();
            mark_layout_dirty();
        }
        track_size_ = core::imsize_t{w, h};
    }

    void ToggleSwitch::press()
    {
        mark_dirty();
        pressed_ = true;
    }

    void ToggleSwitch::release()
    {
        if (!pressed_)
        {
            return;
        }
        mark_dirty();
        pressed_ = false;
        toggle();
    }

    void ToggleSwitch::cancel()
    {
        if (pressed_)
        {
            mark_dirty();
        }
        pressed_ = false;
    }

    void ToggleSwitch::set_checked(const bool c)
    {
        if (checked_ != c)
        {
            mark_dirty();
        }
        checked_ = c;
    }

    void ToggleSwitch::toggle()
    {
        mark_dirty();
        checked_ = !checked_;
        changed(checked_);
    }

    void ToggleSwitch::on_cancel()
    {
        cancel();
    }

    void ToggleSwitch::on_activate()
    {
        toggle();
    }

    bool ToggleSwitch::on_input(const zb::input::input_event &ev)
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

    void ToggleSwitch::draw_at(core::Graphics &area) const
    {
        const int w = track_size_.width;
        const int h = track_size_.height;
        const int r = h / 2;

        const core::Color on  = track_color.value_or(theme().accent);
        const core::Color off = theme().field_bg;
        const core::Color dot = dot_color.value_or(theme().text_inverted);

        // track: checked or pressed fills accent immediately (visual
        // feedback mirrors the checkbox pressed-fill). AA adoption: the
        // pill blends its edge through the new round-rect AA pair.
        if (checked_ || pressed_)
        {
            area.fill_round_rect_aa(0, 0, w - 1, h - 1, r, on);
        }
        else
        {
            area.fill_round_rect_aa(0, 0, w - 1, h - 1, r, off);
            area.draw_round_rect_aa(0, 0, w - 1, h - 1, r, theme().border);
        }
        if (is_focused())
        {
            area.draw_round_rect_aa(0, 0, w - 1, h - 1, r, theme().focus_mark);
        }

        // knob
        const int dot_r = (h - 6) / 2;
        const int cx = checked_ ? (w - 1 - 2 - dot_r) : (2 + dot_r);
        const int cy = h / 2;
        area.fill_circle_aa(cx, cy, dot_r, dot);
    }
}
