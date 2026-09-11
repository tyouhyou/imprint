#include "trend_line.hpp"
#include "theme.hpp"

namespace zb::ui
{
    void TrendLine::set_series(const int *data, const std::size_t n)
    {
        samples_.assign(data, data + n);
        if (static_cast<int>(samples_.size()) > max_points_)
        {
            samples_.erase(samples_.begin(),
                           samples_.begin() + (samples_.size() - max_points_));
        }
        mark_dirty();
    }

    void TrendLine::append(const int v)
    {
        samples_.push_back(v);
        if (static_cast<int>(samples_.size()) > max_points_)
        {
            samples_.erase(samples_.begin());
        }
        mark_dirty();
    }

    void TrendLine::clear()
    {
        if (samples_.empty())
        {
            return;
        }
        samples_.clear();
        mark_dirty();
    }

    void TrendLine::set_max_points(const int n)
    {
        max_points_ = n > 0 ? n : 1;
        if (static_cast<int>(samples_.size()) > max_points_)
        {
            samples_.erase(samples_.begin(),
                           samples_.begin() + (samples_.size() - max_points_));
            mark_dirty();
        }
    }

    void TrendLine::set_y_auto()
    {
        y_fixed_ = false;
        mark_dirty();
    }

    void TrendLine::set_y_range(const int mn, const int mx)
    {
        y_fixed_ = true;
        y_min_ = mn;
        y_max_ = mx;
        mark_dirty();
    }

    void TrendLine::draw_at(core::Graphics &area) const
    {
        const auto s = get_size();
        const int w = s.width;
        const int h = s.height;
        if (w < 4 || h < 4)
        {
            return;
        }

        const core::Color bg   = bg_color.value_or(theme().field_bg);
        const core::Color line = line_color.value_or(theme().accent);
        const core::Color brd  = theme().border;

        // card background
        area.fill_round_rect(0, 0, w - 1, h - 1, 2, bg);
        area.draw_round_rect(0, 0, w - 1, h - 1, 2, brd);

        const std::size_t n = samples_.size();
        if (n < 2)
        {
            return;
        }

        // y scale: fixed or auto
        int lo = y_min_;
        int hi = y_max_;
        if (!y_fixed_)
        {
            lo = samples_[0];
            hi = samples_[0];
            for (std::size_t i = 1; i < n; ++i)
            {
                if (samples_[i] < lo)
                {
                    lo = samples_[i];
                }
                if (samples_[i] > hi)
                {
                    hi = samples_[i];
                }
            }
        }

        const int range = hi - lo;
        const int y_top = 2;
        const int y_base = h - 3;
        const int span = y_base - y_top;

        // polyline
        for (std::size_t i = 1; i < n; ++i)
        {
            const int x0 = 2 + static_cast<int>(i - 1) * (w - 4) / static_cast<int>(n - 1);
            const int x1 = 2 + static_cast<int>(i)     * (w - 4) / static_cast<int>(n - 1);

            auto map_y = [&](const int v) -> int
            {
                if (range <= 0)
                {
                    return y_top + span / 2;
                }
                return y_base - (v - lo) * span / range;
            };

            area.draw_line_aa(x0, map_y(samples_[i - 1]), x1, map_y(samples_[i]), line);
        }
    }
}
