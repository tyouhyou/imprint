#pragma once

#include <memory>
#include <string>
#include <vector>

#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Model-view ListBox: the widget stores only scroll/selection state,
     * the caller owns the items. Rows are fixed-height; the widget height
     * is set from the number of visible rows (set_visible_rows). The row
     * text is pulled lazily per visible row from an ItemText callback.
     *
     * Interaction: press a row to select (changed fires with the row
     * index, or `invalid` semantics for a cleared model); the mouse wheel
     * scrolls the window (3 rows per notch) and only has an effect when
     * the content overflows; up/down on the focused list move the
     * selection and scroll to keep it visible. The scrollbar thumb can be
     * dragged (captures the pointer while dragging; the press is never
     * cancelled). A pressed row is re-selected without an event when the
     * value is unchanged.
     */
    class ListBox : public Widget
    {
    public:
        // selection sentinel for "no item selected"
        static constexpr size_t invalid = static_cast<size_t>(-1);

        // returns the text for the given row; arg comes from
        // set_item_text (may be null)
        using ItemText = std::string (*)(const void *arg, size_t index);

        ListBox() = default;

        void set_row_height(const int h) { row_height = h; mark_dirty(); }
        // re-sizes the widget: width stays, height = rows * row_height
        void set_visible_rows(const size_t rows);
        void set_item_count(const size_t n);

        // text source for rows (no text is drawn when unset)
        void set_item_text(const ItemText fn, const void *arg = nullptr)
        {
            text_fn = fn;
            text_arg = arg;
            mark_dirty();
        }

        // programmatic selection (silent); `invalid` clears it
        void set_value(const size_t v);
        [[nodiscard]] size_t get_value() const { return value; }
        // index of the first visible row
        [[nodiscard]] size_t get_top() const { return top; }

        /*
         * Static string model owned by the widget: fills the rows from a
         * plain list (no ItemText callback needed). The dynamic model
         * (ItemText) takes precedence when both are set.
         */
        void set_items(std::vector<std::string> items);

        // fired when the selection changed through user input
        zb::event::Event<size_t> changed;

        // natural size: 100 wide, visible rows tall
        [[nodiscard]] core::imsize_t measure() const override
        {
            return {100, static_cast<int>(visible) * row_height};
        }

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        bool is_focusable() const override { return true; }
        [[nodiscard]] bool captures_pointer() const override { return dragging; }

    private:
        // largest legal value of `top` (0 when the content fits)
        [[nodiscard]] size_t max_top() const;

        // keeps `sel` inside the visible window, moving `top` as needed
        void reveal(const size_t sel);
        // moves the selection one row in `dir` (+1/-1); false at the
        // ends or on an empty model (fall back to focus navigation)
        bool step_selection(const int dir);
        // scrolls the window `dir * 3` rows; false when nothing moved
        bool scroll_rows(const int dir);
        void clamp_top();

        // scrollbar layout for the current geometry; empty rect when the
        // bar is not shown
        void scrollbar_rect(int *x0, int *y0, int *height, int *thumb_y, int *thumb_h) const;
        // maps local coordinates to rows, or invalid when outside the
        // row area
        [[nodiscard]] size_t row_at(const int x, const int y) const;

        static constexpr int scroll_step = 3;  // rows per wheel notch
        static constexpr int gutter = 8;       // scrollbar width

        int row_height = 16;
        size_t visible = 4;
        size_t count = 0;
        size_t top = 0;
        size_t value = invalid;

        ItemText text_fn = nullptr;
        const void *text_arg = nullptr;
        std::shared_ptr<std::vector<std::string>> items_;  // static model
        size_t static_count = 0;

        bool dragging = false;  // a thumb drag is in flight
        int drag_grab = 0;      // press offset from the thumb top
    };
}