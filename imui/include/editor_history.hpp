#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zb::ui
{
    /*
     * Bounded undo/redo history for the text editors (code-contract
     * §2 "Undo/redo"): a pure record of {text, caret, sel_anchor}
     * snapshots with the merge/cap/redo-clear policy and no widget
     * coupling. The editor captures a step before a user edit and
     * hands it to record() afterwards; undo()/redo() hand back a
     * restore point the editor applies.
     *
     * Policy: consecutive same-kind edits whose before-state equals
     * the previous edit's after-state merge into one step (a typing
     * run); anything else opens a new run. At most max_steps entries
     * stay on the undo stack (the oldest drops); a new run clears
     * the redo stack; undo/redo close the run, so an edit after a
     * restore never merges across it. A no-op edit (before ==
     * after) only closes the run.
     */
    class editor_history
    {
    public:
        static constexpr size_t max_steps = 5;

        // what kind of user edit produced a step (the merge rule is
        // per kind: a typing run merges, a paste never does)
        enum class kind : uint8_t
        {
            insert,
            backspace,
            del,
            paste,
            newline,
            other,
        };

        // one restore point: the full editor text state (the stored
        // UTF-8 bytes plus the caret/anchor byte offsets)
        struct step
        {
            std::string text;
            size_t caret = 0;
            size_t sel_anchor = 0;

            bool operator==(const step &o) const
            {
                return text == o.text && caret == o.caret &&
                       sel_anchor == o.sel_anchor;
            }
        };

        // records one user edit: `before` is the captured pre-edit
        // state, `after` the post-edit state
        void record(const kind k, const step &before, const step &after)
        {
            if (before == after)
            {
                run_open_ = false;  // a no-op breaks the run
                return;
            }
            if (!(run_open_ && k == run_kind_ && before == last_after_))
            {
                push_undo(before);
                redo_.clear();
                run_kind_ = k;
            }
            run_open_ = true;
            last_after_ = after;
        }

        // pops one restore point into `out`; false when the stack is
        // empty. The live state goes to the redo stack.
        bool undo(const step &live, step &out)
        {
            if (undo_.empty())
            {
                return false;
            }
            run_open_ = false;
            redo_.push_back(live);
            if (redo_.size() > max_steps)
            {
                redo_.erase(redo_.begin());
            }
            out = undo_.back();
            undo_.pop_back();
            return true;
        }

        // pops one redo point into `out`; false when the stack is
        // empty. The live state goes back onto the undo stack.
        bool redo(const step &live, step &out)
        {
            if (redo_.empty())
            {
                return false;
            }
            run_open_ = false;
            push_undo(live);
            out = redo_.back();
            redo_.pop_back();
            return true;
        }

        // the history lives for one focus session: cleared on focus
        // gain and loss
        void clear()
        {
            undo_.clear();
            redo_.clear();
            run_open_ = false;
        }

        [[nodiscard]] bool can_undo() const { return !undo_.empty(); }
        [[nodiscard]] bool can_redo() const { return !redo_.empty(); }

    private:
        void push_undo(const step &s)
        {
            if (undo_.size() == max_steps)
            {
                undo_.erase(undo_.begin());  // the oldest drops
            }
            undo_.push_back(s);
        }

        std::vector<step> undo_;
        std::vector<step> redo_;
        bool run_open_ = false;
        kind run_kind_ = kind::other;
        step last_after_;  // the state the last recorded edit landed on
    };
}  // namespace zb::ui
