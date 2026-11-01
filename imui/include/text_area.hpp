#pragma once

#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Multi-line text editor (T-4, code-contract §2 "Multi-line editor").
     * Wrap is always on at the assigned box width, '\n' in the text is a
     * hard break and enter inserts one. There is NO submitted event —
     * submit affordances are app-side (a button); `changed` fires with
     * the whole UTF-8 text after every edit, exactly like TextInput.
     *
     * The caret is a u16 index (clamped on every edit, like TextInput).
     * Up/down move between the lines wrap_spans reports, keeping a pixel
     * column goal across lines; click placement maps (x, y) through the
     * spans. Horizontal overflow within a line clips — there is no
     * horizontal scrolling.
     *
     * The view holds an internal vertical scroll offset: any caret move
     * scrolls minimally so the caret's line stays visible; mouse wheel
     * deltas scroll without moving the caret (delta > 0 scrolls toward
     * the top, the ScrollPanel convention). Focus ring as TextInput. No
     * undo/redo and no selection in this batch (caret only); both are
     * additive later without a contract change.
     */
    class TextArea : public Widget
    {
    public:
        TextArea();

        // fired on every edit, with the whole text (UTF-8)
        zb::event::Event<std::string> changed;

        void set_border_color(const core::Color &c)
        {
            border_color = c;
            mark_dirty();
        }
        void set_caret_color(const core::Color &c)
        {
            caret_color = c;
            mark_dirty();
        }
        void set_text_gap(const int g)
        {
            text_gap = g;
            mark_dirty();
        }

        // natural size: 100 wide, three lines plus the border
        [[nodiscard]] core::imsize_t measure() const override;

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        bool is_focusable() const override { return true; }

    private:
        // the editing core mirrors TextInput's (a u16 caret clamped on
        // every edit); enter inserts '\n' instead of submitting
        void clamp_caret();
        void insert(const char16_t ch);
        bool erase_before_caret();
        bool erase_at_caret();
        bool move_caret(const int dir);
        bool move_caret_vertical(const int dir);
        void place_caret_at(const int x, const int y);
        void fire_changed();

        // the pitch, inner wrap width, and caret geometry shared by the
        // input and draw paths — one definition so a click, a caret
        // move, and the paint never disagree about the line map
        [[nodiscard]] int line_pitch() const;
        [[nodiscard]] int inner_width() const;
        // the line holding the caret (a caret on a line boundary sits at
        // the END of the earlier line) as an index into `spans`
        [[nodiscard]] int caret_line(
            const std::vector<std::pair<int, int>> &spans) const;
        // the unit index within [0, len] whose caret column best matches
        // the pixel column `col` (past the glyph's midpoint the caret
        // goes after it — the TextInput click rule)
        [[nodiscard]] size_t column_to_caret(const char16_t *data,
                                             const int len,
                                             const int col) const;
        [[nodiscard]] int caret_pixel_column() const;
        // after any caret move: scroll minimally so the caret's line is
        // visible; wheel scrolling clamps to the block instead
        void scroll_caret_into_view();
        void clamp_scroll();

        size_t caret = 0;
        // pixel column kept across vertical moves; every non-vertical
        // caret change re-anchors it to the caret's own column
        int column_goal = 0;
        int scroll_y = 0;  // vertical viewport offset in px
        int text_gap = 2;
        // unset = follow the active theme (contract 10.3)
        std::optional<core::Color> border_color;  // override of theme border
        std::optional<core::Color> caret_color;   // override of theme accent
    };
}  // namespace zb::ui
