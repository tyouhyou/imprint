#include "dispatcher.hpp"

#include "logging.hpp"

namespace zb::ui
{
    bool InputDispatcher::is_press(const input::input_event &ev)
    {
        return ev.type == input::input_type::mouse_left_down ||
               ev.type == input::input_type::touch_down;
    }

    bool InputDispatcher::is_release(const input::input_event &ev)
    {
        return ev.type == input::input_type::mouse_left_up ||
               ev.type == input::input_type::touch_up;
    }

    bool InputDispatcher::is_move(const input::input_event &ev)
    {
        return ev.type == input::input_type::mouse_move ||
               ev.type == input::input_type::touch_move;
    }

    bool InputDispatcher::is_key(const input::input_event &ev)
    {
        return ev.type == input::input_type::key_down ||
               ev.type == input::input_type::key_up;
    }

    bool InputDispatcher::is_wheel(const input::input_event &ev)
    {
        return ev.type == input::input_type::mouse_wheel;
    }

    void InputDispatcher::collect_focusable(Widget &w, std::vector<Widget *> &out)
    {
        // hidden widgets (e.g. buttons of a closed dialog) must not be
        // reached by keyboard focus
        if (!w.is_visible())
        {
            return;
        }
        if (w.is_focusable())
        {
            out.push_back(&w);
        }
        for (size_t i = 0; i < w.child_count(); ++i)
        {
            if (auto *c = w.child_at(i))
            {
                collect_focusable(*c, out);
            }
        }
    }

    void InputDispatcher::set_focus(Widget *w)
    {
        if (focus_target != nullptr)
        {
            focus_target->set_focused(false);
        }
        focus_target = w;
        if (focus_target != nullptr)
        {
            focus_target->set_focused(true);
        }
    }

    void InputDispatcher::clear_focus()
    {
        set_focus(nullptr);
    }

    void InputDispatcher::evict(const Widget *w)
    {
        if (w == nullptr)
        {
            return;
        }
        // an active press on the subtree: deliver the cancel like the
        // dispatcher state machine would, then drop the pointer
        if (pressed_target != nullptr &&
            (pressed_target == w || pressed_target->is_descendant_of(w)))
        {
            pressed_target->on_cancel();
            pressed_target = nullptr;
        }
        // the focus target is a single widget: either the removed widget
        // itself or one of its descendants
        if (focus_target != nullptr &&
            (focus_target == w || focus_target->is_descendant_of(w)))
        {
            set_focus(nullptr);
        }
        if (modal != nullptr && (modal == w || modal->is_descendant_of(w)))
        {
            modal = nullptr;
        }
        // a claim or wheel-bubble hop in flight across the user callback:
        // the dispatcher dereferences the widget again right after its
        // on_input() returns, so the eviction must reach it too
        if (in_flight != nullptr &&
            (in_flight == w || in_flight->is_descendant_of(w)))
        {
            in_flight = nullptr;
        }
    }

    bool InputDispatcher::focus_next(Widget &root, const bool forward)
    {
        std::vector<Widget *> list;
        collect_focusable(root, list);
        if (list.empty())
        {
            return false;
        }

        int idx = -1;
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i] == focus_target)
            {
                idx = static_cast<int>(i);
                break;
            }
        }

        if (idx < 0)
        {
            idx = forward ? 0 : static_cast<int>(list.size()) - 1;
        }
        else if (forward)
        {
            idx = (idx + 1) % static_cast<int>(list.size());
        }
        else
        {
            idx = (idx - 1 + static_cast<int>(list.size())) % static_cast<int>(list.size());
        }

        if (list[static_cast<size_t>(idx)] == focus_target)
        {
            return false;  // already there (single focusable widget)
        }
        set_focus(list[static_cast<size_t>(idx)]);
        return true;
    }

    bool InputDispatcher::handle_key(Widget &root, const input::input_event &ev)
    {
        if (ev.type != input::input_type::key_down)
        {
            return false;
        }
        // a focused widget hidden after the focus was set (e.g. its
        // dialog closed) must not consume keys: release the stale focus,
        // navigation continues on the visible tree
        if (focus_target != nullptr && !focus_target->is_effectively_visible())
        {
            set_focus(nullptr);
        }
        // modal: keys only reach the modal subtree; a focus target left
        // outside (focused before the modal opened) is released
        if (modal != nullptr && focus_target != nullptr &&
            !focus_target->is_descendant_of(modal))
        {
            set_focus(nullptr);
        }
        // the focused widget consumes the key first (slider arrows now,
        // TextInput characters later); an unconsumed key falls back to
        // the navigation handling below
        if (focus_target != nullptr && focus_target->on_input(ev))
        {
            LD << "key consumed by focused widget (" << ev.key << ")";
            return true;
        }
        // characters never navigate: unconsumed text is dropped (B1)
        if (ev.ch != 0)
        {
            return false;
        }
        // navigation is confined to the modal subtree while a modal is
        // open (Tab must not walk out of the dialog)
        Widget &scope = modal != nullptr ? *modal : root;
        switch (static_cast<input::key_code>(ev.key))
        {
        case input::key_code::tab:
        case input::key_code::down:
        case input::key_code::right:
            return focus_next(scope, true);
        case input::key_code::up:
        case input::key_code::left:
            return focus_next(scope, false);
        case input::key_code::enter:
        case input::key_code::space:
            if (focus_target != nullptr)
            {
                focus_target->on_activate();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    Widget *InputDispatcher::pick_target(Widget &root, const int x, const int y) const
    {
        if (modal != nullptr)
        {
            // modal: only a widget inside the modal subtree can be hit
            if (auto *t = pick_target_internal(root, x, y))
            {
                if (t->is_descendant_of(modal))
                {
                    return t;
                }
            }
            return nullptr;
        }
        return pick_target_internal(root, x, y);
    }

    Widget *InputDispatcher::pick_target_internal(Widget &root, const int x, const int y) const
    {
        if (!root.hit(x, y))
        {
            return nullptr;
        }
        if (auto *inner = root.pick(x, y))
        {
            return inner;
        }
        return &root;
    }

    bool InputDispatcher::dispatch(Widget &root, const input::input_event &ev)
    {
        // defensive liveness (A-14): a cached widget that left the tree
        // without the evict handshake must not be dereferenced by later
        // events. The descendant probe catches removed-but-alive widgets
        // (their parent chain no longer reaches the root); a DESTROYED
        // widget is still the caller's contract breach -- removal goes
        // through CanvasWindow::remove_from, which evicts first
        if (pressed_target != nullptr && !pressed_target->is_descendant_of(&root))
        {
            LD << "press dropped: target left the tree";
            pressed_target = nullptr;
        }
        if (focus_target != nullptr && !focus_target->is_descendant_of(&root))
        {
            set_focus(nullptr);
        }
        if (modal != nullptr && !modal->is_descendant_of(&root))
        {
            modal = nullptr;
        }

        if (is_key(ev))
        {
            return handle_key(root, ev);
        }

        if (is_wheel(ev))
        {
            // wheel events go to the widget under the pointer and BUBBLE
            // up the ancestor chain (H-4): an unscrolled inner container
            // lets an outer scroller take the notch, the ListBox-inside-
            // ScrollPanel shape works, and a fully-consumed wheel still
            // stops at the first claimer
            if (auto *t = pick_target(root, ev.x, ev.y))
            {
                for (Widget *w = t; w != nullptr;)
                {
                    // the hop is read BEFORE the handler: a handler may
                    // remove (or destroy) w mid-callback via the §6
                    // tree-mutation protocol
                    Widget *next = w->parent;
                    in_flight = w;
                    const bool claimed = w->on_input(ev);
                    const bool evicted = in_flight == nullptr;
                    in_flight = nullptr;
                    if (claimed)
                    {
                        LD << "wheel claimed by widget";
                        return true;
                    }
                    if (evicted)
                    {
                        // w (or one of its ancestors) left the tree inside
                        // its own handler: the captured chain is stale
                        LD << "wheel bubble stopped: widget left the tree";
                        return false;
                    }
                    // the bubble is confined to an open modal subtree
                    // (inclusive), mirroring the key path above: an
                    // ancestor above the dialog (e.g. a page scroller
                    // hosting the overlay) must not take the notch
                    if (w == modal)
                    {
                        break;
                    }
                    w = next;
                }
            }
            return false;
        }

        // a pressed widget hidden mid-press (e.g. its dialog closed)
        // must not receive further moves/releases: cancel it and let
        // the event flow on (a following press claims a fresh target)
        if (pressed_target != nullptr && !pressed_target->is_effectively_visible())
        {
            LD << "press cancelled: target not effectively visible";
            pressed_target->on_cancel();
            pressed_target = nullptr;
        }
        // modal confinement covers the whole press lifecycle: a press
        // claimed BEFORE the modal opened must not receive moves or
        // releases behind the dialog (the classic click-through) --
        // cancel it like a mid-press hide
        if (pressed_target != nullptr && modal != nullptr &&
            !pressed_target->is_descendant_of(modal))
        {
            LD << "press cancelled: target outside the open modal";
            pressed_target->on_cancel();
            pressed_target = nullptr;
        }

        if (is_press(ev))
        {
            bool changed = false;
            if (pressed_target != nullptr)
            {
                // a previous press was never released; cancel it first
                LD << "press cancelled: superseded by a new press";
                pressed_target->on_cancel();
                pressed_target = nullptr;
                changed = true;
            }

            auto *t = pick_target(root, ev.x, ev.y);
            if (t != nullptr)
            {
                // claim handshake: evict() can reach the widget being
                // claimed across its own user callback (a handler that
                // removes its own subtree via CanvasWindow::remove_from)
                in_flight = t;
                const bool claimed = t->on_input(ev);
                const bool evicted = in_flight == nullptr;
                in_flight = nullptr;
                if (claimed)
                {
                    if (!evicted)
                    {
                        pressed_target = t;
                        press_x = ev.x;
                        press_y = ev.y;
                        press_touch_id = ev.touch_id;
                        touch_outside_count = 0;
                        if (t->is_focusable())
                        {
                            set_focus(t);
                        }
                    }
                    LD << "press claimed at " << ev.x << "," << ev.y;
                    return true;
                }
            }
            LD << "press NOT claimed at " << ev.x << "," << ev.y;
            return changed;
        }

        if (is_release(ev))
        {
            if (pressed_target != nullptr && ev.touch_id == press_touch_id)
            {
                LD << "release on pressed target";
                pressed_target->on_input(ev);
                pressed_target = nullptr;
                return true;
            }
            if (pressed_target != nullptr)
            {
                // a release from another pointer: ignored (one active
                // press; the data model stays multi-touch)
                LD << "release from touch_id " << ev.touch_id
                   << " ignored (press held by touch_id " << press_touch_id
                   << ")";
                return false;
            }
            LD << "release with no pressed target";
            return false;
        }

        if (is_move(ev))
        {
            if (pressed_target != nullptr && ev.touch_id == press_touch_id)
            {
                // drag semantics: a widget that captures the pointer
                // (e.g. a slider) receives every move while held; the
                // slop rule does not apply to it
                if (pressed_target->captures_pointer())
                {
                    if (pressed_target->on_input(ev))
                    {
                        LD << "move delivered to captured pointer at " << ev.x << "," << ev.y;
                        return true;
                    }
                    return false;
                }
                // the pointer left the pressed widget: cancel the press,
                // but tolerate a few pixels of drift (touch jitter) that
                // must not eat a click. A move over the pressed widget's
                // own subtree is still ON target: the deepest pick returns
                // the child, but a container that consumed the press owns
                // its children's area (contract: cancel when the pointer
                // left the target, not when it entered a descendant)
                const int dx = ev.x - press_x;
                const int dy = ev.y - press_y;
                // int64 squares: dx/dy come straight from the C-ABI event
                // (untrusted int coordinates), dx*dx overflows int
                const bool beyond_slop =
                    static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy >
                    static_cast<int64_t>(press_slop) * press_slop;
                Widget *picked = pick_target(root, ev.x, ev.y);
                // a nullptr pick (off-window, or a modal miss) is simply
                // off target — do not dereference it
                const bool on_target = picked != nullptr &&
                                       (picked == pressed_target ||
                                        picked->is_descendant_of(pressed_target));
                if (!on_target && beyond_slop)
                {
                    // touch panels occasionally report one glitch
                    // sample far from the real position; a single
                    // off-target touch move must not eat the press --
                    // the cancel needs two consecutive off-target
                    // moves (mouse moves are exact and cancel at once).
                    // Any sample that is not a strike (back on the
                    // widget, or still within the slop) resets the
                    // counter so the strikes are truly consecutive.
                    if (ev.type == input::input_type::touch_move &&
                        ++touch_outside_count < 2)
                    {
                        LD << "off-target touch move tolerated at " << ev.x << "," << ev.y;
                        return false;
                    }
                    LD << "press cancelled by move to " << ev.x << "," << ev.y;
                    pressed_target->on_cancel();
                    pressed_target = nullptr;
                    touch_outside_count = 0;
                    return true;
                }
                touch_outside_count = 0;
            }
            return false;
        }

        return false;
    }
}
