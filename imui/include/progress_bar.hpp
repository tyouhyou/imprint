#pragma once

#include "widget.hpp"

namespace zb::ui
{
    /*
     * Horizontal progress bar over an int range (display-only).
     *
     * Non-interactive: not focusable, consumes no input, fires no events.
     * set_value() is programmatic; it marks dirty only when the clamped
     * value actually changes, so feeding a running bar its current value
     * never produces damage.
     *
     * Drawing: recessed track (theme field_bg) + proportional fill
     * (theme accent) + a 1px outline (theme border) that keeps the track
     * legible against light backgrounds at every color depth. Integer
     * math only (embedded-safe).
     */
    class ProgressBar : public Widget
    {
    public:
        ProgressBar() = default;

        void set_range(const int min, const int max);
        void set_value(const int v);
        [[nodiscard]] int get_value() const { return value; }
        [[nodiscard]] int get_min() const { return min; }
        [[nodiscard]] int get_max() const { return max; }

        void set_track_color(const core::Color &c) { track_color = c; mark_dirty(); }
        void set_fill_color(const core::Color &c) { fill_color = c; mark_dirty(); }

        // natural size: 100x12 (fixed; no setter feeds measure, so state
        // changes never need mark_layout_dirty)
        [[nodiscard]] core::imsize_t measure() const override { return {100, 12}; }

    protected:
        void draw_at(core::Graphics &area) const override;

    private:
        int min = 0;
        int max = 100;
        int value = 0;
        // unset = follow the active theme (contract 10.3)
        std::optional<core::Color> track_color;  // override of theme field_bg
        std::optional<core::Color> fill_color;   // override of theme accent
    };
}
