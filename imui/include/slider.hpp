#pragma once

#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Horizontal slider over an int range.
     *
     * The thumb jumps to the press position and follows the pointer
     * (the widget captures the pointer while pressed, so dragging past
     * the ends clamps the value; the press is never cancelled).
     * Keyboard: left/right step by `step` (focused widget consumes the
     * key first). The mouse wheel steps too. `changed` fires with the
     * new value on every user interaction that moves the value;
     * set_value() is programmatic and silent.
     */
    class Slider : public Widget
    {
    public:
        Slider() = default;

        void set_range(const int min, const int max);
        void set_value(const int v);
        [[nodiscard]] int get_value() const { return value; }
        void set_step(const int s) { step = s; }

        void set_track_color(const core::Color &c) { track_color = c; }
        void set_thumb_color(const core::Color &c) { thumb_color = c; }
        void set_thumb_width(const int w) { thumb_w = w; }
        void set_track_height(const int h) { track_h = h; }

        // fired when the value changes through a user interaction
        zb::event::Event<int> changed;

        // natural size: 100x20
        [[nodiscard]] core::imsize_t measure() const override { return {100, 20}; }

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        bool is_focusable() const override { return true; }
        [[nodiscard]] bool captures_pointer() const override { return true; }

    private:
        // maps a widget-local x (clamped to the widget) to a value
        int value_from_x(const int x) const;
        // steps by one step in direction (+1/-1); returns true when the
        // value actually moved
        bool apply_step(const int dir);

        int min = 0;
        int max = 100;
        int value = 0;
        int step = 1;

        int thumb_w = 10;
        int track_h = 4;
        core::Color track_color = core::colors::Black;
        core::Color thumb_color = core::colors::Blue;
    };
}