#pragma once

#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Rotary knob: a circular dial for a bounded int range.
     *
     * Interactive (the Slider rule): focusable, fires `changed` on user
     * interaction only; set_value() is programmatic and silent. Adjust:
     * vertical drag (drag up = value up, one full widget height = the
     * whole range), wheel (one notch = one step), arrow keys (one step).
     * The pointer is captured while held.
     *
     * hit() is the circular face (A-24, first in-tree overriding consumer
     * alongside GaugeDial). The pointer angle spans −135° to +135°
     * (270° sweep, gap at the lower half) -- the same convention as
     * GaugeDial but with a different default opening.
     */
    class Knob : public Widget
    {
    public:
        Knob() { set_size({40, 40}); }

        void set_range(int mn, int mx);
        [[nodiscard]] int get_min() const { return min; }
        [[nodiscard]] int get_max() const { return max; }
        void set_value(int v);
        [[nodiscard]] int get_value() const { return value; }
        void set_step(int s) { step = s; }
        [[nodiscard]] int get_step() const { return step; }

        void set_face_color(const core::Color &c) { face_color = c; mark_dirty(); }
        void set_ring_color(const core::Color &c) { ring_color = c; mark_dirty(); }
        void set_pointer_color(const core::Color &c) { pointer_color = c; mark_dirty(); }

        // fired on user interaction with the new value
        zb::event::Event<int> changed;

        [[nodiscard]] core::imsize_t measure() const override { return {40, 40}; }

        // circular picking hook (A-24) - public geometry query (same disc
        // convention as GaugeDial)
        [[nodiscard]] bool hit(int x, int y) const override;

    protected:
        void draw_at(core::Graphics &area) const override;
        bool captures_pointer() const override { return true; }
        bool is_focusable() const override { return true; }

    public:
        bool on_input(const zb::input::input_event &ev) override;

    private:
        bool apply_step(int dir);
        void apply_delta(int n);
        [[nodiscard]] int value_deg() const;

        bool captured_drag_ = false;
        int drag_budget_ = 0;   // 256-scaled fractional accumulator
        int last_y_ = 0;

        int min = 0;
        int max = 100;
        int value = 0;
        int step = 1;
        // unset = follow the active theme (contract 10.3)
        std::optional<core::Color> face_color;    // field_bg
        std::optional<core::Color> ring_color;    // border
        std::optional<core::Color> pointer_color; // accent
    };
}
