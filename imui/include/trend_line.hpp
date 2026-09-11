#pragma once

#include "widget.hpp"
#include <cstddef>
#include <vector>

namespace zb::ui
{
    /*
     * Trend-line pawn: a scrolling polyline of int samples on a card.
     * Display-only (the ProgressBar rule): not focusable, no events.
     *
     * set_series() replaces the whole series (copied), append() pushes
     * one sample and scrolls the oldest out past max_points, clear()
     * empties. Y mapping is autoscaled to the data's min/max unless
     * set_y_range(min,max) fixes it; a degenerate range pins to the card
     * middle.
     */
    class TrendLine : public Widget
    {
    public:
        TrendLine() { set_size({120, 40}); }

        void set_series(const int *data, std::size_t n);
        void append(const int v);
        void clear();
        [[nodiscard]] std::size_t get_count() const { return samples_.size(); }
        [[nodiscard]] int get_max_points() const { return max_points_; }
        void set_max_points(int n);

        void set_y_auto();
        void set_y_range(int mn, int mx);
        [[nodiscard]] bool is_y_fixed() const { return y_fixed_; }

        void set_line_color(const core::Color &c) { line_color = c; mark_dirty(); }
        void set_bg_color(const core::Color &c) { bg_color = c; mark_dirty(); }

        [[nodiscard]] core::imsize_t measure() const override { return {120, 40}; }

    protected:
        void draw_at(core::Graphics &area) const override;

    private:
        std::vector<int> samples_;
        int max_points_ = 64;
        bool y_fixed_ = false;
        int y_min_ = 0;
        int y_max_ = 0;
        // unset = follow the active theme (contract 10.3)
        std::optional<core::Color> line_color; // accent
        std::optional<core::Color> bg_color;   // field_bg
    };
}
