#pragma once

#include <vector>

#include "widget.hpp"

namespace zb::ui
{
    /*
     * Routes input events to the widget tree.
     *
     * A press picks the deepest widget under the pointer and claims it as
     * the pressed target (if it accepts the event). While held, the target
     * is tracked by the widget, not by the pointer: releasing anywhere
     * still delivers the release to it, and leaving its area cancels it
     * (a touch move cancels only after two consecutive off-target moves:
     * touch panels report single glitch samples, mouse moves are exact).
     *
     * Modal dialogs can be enforced with set_modal(): while set, events
     * only reach widgets inside the modal widget (see Dialog).
     *
     * Keyboard handling: Tab / down / right move the focus forward, up /
     * left move it backward, Enter / Space activate the focused widget.
     * A press on a focusable widget also grants it the focus.
     */
    class InputDispatcher
    {
    public:
        InputDispatcher() = default;

        /*
         * Routes one input event; returns true when the event changed the
         * widget tree (a press was claimed, a release delivered, a press
         * cancelled, the focus moved, a key activated a widget). A callers
         * (e.g. CanvasWindow::input) can use this to repaint only when
         * something actually changed -- moves within the press slop that
         * cross no widget boundary are the main no-op source.
         */
        bool dispatch(Widget &root, const input::input_event &ev);

        // clears the pressed target (e.g. on window focus loss)
        void reset()
        {
            // deliver the cancel like every other press-abandon path: a
            // Button must not keep its pressed visuals and a ListBox
            // must not keep an in-flight thumb drag
            if (pressed_target != nullptr)
            {
                pressed_target->on_cancel();
            }
            pressed_target = nullptr;
            press_touch_id = 0;
            touch_outside_count = 0;
        }

        /*
         * Cuts every state reference to a subtree that is about to be
         * removed from the tree (batch J3): an active press on it is
         * cancelled (on_cancel is delivered), a focus it holds is
         * released (set_focused(false)), and a modal it hosts is
         * dropped. Call this before Panel::remove_child /
         * FlexPanel::remove_child -- see the docs on those, the
         * coordinated entry is CanvasWindow::remove_from.
         */
        void evict(const Widget *w);

        // events only reach widgets inside this subtree (nullptr = no modal)
        void set_modal(Widget *m) { modal = m; }

        // moves the focus to the next/previous focusable widget in the tree;
        // returns false when the focus did not move
        bool focus_next(Widget &root, const bool forward);

        void clear_focus();
        [[nodiscard]] Widget *get_focus_target() const { return focus_target; }

    private:
        static bool is_press(const input::input_event &ev);
        static bool is_release(const input::input_event &ev);
        static bool is_move(const input::input_event &ev);
        static bool is_key(const input::input_event &ev);
        static bool is_wheel(const input::input_event &ev);

        Widget *pick_target(Widget &root, const int x, const int y) const;
        Widget *pick_target_internal(Widget &root, const int x, const int y) const;
        bool handle_key(Widget &root, const input::input_event &ev);

        static void collect_focusable(Widget &w, std::vector<Widget *> &out);
        void set_focus(Widget *w);

        Widget *pressed_target = nullptr;
        Widget *modal = nullptr;
        Widget *focus_target = nullptr;

        // where the current press started; a move only cancels the press
        // when the pointer left the target AND moved farther than slop
        // (touch panels jitter by a few pixels, which must not eat clicks)
        static constexpr int press_slop = 8;
        int press_x = 0;
        int press_y = 0;

        // consecutive off-target touch moves beyond slop; the press is
        // cancelled at 2 -- a single glitch sample (touch panels report
        // occasional readings far from the real position) must not eat
        // the click. Reset when a move re-picks the pressed target
        int touch_outside_count = 0;

        // the touch pointer that owns the active press; moves/ups from a
        // different touch_id are ignored (multi-touch data model, single
        // active press for now)
        int press_touch_id = 0;
    };
}
