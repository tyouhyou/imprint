#pragma once

#include "widget.hpp"

namespace zb::ui
{
    /*
     * Circular gauge dial: a value arc over a track, radial ticks, a
     * needle. Display-only (the ProgressBar rule): not focusable, no
     * events, set_value() clamps and repaints only on real change.
     *
     * Geometry uses core::point_on_circle (integer-degree, two-trig-path)
     * so needle and arc always agree. Default arc -225° → +45° (270° sweep,
     * gap at the lower half): classic gauge.
     *
     * hit() is the circular face (A-24, the first in-tree overriding
     * consumer alongside Knob): disc of radius min(w,h)/2 - 1, reproducing
     * the base is_visible() gate.
     */
    class GaugeDial : public Widget
    {
    public:
        GaugeDial() { set_size({64, 64}); }

        void set_range(int mn, int mx);
        [[nodiscard]] int get_min() const { return min; }
        [[nodiscard]] int get_max() const { return max; }
        void set_value(int v);
        [[nodiscard]] int get_value() const { return value; }

        void set_start_deg(int d);
        [[nodiscard]] int get_start_deg() const { return start_deg; }
        void set_sweep_deg(int d);
        [[nodiscard]] int get_sweep_deg() const { return sweep_deg; }

        void set_arc_color(const core::Color &c) { arc_color = c; mark_dirty(); }
        void set_track_color(const core::Color &c) { track_color = c; mark_dirty(); }
        void set_needle_color(const core::Color &c) { needle_color = c; mark_dirty(); }
        void set_tick_color(const core::Color &c) { tick_color = c; mark_dirty(); }
        void set_face_color(const core::Color &c) { face_color = c; mark_dirty(); }

        [[nodiscard]] core::imsize_t measure() const override { return {64, 64}; }

        // circular picking hook (A-24) - public geometry query: local-space
        // disc of radius min(w,h)/2 - 1, reproducing the visible gate
        [[nodiscard]] bool hit(int x, int y) const override;

    protected:
        void draw_at(core::Graphics &area) const override;

    private:
        [[nodiscard]] int value_deg() const;

        int min = 0;
        int max = 100;
        int value = 0;
        int start_deg = -225;
        int sweep_deg = 270;
        // unset = follow the active theme (contract 10.3)
        std::optional<core::Color> arc_color;    // accent
        std::optional<core::Color> track_color;  // border
        std::optional<core::Color> needle_color; // accent
        std::optional<core::Color> tick_color;   // border
        std::optional<core::Color> face_color;   // background
    };
}
