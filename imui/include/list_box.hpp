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
     * the caller owns the items. Rows are fixed-height by default (the
     * widget height is set from the number of visible rows,
     * set_visible_rows); a height provider (set_item_height, FI-06,
     * code-contract §3.7) switches the offset math to per-row heights —
     * prefix-sum offsets, a binary-search visible window, and a widget
     * height derived from the provider's own model. The row text is
     * pulled lazily per visible row from an ItemText callback.
     *
     * Interaction: press a row to select (changed fires with the row
     * index, or `invalid` semantics for a cleared model); the mouse wheel
     * scrolls the window (3 rows per notch; with a height provider, 3 x
     * the topmost visible row's height) and only has an effect when
     * the content overflows; up/down on the focused list move the
     * selection and scroll to keep it visible. The scrollbar thumb can be
     * dragged (captures the pointer while dragging; the press is never
     * cancelled). A pressed row is re-selected without an event when the
     * value is unchanged.
     *
     * Row images are cached (batch J2): the visible rows are rasterized
     * once per state and re-blitted on warm repaints, so the draw path
     * performs zero allocations while nothing changed. The cache is
     * bounded by the visible window and a fixed byte budget, and every
     * setter that can change the rendered rows (geometry, model,
     * selection, scroll) invalidates it. Content behind a dynamic
     * ItemText that changes without a setter call is the caller's
     * responsibility: any setter call (e.g. set_item_text) resets the
     * cache -- the keys never hash the row text.
     */
    class ListBox : public Widget
    {
    public:
        // selection sentinel for "no item selected"
        static constexpr size_t invalid = static_cast<size_t>(-1);

        // returns the text for the given row; arg comes from
        // set_item_text (may be null)
        using ItemText = std::string (*)(const void *arg, size_t index);

        // returns the pixel height of the given row; arg comes from
        // set_item_height (may be null). A returned value <= 0 is
        // tolerated as 1 (a zero-height row can neither be hit nor seen)
        using ItemHeight = int (*)(const void *arg, size_t index);

        ListBox() = default;

        // clamps to >= 1 (a zero height divided by zero on the next
        // click) and re-derives the widget height from the visible rows
        void set_row_height(const int h);
        // re-sizes the widget: width stays, height = rows * row_height
        // (with a height provider: the sum of the provider's heights for
        // items [0, rows), the current model). A width that was never
        // explicit stays measured (L-3): a declarative list without a
        // width keeps its natural 100px instead of freezing the
        // pre-layout 0 as explicit.
        void set_visible_rows(const size_t rows);
        void set_item_count(const size_t n);

        // text source for rows (no text is drawn when unset)
        void set_item_text(const ItemText fn, const void *arg = nullptr)
        {
            text_fn = fn;
            text_arg = arg;
            invalidate_row_cache();
            mark_dirty();
        }

        // per-row height source (code-contract §3.7). Unset = the fixed
        // row_height for every row (byte-identical behavior). Same
        // invalidation rules as set_item_text: any setter that can
        // change rendered rows resets the row cache; installing or
        // replacing the provider also rebuilds the prefix sums and
        // re-derives the widget height from the current model
        void set_item_height(const ItemHeight fn, const void *arg = nullptr);

        // programmatic selection (silent); `invalid` clears it
        void set_value(const size_t v);
        [[nodiscard]] size_t get_value() const { return value; }
        // index of the first visible row (with a height provider: the
        // row whose span crosses the window top, found by the prefix-sum
        // binary search)
        [[nodiscard]] size_t get_top() const;

        /*
         * Observability seam (code-contract §8): the number of row
         * rasterizations (row-image cache misses) since construction.
         * The invalidation tests assert deltas of it: unlike allocation
         * counting, it works across the shared-library boundary — on
         * Windows the test binary's operator-new replacement cannot see
         * allocations made inside the imcore DLL.
         */
        [[nodiscard]] long long rasterization_count() const { return rasterizations_; }

        /*
         * Static string model owned by the widget: fills the rows from a
         * plain list (no ItemText callback needed). The dynamic model
         * (ItemText) takes precedence when both are set.
         */
        void set_items(std::vector<std::string> items);

        // fired when the selection changed through user input
        zb::event::Event<size_t> changed;

        // natural size: 100 wide, visible rows tall (with a height
        // provider: the sum of the provider's heights for items
        // [0, visible) of the current model)
        [[nodiscard]] core::imsize_t measure() const override;

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        // a cancelled press (reset, a competing press, eviction) must
        // clear an in-flight thumb drag, or the stale dragging flag
        // turns the next row press into a phantom thumb drag
        void on_cancel() override { dragging = false; }
        bool is_focusable() const override { return true; }
        [[nodiscard]] bool captures_pointer() const override { return dragging; }

    private:
        // largest legal value of `top` (0 when the content fits)
        [[nodiscard]] size_t max_top() const;

        // FI-06 variable-height offset math (height provider set):
        // everything below runs in pixel offsets over the prefix sums;
        // without a provider the fixed row math above stands untouched
        // (byte-identical shape)
        // the height of row i: the provider's value floored at 1, or
        // the fixed row_height when no provider is set
        [[nodiscard]] int height_at(const size_t i) const;
        // rebuilds the prefix sums (sums_[i] = the top of row i,
        // sums_[count] = the content total) from the model + provider
        void rebuild_sums();
        // the content height the model yields for `rows` visible rows
        [[nodiscard]] int64_t visible_height(const size_t rows) const;
        // largest legal pixel offset (0 when the content fits)
        [[nodiscard]] int64_t max_top_px() const;
        // smallest row whose span crosses the window top: the binary
        // search over the prefix sums
        [[nodiscard]] size_t row_at_offset() const;
        // whether the scrollbar shows for the current geometry (the
        // overflow test: row-count rule fixed, content-vs-viewport rule
        // with a provider)
        [[nodiscard]] bool overflows() const;

        // keeps `sel` inside the visible window, moving `top` as needed
        void reveal(const size_t sel);
        // moves the selection one row in `dir` (+1/-1); false at the
        // ends or on an empty model (fall back to focus navigation)
        bool step_selection(const int dir);
        // scrolls the window `dir * 3` rows (with a height provider:
        // `dir * 3 x` the topmost visible row's height); false when
        // nothing moved
        bool scroll_rows(const int dir);
        void clamp_top();

        // one row's face + text at (0, y0) with height rh -- shared by
        // the fixed-height and the variable-height draw loops (the
        // wireframe bypass and the row-image cache are identical)
        void draw_row(core::Graphics &area, const size_t r, const int y0,
                      const int rh, const int text_w, const Theme &th) const;

        // scrollbar layout for the current geometry; empty rect when the
        // bar is not shown
        void scrollbar_rect(int *x0, int *y0, int *height, int *thumb_y, int *thumb_h) const;
        // maps local coordinates to rows, or invalid when outside the
        // row area
        [[nodiscard]] size_t row_at(const int x, const int y) const;

        static constexpr int scroll_step = 3;  // rows per wheel notch
        static constexpr int gutter = 8;       // scrollbar width

        // fixed cap for the row-image cache (batch J2)
        static constexpr size_t row_cache_budget = 32 * 1024;

        // one cached row image; the key is everything that can change the
        // rendered pixels (row index, selection, geometry, colors).
        // Wireframe rows bypass the cache (a cached face would blit
        // opaque over the bones), so the mode is not part of the key.
        struct row_cache_entry
        {
            size_t row;
            bool sel;
            int w;
            int h;
            core::Color fg;
            core::Color bg;
            zb::SharedPtr<core::Graphics> img;
        };

        // row image cache: filled by draw_at, cleared by every row
        // render-affecting setter (see the class doc)
        mutable std::vector<row_cache_entry> row_cache_;
        mutable size_t row_cache_bytes_ = 0;
        mutable long long rasterizations_ = 0;  // cache-miss counter (§8)

        [[nodiscard]] const row_cache_entry *find_row_cache(const size_t row, const bool sel,
                                                            const int w, const int h,
                                                            const core::Color &fg,
                                                            const core::Color &bg) const;
        void insert_row_cache(row_cache_entry &&e) const;
        void invalidate_row_cache() const
        {
            row_cache_.clear();
            row_cache_bytes_ = 0;
        }

        int row_height = 16;
        size_t visible = 4;
        size_t count = 0;
        size_t top = 0;
        size_t value = invalid;

        ItemText text_fn = nullptr;
        const void *text_arg = nullptr;
        std::shared_ptr<std::vector<std::string>> items_;  // static model

        // FI-06 state (all of it only read when height_fn != nullptr):
        // the prefix sums over the row heights (sums_[i] = the top of
        // row i, sums_[count] = the content total; count + 1 entries),
        // rebuilt by set_item_count / set_item_height, and the pixel
        // scroll offset they address
        ItemHeight height_fn = nullptr;
        const void *height_arg = nullptr;
        std::vector<int64_t> sums_;
        int64_t top_px = 0;

        bool dragging = false;  // a thumb drag is in flight
        int drag_grab = 0;      // press offset from the thumb top
    };
}