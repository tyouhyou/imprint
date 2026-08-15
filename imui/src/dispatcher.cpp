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
        switch (static_cast<input::key_code>(ev.key))
        {
        case input::key_code::tab:
        case input::key_code::down:
        case input::key_code::right:
            return focus_next(root, true);
        case input::key_code::up:
        case input::key_code::left:
            return focus_next(root, false);
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
        if (is_key(ev))
        {
            return handle_key(root, ev);
        }

        if (is_wheel(ev))
        {
            // wheel events go to the widget under the pointer; a widget
            // that does not claim them leaves the event unhandled
            if (auto *t = pick_target(root, ev.x, ev.y))
            {
                if (t->on_input(ev))
                {
                    LD << "wheel claimed by widget";
                    return true;
                }
            }
            return false;
        }

        if (is_press(ev))
        {
            bool changed = false;
            if (pressed_target != nullptr)
            {
                // a previous press was never released; cancel it first
                pressed_target->on_cancel();
                pressed_target = nullptr;
                changed = true;
            }

            auto *t = pick_target(root, ev.x, ev.y);
            if (t != nullptr && t->on_input(ev))
            {
                pressed_target = t;
                press_x = ev.x;
                press_y = ev.y;
                press_touch_id = ev.touch_id;
                LD << "press claimed at " << ev.x << "," << ev.y;
                if (t->is_focusable())
                {
                    set_focus(t);
                }
                return true;
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
                // must not eat a click
                if (pick_target(root, ev.x, ev.y) != pressed_target)
                {
                    const int dx = ev.x - press_x;
                    const int dy = ev.y - press_y;
                    if (dx * dx + dy * dy > press_slop * press_slop)
                    {
                        LD << "press cancelled by move to " << ev.x << "," << ev.y;
                        pressed_target->on_cancel();
                        pressed_target = nullptr;
                        return true;
                    }
                }
            }
            return false;
        }

        return false;
    }
}
