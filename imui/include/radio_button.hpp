#pragma once

#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Mutually exclusive option button. All RadioButtons in the same
     * group (int id) under the same parent form one selection set: the
     * users of the frame keeps every other set member unselected whenever
     * one becomes selected.
     *
     * Press/release/cancel follows Button; the selection happens on
     * release while pressed (a press dragged away cancels), or on
     * keyboard activation. `changed` fires only for the button that just
     * became selected (programmatic set_checked is silent).
     */
    class RadioButton : public Widget
    {
    public:
        RadioButton() { sync_text_offset(); }

        // input hooks, driven by the input dispatcher
        void press();
        void release();
        void cancel();

        void set_group(const int g) { group_ = g; mark_dirty(); }
        [[nodiscard]] int get_group() const { return group_; }

        [[nodiscard]] bool is_checked() const { return checked_; }

        /*
         * Programmatic selection: selects this button (and unselects the
         * same-group siblings), without emitting `changed`. Silent state
         * changes are never reported by this widget.
         */
        void set_checked(const bool c);

        void set_circle_color(const core::Color &c) { circle_color = c; mark_dirty(); }
        void set_dot_color(const core::Color &c) { dot_color = c; mark_dirty(); }
        // circle_size/text_gap feed measure(): they report damage and
        // invalidate the layout too
        void set_circle_size(const int s) { circle_size = s; sync_text_offset(); mark_dirty(); mark_layout_dirty(); }
        void set_text_gap(const int g) { text_gap = g; sync_text_offset(); mark_dirty(); mark_layout_dirty(); }

        // fired when this button becomes selected by a user interaction
        zb::event::Event<> changed;

        // natural size: circle, or circle + gap + label (tallest of two)
        [[nodiscard]] core::imsize_t measure() const override;

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        void on_cancel() override;
        bool is_focusable() const override { return true; }
        void on_activate() override;

        // group notification hook (called on the same-parent siblings):
        // a radio unchecks itself when its group was won elsewhere
        void on_group_selected(const int group, const Widget *selected) override;

    private:
        void select();

        // the label sits right of the circle; recompute whenever
        // circle_size or text_gap changes so the const draw path never
        // writes state
        void sync_text_offset() { set_text_offset(core::impoint_t{circle_size + text_gap, 0}); }

        int group_ = 0;
        bool checked_ = false;
        bool pressed_ = false;

        int circle_size = 12;
        int text_gap = 4;
        core::Color circle_color = core::colors::Black;
        core::Color dot_color = core::colors::Blue;
    };
}