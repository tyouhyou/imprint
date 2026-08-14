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
        RadioButton() = default;

        // input hooks, driven by the input dispatcher
        void press();
        void release();
        void cancel();

        void set_group(const int g) { group_ = g; }
        [[nodiscard]] int get_group() const { return group_; }

        [[nodiscard]] bool is_checked() const { return checked_; }

        /*
         * Programmatic selection: selects this button (and unselects the
         * same-group siblings), without emitting `changed`. Silent state
         * changes are never reported by this widget.
         */
        void set_checked(const bool c);

        void set_circle_color(const core::Color &c) { circle_color = c; }
        void set_dot_color(const core::Color &c) { dot_color = c; }
        void set_circle_size(const int s) { circle_size = s; }
        void set_text_gap(const int g) { text_gap = g; }

        // fired when this button becomes selected by a user interaction
        zb::event::Event<> changed;

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

        int group_ = 0;
        bool checked_ = false;
        bool pressed_ = false;

        int circle_size = 12;
        int text_gap = 4;
        core::Color circle_color = core::colors::Black;
        core::Color dot_color = core::colors::Blue;
    };
}