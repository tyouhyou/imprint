#pragma once

#include "editor_history.hpp"
#include "event.hpp"
#include "widget.hpp"

namespace zb::ui
{
    /*
     * Single-line text editor. Accepts printable ASCII plus any code
     * point >= U+00A0 (docs/code-contract.md §2 "Text input scope");
     * printable characters insert at the caret, backspace deletes one
     * whole code point before it, del deletes one after it, left/right
     * move the caret, enter submits. The caret is a static block (no
     * timer, no blink animation).
     *
     * The text lives in Widget::text_ (UTF-8 bytes, code-contract
     * §2.1): set_text() is inherited and works through a Widget
     * reference (builder materialization included). The caret is a
     * byte offset into it, always on a code-point boundary, clamped
     * to the current length at every edit and at draw; a programmatic
     * set_text shorter than the caret therefore lands the caret at the
     * new end (it never resets to the start).
     *
     * `changed` fires with the UTF-8 text after every edit; `submitted`
     * fires with the UTF-8 text on enter.
     *
     * Selection (FI-02, code-contract §3.8), the single-line mirror of
     * TextArea: drag and shift+arrow select, Ctrl+A selects all, typing
     * replaces the selection, and the range draws with the inverted
     * face. Ctrl+C / Ctrl+Insert copy, Ctrl+V / Shift+Insert paste at
     * the caret — through the process-wide ClipboardSink (clipboard.hpp).
     *
     * Read-only (set_read_only, code-contract §2): user edits stop at
     * the edit primitives — typing/IME ch, space, backspace, delete
     * and paste are silent no-ops and `changed` stays quiet; caret
     * motion, selection, copy and enter-submitted behave as before,
     * and a programmatic set_text still applies.
     *
     * Undo/redo (code-contract §2 "Undo/redo"): every user edit
     * records one history step; consecutive same-kind edits merge
     * (a typing run, a backspace run). Ctrl+Z undoes, Ctrl+Y /
     * Ctrl+Shift+Z redo; the stacks hold at most 5 steps and a new
     * edit clears the redo side. The history lives for one focus
     * session — cleared on focus gain and loss — so a programmatic
     * set_text is never undoable across focus. undo()/redo() are
     * public (can_undo()/can_redo() report the stacks); read-only
     * gates them like every other edit primitive.
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
        void set_read_only(const bool on)
        {
            if (read_only_ == on)
            {
                return;
            }
            read_only_ = on;
            mark_dirty();
        }
        [[nodiscard]] bool is_read_only() const { return read_only_; }

        // bounded undo/redo (code-contract §2 "Undo/redo"); a call
        // with an empty stack is a silent no-op
        void undo();
        void redo();
        [[nodiscard]] bool can_undo() const { return history_.can_undo(); }
        [[nodiscard]] bool can_redo() const { return history_.can_redo(); }

        // the current selection as UTF-8 (empty when collapsed)
        [[nodiscard]] std::string selected_text() const;

        // natural size: 100 wide, one line plus the border
        [[nodiscard]] core::imsize_t measure() const override;

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        bool is_focusable() const override { return true; }
        // a selection drag owns the pointer until release (the slider
        // pattern) so moves keep extending the selection off-widget
        [[nodiscard]] bool captures_pointer() const override
        {
            return selecting_;
        }
        void on_cancel() override { selecting_ = false; }
        void on_focus_change(const bool focused) override;

    private:
        void clamp_caret();
        void insert(const char32_t cp);
        bool erase_before_caret();
        bool erase_at_caret();
        bool move_caret(const int dir, const bool extend_selection = false);
        void place_caret_at(const int x);
        void fire_changed();

        // FI-02 selection mechanics (see TextArea): erase_selection
        // replaces the range (`fire` controls the changed event —
        // insert paths erase silently so one user edit fires once);
        // insert_text inserts a whole string (paste)
        bool erase_selection(bool fire = true);
        void insert_text(const std::string &units);
        // the field's own input rules applied to pasted text: the T-2
        // acceptance, no line break (single line)
        [[nodiscard]] std::string paste_filter(const std::string &in) const;
        void copy_selection();
        void paste_at_caret();

        // §2 "Undo/redo" bookkeeping: the history owns the stacks and
        // the merge policy; the editor captures a state before each
        // user edit and applies what undo/redo hand back
        using history_step = editor_history::step;
        [[nodiscard]] history_step capture() const;
        void record_edit(editor_history::kind k, const history_step &before);
        void apply_state(const history_step &s);

        size_t caret = 0;
        // the selection anchor: [min(sel_anchor, caret), max(...));
        // collapsed when it equals the caret
        size_t sel_anchor = 0;
        bool selecting_ = false;  // a selection drag is in flight
        bool read_only_ = false;  // code-contract §2 "Read-only mode"
        editor_history history_;
        int text_gap = 2;
        // unset = follow the active theme (contract 10.3)
        std::optional<core::Color> border_color;  // override of theme border
        std::optional<core::Color> caret_color;   // override of theme accent
    };
}  // namespace zb::ui