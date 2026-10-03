#pragma once

#include "flex_panel.hpp"

namespace zb::ui
{
    /*
     * H-4 vertical scroll container: a FlexPanel column whose overflow
     * scrolls instead of clipping. Children lay out per the flex rules
     * (direction is always column); when their stacked extent exceeds
     * the panel's height, the excess is reachable by scrolling — the
     * mouse wheel (or a dragged scrollbar thumb) moves the content under
     * a hard viewport edge.
     *
     * Mechanics: the scroll offset only shifts the nested clip origin at
     * draw time and the child-local y at hit-test time; layout always
     * sees the unscrolled frame, so scrolling never re-runs layout and
     * the flex math is identical to a plain column. Children poking
     * above/below the viewport are cut by the enclosing draw area (the
     * same clip that bounds every widget), so a scrolled-out child can
     * never smear over its neighbors.
     *
     * Interaction: wheel up (delta > 0) scrolls toward the top, one
     * wheel_step per notch; pressing the scrollbar thumb captures the
     * pointer until release (ListBox precedent — the drag is never
     * cancelled). A click on the content maps through the offset, so
     * buttons/rows under the viewport edge hit where they draw.
     */
    class ScrollPanel : public FlexPanel
    {
    public:
        ScrollPanel()
        {
            set_direction(flex_direction::column);
        }

        // current scroll offset in pixels (0 = top)
        [[nodiscard]] int scroll_offset() const { return top_; }
        // clamped against the content extent; marks dirty when it moves
        void set_scroll_offset(const int v);
        // how many pixels of content hide below the viewport (0 = fits)
        [[nodiscard]] int max_scroll() const;
        // re-clamps the offset after any content/viewport change: paint()
        // always runs layout() before draw (contract 11), so draw/pick/
        // on_input never see a stale offset (e.g. scrolled to the bottom,
        // then rows removed)
        void layout() override;

    protected:
        void draw_at(core::Graphics &area) const override;
        Widget *pick(const int x, const int y) override;
        bool on_input(const zb::input::input_event &ev) override;
        void on_cancel() override { dragging_ = false; }
        [[nodiscard]] bool captures_pointer() const override { return dragging_; }

    private:
        // the content frame lives top_ pixels above the panel box
        [[nodiscard]] int content_frame_shift_y() const override { return -top_; }

        // scrollbar layout for the current geometry; an empty rect when
        // the content fits (no bar)
        void scrollbar_rect(int *x0, int *y0, int *height, int *thumb_y, int *thumb_h) const;
        // scrolls by delta pixels (positive = toward the top); true when
        // the offset moved
        bool scroll_by(const int delta);

        int top_ = 0;           // scroll offset in pixels
        bool dragging_ = false; // a thumb drag is in flight
        int drag_grab_ = 0;     // press offset from the thumb top

        static constexpr int gutter = 8;      // scrollbar width
        static constexpr int wheel_step = 32; // px per wheel notch
    };
}
