#pragma once

#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Single-line text editor. ASCII scope for now (see docs/code-
     * contract.md, batch B): printable characters insert at the caret,
     * backspace deletes before it, del deletes after it, left/right move
     * the caret, enter submits. The caret is a static block (no timer,
     * no blink animation).
     *
     * The text lives in Widget::text_slots (the base text_): set_text()
     * is inherited and works through a Widget reference (builder
     * materialization included). The caret is an index into it, clamped
     * on every edit; a programmatic set_text leaves it at the start.
     *
     * `changed` fires with the UTF-8 text after every edit; `submitted`
     * fires with the UTF-8 text on enter.
     *
     * The frame highlights while focused; the text is drawn via the
     * shared text helpers (advance_of / draw_text_at), so custom glyph
     * providers apply.
     */
    class TextInput : public Widget
    {
    public:
        TextInput() = default;

        // fired on every edit, with the whole text (UTF-8)
        zb::event::Event<std::string> changed;
        // fired on enter, with the whole text (UTF-8)
        zb::event::Event<std::string> submitted;

        void set_border_color(const core::Color &c) { border_color = c; mark_dirty(); }
        void set_caret_color(const core::Color &c) { caret_color = c; mark_dirty(); }
        void set_text_gap(const int g) { text_gap = g; mark_dirty(); }

        // natural size: 100 wide, one line plus the border
        [[nodiscard]] core::imsize_t measure() const override;

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        bool is_focusable() const override { return true; }

    private:
        void clamp_caret();
        void insert(const char16_t ch);
        bool erase_before_caret();
        bool erase_at_caret();
        bool move_caret(const int dir);
        void place_caret_at(const int x);
        void fire_changed();

        size_t caret = 0;
        int text_gap = 2;
        // unset = follow the active theme (contract 10.3)
        std::optional<core::Color> border_color;  // override of theme border
        std::optional<core::Color> caret_color;   // override of theme accent
    };
}  // namespace zb::ui