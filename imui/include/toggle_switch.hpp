#pragma once

#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Two-state pill switch with a sliding knob.
     *
     * The press/release/cancel state machine mirrors Checkbox: the toggle
     * fires on release() while pressed, so a press dragged away cancels
     * instead of toggling; activation (Enter/Space) toggles immediately.
     *
     * `changed` fires for every user interaction with the new state;
     * set_checked() is programmatic and silent. Two states only; tri-state
     * is deliberately out of scope (the Checkbox note applies verbatim).
     */
    class ToggleSwitch : public Widget
    {
    public:
        ToggleSwitch() = default;

        void press();
        void release();
        void cancel();

        [[nodiscard]] bool is_checked() const { return checked_; }
        void set_checked(const bool c);
        [[nodiscard]] bool is_pressed() const { return pressed_; }

        void set_track_color(const core::Color &c) { track_color = c; mark_dirty(); }
        void set_dot_color(const core::Color &c) { dot_color = c; mark_dirty(); }
        void set_track_size(const core::imsize_t &s);
        [[nodiscard]] const core::imsize_t &track_size() const { return track_size_; }

        // fired on user toggle, carrying the new state
        zb::event::Event<bool> changed;

        [[nodiscard]] core::imsize_t measure() const override { return track_size_; }

    protected:
        void draw_at(core::Graphics &area) const override;
        void on_cancel() override;
        bool is_focusable() const override { return true; }
        void on_activate() override;

    public:
        bool on_input(const zb::input::input_event &ev) override;

    private:
        void toggle();

        bool checked_ = false;
        bool pressed_ = false;
        core::imsize_t track_size_{40, 20};
        // unset = follow the active theme (contract 10.3)
        std::optional<core::Color> track_color;  // accent when on, field_bg when off
        std::optional<core::Color> dot_color;    // text_inverted
    };
}
