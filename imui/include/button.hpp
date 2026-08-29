#pragma once

#include <optional>

#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Push button.
     *
     * The press/release/cancel state machine is driven by the input
     * dispatcher: press() on press, release() on release, cancel() when the
     * pointer leaves the button while held. A click is emitted on release()
     * only if the button is pressed and was not cancelled.
     *
     * Foreground: when pressed, the button draws pressed_color (or the
     * pressed image, which wins) over the base background, then the border,
     * then the text (see Widget::draw_text).
     */
    class Button : public Widget
    {
    public:
        enum class state
        {
            normal,
            pressed
        };

        Button() = default;

        // input hooks, driven by the input dispatcher
        void press();
        void release();
        void cancel();

        [[nodiscard]] state get_state() const { return state_; }

        // content-derived natural size (text + padding + border) so flex
        // layouts and design files can size buttons without explicit
        // width/height
        [[nodiscard]] core::imsize_t measure() const override;

        // per-widget overrides of theme tokens; unset follows the active
        // theme (contract 10.3)
        void set_pressed_color(const core::Color &c) { pressed_color = c; }
        void set_pressed_image(const core::image_t &img) { pressed_image = img; }
        void set_border_color(const core::Color &c) { border = c; }
        void set_focus_border_color(const core::Color &c) { focus_border = c; }
        void set_show_border(const bool b) { show_border = b; }

        // emitted when the button is released while pressed
        zb::event::Event<> clicked;

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const input::input_event &ev) override;
        void on_cancel() override;
        bool is_focusable() const override { return true; }
        void on_activate() override { clicked(); }

    private:
        state state_ = state::normal;
        std::optional<core::Color> pressed_color;  // override of theme accent
        std::optional<core::image_t> pressed_image;
        std::optional<core::Color> border;        // override of theme border
        std::optional<core::Color> focus_border;  // override of theme accent
        bool show_border = true;
    };
}
