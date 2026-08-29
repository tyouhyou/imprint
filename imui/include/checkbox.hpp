#pragma once

#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Two-state check box with an optional label drawn to the right of
     * the box.
     *
     * The press/release/cancel state machine mirrors Button: the toggle
     * fires on release() while pressed, so a press dragged away from the
     * box cancels instead of toggling. Activation (Enter/Space while
     * focused) toggles immediately.
     *
     * `changed` carries the new state and fires for every user interaction
     * (pointer or keyboard); set_checked() is programmatic and silent.
     * Tri-state is deliberately out of scope; when needed it is a small
     * API break (bool -> enum state, Event<bool> -> Event<state>).
     */
    class Checkbox : public Widget
    {
    public:
        Checkbox() { sync_text_offset(); }

        // input hooks, driven by the input dispatcher
        void press();
        void release();
        void cancel();

        [[nodiscard]] bool is_checked() const { return checked_; }
        void set_checked(const bool c);
        [[nodiscard]] bool is_pressed() const { return pressed_; }

        void set_box_color(const core::Color &c) { box_color = c; mark_dirty(); }
        void set_check_color(const core::Color &c) { check_color = c; mark_dirty(); }
        // box_size/text_gap feed measure(): they invalidate the layout too
        void set_box_size(const int s) { box_size = s; sync_text_offset(); mark_dirty(); mark_layout_dirty(); }
        void set_text_gap(const int g) { text_gap = g; sync_text_offset(); mark_dirty(); mark_layout_dirty(); }

        // fired on user toggle, with the new state
        zb::event::Event<bool> changed;

        // natural size: box, or box + gap + label (tallest of the two)
        [[nodiscard]] core::imsize_t measure() const override;

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        void on_cancel() override;
        bool is_focusable() const override { return true; }
        void on_activate() override;

    private:
        void toggle();

        // the label sits right of the box; recompute whenever box_size or
        // text_gap changes so the const draw path never writes state
        void sync_text_offset() { set_text_offset(core::impoint_t{box_size + text_gap, 0}); }

        bool checked_ = false;
        bool pressed_ = false;

        int box_size = 14;
        int text_gap = 4;
        // unset = follow the active theme (contract 10.3)
        std::optional<core::Color> box_color;    // override of theme border
        std::optional<core::Color> check_color;  // override of theme accent
    };
}