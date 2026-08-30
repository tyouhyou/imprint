#include "progress_bar.hpp"

#include "logging.hpp"

namespace zb::ui
{
    void ProgressBar::set_range(const int mn, const int mx)
    {
        mark_dirty();
        min = mn;
        max = mx;
        if (max < min)
        {
            LW << "progress bar range reversed; clamping to a point";
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

    void ProgressBar::set_value(const int v)
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
            return;  // no change: no damage (a bar pinned at its value
                    // must not repaint when fed the same value)
        }
        mark_dirty();
        value = nv;
    }

    void ProgressBar::draw_at(core::Graphics &area) const
    {
        const auto s = get_size();
        if (s.width < 2 || s.height < 2)
        {
            return;
        }

        // track
        area.fill_rect(0, 0, s.width - 1, s.height - 1,
                       track_color.value_or(theme().field_bg));

        // fill: interior span only (the outline owns the border pixels)
        const int span = s.width - 2;
        const int fill_w = (max > min) ? (value - min) * span / (max - min) : 0;
        if (fill_w > 0)
        {
            area.fill_rect(1, 1, fill_w, s.height - 2,
                           fill_color.value_or(theme().accent));
        }

        // outline: keeps the track legible where field_bg nearly matches
        // the background (light theme at 16bpp)
        area.draw_rect(0, 0, s.width - 1, s.height - 1, theme().border);
    }
}
