#include "test.hpp"

#include "imui.hpp"

using namespace zb::ui;

namespace
{
    zb::input::input_event press_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    zb::input::input_event release_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_up;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    zb::input::input_event move_to(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_move;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    zb::input::input_event touch_press_at(const int x, const int y, const int id)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::touch_down;
        ev.x = x;
        ev.y = y;
        ev.touch_id = id;
        return ev;
    }

    zb::input::input_event touch_release_at(const int x, const int y, const int id)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::touch_up;
        ev.x = x;
        ev.y = y;
        ev.touch_id = id;
        return ev;
    }

    zb::input::input_event touch_move_to(const int x, const int y, const int id)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::touch_move;
        ev.x = x;
        ev.y = y;
        ev.touch_id = id;
        return ev;
    }

    zb::input::input_event key_down(const int key)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.key = key;
        return ev;
    }

    // a 100x100 root panel with a button at (10,10) size 20x20
    struct Tree
    {
        Panel root;
        Button *button = nullptr;

        Tree()
        {
            root.set_size(100, 100);
            auto b = std::make_unique<Button>();
            b->set_size(20, 20);
            b->set_position(10, 10);
            button = b.get();
            root.add_child(std::move(b));
        }
    };

    /*
     * A widget that captures the pointer while pressed (like a slider):
     * drags outside its area must reach it and never cancel the press.
     */
    struct CaptureProbe : public Widget
    {
        bool pressed = false;
        int moves_while_captured = 0;
        int cancelled = 0;
        bool reports_change_on_move = true;

        bool captures_pointer() const override { return true; }

        void on_cancel() override { ++cancelled; }

        bool on_input(const zb::input::input_event &ev) override
        {
            if (ev.type == zb::input::input_type::mouse_left_down ||
                ev.type == zb::input::input_type::touch_down)
            {
                pressed = true;
                return true;
            }
            if (ev.type == zb::input::input_type::mouse_left_up ||
                ev.type == zb::input::input_type::touch_up)
            {
                pressed = false;
                return true;
            }
            if (ev.type == zb::input::input_type::mouse_move ||
                ev.type == zb::input::input_type::touch_move)
            {
                ++moves_while_captured;
                return reports_change_on_move;
            }
            return false;
        }
    };

    /*
     * A focusable widget that consumes key_down (returns true) so the
     * dispatcher must not run the focus navigation for that key.
     */
    struct KeyProbe : public Widget
    {
        int keys_consumed = 0;
        bool consume = true;

        bool is_focusable() const override { return true; }

        bool on_input(const zb::input::input_event &ev) override
        {
            if (ev.type == zb::input::input_type::key_down)
            {
                ++keys_consumed;
                return consume;
            }
            return false;
        }
    };

    /*
     * Records characters (ev.ch); consumes them when asked so the
     * dispatch result can be asserted either way.
     */
    struct CharProbe : public Widget
    {
        int chars_received = 0;
        bool consume = true;

        bool is_focusable() const override { return true; }

        bool on_input(const zb::input::input_event &ev) override
        {
            if (ev.type == zb::input::input_type::key_down && ev.ch != 0)
            {
                ++chars_received;
                return consume;
            }
            return false;
        }
    };
}

int test_dispatch()
{
    // press + release on the button fires clicked
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.button->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, press_at(15, 15));
        EXPECT(t.button->get_state() == Button::state::pressed);
        d.dispatch(t.root, release_at(15, 15));
        EXPECT(t.button->get_state() == Button::state::normal);
        EXPECT(clicks == 1);
    }

    // press on empty area does nothing
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.button->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, press_at(50, 50));
        d.dispatch(t.root, release_at(50, 50));
        EXPECT(clicks == 0);
        EXPECT(t.button->get_state() == Button::state::normal);
    }

    // dragging out of the button while held cancels the press
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.button->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, press_at(15, 15));
        d.dispatch(t.root, move_to(80, 80));  // left the button
        EXPECT(t.button->get_state() == Button::state::normal);  // cancelled
        d.dispatch(t.root, release_at(80, 80));
        EXPECT(clicks == 0);
    }

    // a small drift outside the button does not cancel the press
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.button->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, press_at(28, 15));    // inside the button (10..29)
        d.dispatch(t.root, move_to(32, 15));     // 3px out, but 4px drift
        EXPECT(t.button->get_state() == Button::state::pressed);
        d.dispatch(t.root, release_at(32, 15));
        EXPECT(clicks == 1);
    }

    // release is delivered to the pressed widget even off its area
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.button->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, press_at(15, 15));
        d.dispatch(t.root, release_at(80, 80));  // released elsewhere
        EXPECT(clicks == 1);
    }

    // a second touch pointer never interferes with the active press:
    // its moves and releases are ignored (multi-touch data model)
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.button->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, touch_press_at(15, 15, 1));
        EXPECT(t.button->get_state() == Button::state::pressed);

        // finger 2 drags away from the button: must not cancel finger 1
        d.dispatch(t.root, touch_move_to(80, 80, 2));
        EXPECT(t.button->get_state() == Button::state::pressed);

        // finger 2 lifts: must not release finger 1's press
        d.dispatch(t.root, touch_release_at(80, 80, 2));
        EXPECT(t.button->get_state() == Button::state::pressed);
        EXPECT(clicks == 0);

        // finger 1 lifts: the click fires
        d.dispatch(t.root, touch_release_at(15, 15, 1));
        EXPECT(t.button->get_state() == Button::state::normal);
        EXPECT(clicks == 1);
    }

    // a move within slop by the same touch pointer does not cancel the press
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.button->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, touch_press_at(28, 15, 3));
        d.dispatch(t.root, touch_move_to(32, 15, 3));  // 3px out, 4px drift
        EXPECT(t.button->get_state() == Button::state::pressed);
        d.dispatch(t.root, touch_release_at(32, 15, 3));
        EXPECT(clicks == 1);
    }

    // right button does not press the button
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.button->clicked += [&clicks]() { ++clicks; };

        zb::input::input_event ev = press_at(15, 15);
        ev.type = zb::input::input_type::mouse_right_down;
        d.dispatch(t.root, ev);
        ev.type = zb::input::input_type::mouse_right_up;
        d.dispatch(t.root, ev);
        EXPECT(clicks == 0);
        EXPECT(t.button->get_state() == Button::state::normal);
    }

    // nested panels: the deepest widget under the pointer wins
    {
        Panel root;
        root.set_size(100, 100);

        auto outer = std::make_unique<Panel>();
        outer->set_size(50, 50);
        outer->set_position(10, 10);
        auto *pouter = outer.get();

        auto inner = std::make_unique<Panel>();
        inner->set_size(20, 20);
        inner->set_position(5, 5);
        auto *pinner = inner.get();

        auto b = std::make_unique<Button>();
        b->set_size(10, 10);
        auto *pbtn = b.get();
        inner->add_child(std::move(b));
        outer->add_child(std::move(inner));
        root.add_child(std::move(outer));

        InputDispatcher d;
        int clicks = 0;
        pbtn->clicked += [&clicks]() { ++clicks; };

        // button sits at (0,0) inside inner -> (15,15) in root coordinates,
        // spanning (15,15)..(24,24)
        d.dispatch(root, press_at(17, 17));
        d.dispatch(root, release_at(17, 17));
        EXPECT(clicks == 1);
        EXPECT(pbtn->is_descendant_of(&root));
        EXPECT(pbtn->is_descendant_of(pouter));
        EXPECT(pbtn->is_descendant_of(pinner));
        EXPECT(!pbtn->is_descendant_of(nullptr));
    }

    // reset() clears a stuck press
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.button->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, press_at(15, 15));
        d.reset();
        d.dispatch(t.root, release_at(15, 15));
        EXPECT(clicks == 0);
    }

    // modal dialog: set_modal() blocks everything outside the modal subtree
    {
        Panel root;
        root.set_size(100, 100);

        // outer button at (0,0), NOT covered by the dialog
        auto outer = std::make_unique<Button>();
        outer->set_size(20, 20);
        outer->set_position(0, 0);
        auto *pouter = outer.get();
        int outer_clicks = 0;
        pouter->clicked += [&outer_clicks]() { ++outer_clicks; };

        // dialog covers (30,30)..(69,69); its button sits inside
        auto dlg = std::make_unique<Dialog>();
        dlg->set_size(40, 40);
        dlg->set_position(30, 30);
        dlg->set_frame_size(40, 40);
        dlg->set_button_size(20, 18);
        dlg->open();
        auto &ok = dlg->add_button("OK");
        int ok_clicks = 0;
        ok.clicked += [&ok_clicks]() { ++ok_clicks; };
        auto *pdlg = dlg.get();

        root.add_child(std::move(outer));
        root.add_child(std::move(dlg));
        pdlg->layout();

        InputDispatcher d;

        // no modal: the outer button works (outside the dialog area)
        d.dispatch(root, press_at(10, 10));
        d.dispatch(root, release_at(10, 10));
        EXPECT(outer_clicks == 1);

        // no modal: the dialog's own button works
        // frame at (30,30); button inside frame at (8,14) -> (38,44) 20x18
        d.dispatch(root, press_at(45, 50));
        d.dispatch(root, release_at(45, 50));
        EXPECT(ok_clicks == 1);

        // modal: the outer button is blocked
        d.set_modal(pdlg);
        d.dispatch(root, press_at(10, 10));
        d.dispatch(root, release_at(10, 10));
        EXPECT(outer_clicks == 1);

        // modal: the dialog's own button still works
        d.dispatch(root, press_at(45, 50));
        d.dispatch(root, release_at(45, 50));
        EXPECT(ok_clicks == 2);

        // modal: clicking the dialog frame area (no button) is absorbed
        d.dispatch(root, press_at(35, 35));
        d.dispatch(root, release_at(35, 35));
        EXPECT(ok_clicks == 2);
        EXPECT(outer_clicks == 1);

        // modal + drag out: pressing the dialog button then leaving the
        // modal cancels the press
        d.dispatch(root, press_at(45, 50));
        d.dispatch(root, move_to(5, 5));  // outside the dialog
        EXPECT(ok.get_state() == Button::state::normal);
        d.dispatch(root, release_at(5, 5));
        EXPECT(ok_clicks == 2);  // no extra click

        // modal cleared: the outer button works again
        d.set_modal(nullptr);
        d.dispatch(root, press_at(10, 10));
        d.dispatch(root, release_at(10, 10));
        EXPECT(outer_clicks == 2);
    }

    // a closed dialog does not intercept clicks in its area
    {
        Panel root;
        root.set_size(100, 100);

        auto dlg = std::make_unique<Dialog>();
        dlg->set_size(40, 40);
        dlg->set_position(30, 30);
        dlg->set_frame_size(40, 40);
        dlg->set_button_size(20, 18);
        dlg->close();
        auto &ok = dlg->add_button("OK");
        int ok_clicks = 0;
        ok.clicked += [&ok_clicks]() { ++ok_clicks; };
        root.add_child(std::move(dlg));

        InputDispatcher d;
        d.dispatch(root, press_at(45, 50));
        d.dispatch(root, release_at(45, 50));
        EXPECT(ok_clicks == 0);  // closed dialog ignores clicks
    }

    // dispatch reports whether the event changed the tree (repaint gate)
    {
        Tree t;
        InputDispatcher d;

        // claimed press and delivered release change the tree
        EXPECT(d.dispatch(t.root, press_at(15, 15)));
        EXPECT(d.dispatch(t.root, release_at(15, 15)));

        // press outside any widget is a no-op
        EXPECT(!d.dispatch(t.root, press_at(90, 90)));
        EXPECT(!d.dispatch(t.root, release_at(90, 90)));

        // a held press + moves inside the slop change nothing
        EXPECT(d.dispatch(t.root, press_at(15, 15)));
        EXPECT(!d.dispatch(t.root, move_to(16, 16)));  // within slop
        EXPECT(d.dispatch(t.root, release_at(15, 15)));

        // a move far outside the widget cancels the press (a change)
        EXPECT(d.dispatch(t.root, press_at(15, 15)));
        EXPECT(d.dispatch(t.root, move_to(80, 80)));  // beyond slop
        EXPECT(!d.dispatch(t.root, release_at(80, 80)));  // no press left
        EXPECT(!d.dispatch(t.root, move_to(80, 80)));  // no press: no-op
    }

    // keyboard events report focus moves and activations
    {
        Tree t;
        InputDispatcher d;

        // no focusable? the tree still has none on the root panel
        // (button is focusable); tab moves focus -> a change
        EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::tab))));
        // a second tab on the single focusable wraps to itself: no change
        EXPECT(!d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::tab))));
        // activation fires (focus is on the button)
        EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::enter))));
    }

    // pointer capture: a widget that captures the pointer receives every
    // move while held, even far outside its area, and is never cancelled
    {
        Panel root;
        root.set_size(100, 100);

        auto probe = std::make_unique<CaptureProbe>();
        probe->set_size(20, 20);
        probe->set_position(10, 10);
        auto *pprobe = probe.get();
        root.add_child(std::move(probe));

        InputDispatcher d;

        // press claims the probe; drags beyond slop still reach it and the
        // press survives; the release fires anywhere
        EXPECT(d.dispatch(root, press_at(15, 15)));
        EXPECT(pprobe->pressed);
        EXPECT(d.dispatch(root, move_to(90, 90)));  // far outside
        EXPECT(pprobe->moves_while_captured == 1);
        EXPECT(pprobe->cancelled == 0);
        EXPECT(pprobe->pressed);
        d.dispatch(root, move_to(95, 5));
        EXPECT(pprobe->moves_while_captured == 2);
        EXPECT(d.dispatch(root, release_at(95, 5)));
        EXPECT(!pprobe->pressed);
        EXPECT(pprobe->cancelled == 0);

        // a move that does not change the widget reports no repaint
        EXPECT(d.dispatch(root, press_at(15, 15)));
        pprobe->reports_change_on_move = false;
        EXPECT(!d.dispatch(root, move_to(90, 90)));
        pprobe->reports_change_on_move = true;
        EXPECT(d.dispatch(root, release_at(90, 90)));
    }

    // non-capture widgets keep the slop-cancel rule (capture contrast)
    {
        Tree t;
        InputDispatcher d;

        d.dispatch(t.root, press_at(28, 15));
        EXPECT(t.button->get_state() == Button::state::pressed);
        d.dispatch(t.root, move_to(40, 15));  // 11px out: beyond slop
        EXPECT(t.button->get_state() == Button::state::normal);  // cancelled
    }

    // the focused widget consumes keys first; unconsumed keys fall back
    // to the focus navigation
    {
        Panel root;
        root.set_size(100, 100);

        auto probe = std::make_unique<KeyProbe>();
        probe->set_size(20, 20);
        probe->set_position(10, 10);
        auto *pprobe = probe.get();
        auto btn = std::make_unique<Button>();
        btn->set_size(20, 20);
        btn->set_position(50, 50);
        auto *pbtn = btn.get();
        root.add_child(std::move(probe));
        root.add_child(std::move(btn));

        InputDispatcher d;

        // first tab focuses the probe (no focus yet: nothing to consume)
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pprobe);

        // the probe consumes the tab: focus must not move
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(pprobe->keys_consumed == 1);
        EXPECT(d.get_focus_target() == pprobe);

        // a probe that does not consume the key: navigation resumes
        pprobe->consume = false;
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(pprobe->keys_consumed == 2);  // saw the key, did not consume
        EXPECT(d.get_focus_target() == pbtn);
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pprobe);

        // activations only fire when the key is not consumed
        int clicks = 0;
        pbtn->clicked += [&clicks]() { ++clicks; };
        // walk the focus to the button with an unconsumed tab
        pprobe->consume = false;
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));  // probe -> button
        EXPECT(d.get_focus_target() == pbtn);
        // enter reaches the button (it does not consume keys): click fires
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::enter))));
        EXPECT(clicks == 1);
        // a consuming focused widget would have eaten the enter: no click
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));  // button -> probe
        pprobe->consume = true;
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::enter))));  // probe eats it
        EXPECT(clicks == 1);
    }

    // characters are routed to the focused widget and never fall back
    // to navigation when unconsumed (B1)
    {
        Panel root;
        root.set_size(100, 100);

        auto probe = std::make_unique<CharProbe>();
        probe->set_size(20, 20);
        probe->set_position(10, 10);
        auto *pprobe = probe.get();
        auto btn = std::make_unique<Button>();
        btn->set_size(20, 20);
        btn->set_position(50, 50);
        auto *pbtn = btn.get();
        root.add_child(std::move(probe));
        root.add_child(std::move(btn));

        InputDispatcher d;
        auto ch_ev = [](const int ch) {
            zb::input::input_event ev;
            ev.type = zb::input::input_type::key_down;
            ev.ch = ch;
            return ev;
        };

        // no focus: a character is dropped, nothing navigates
        EXPECT(!d.dispatch(root, ch_ev(static_cast<int>('a'))));
        EXPECT(pprobe->chars_received == 0);

        // focus the probe, then a character reaches it
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pprobe);
        EXPECT(d.dispatch(root, ch_ev(static_cast<int>('a'))));
        EXPECT(pprobe->chars_received == 1);

        // an unconsumed character is dropped: no navigation, no activation
        pprobe->consume = false;
        EXPECT(!d.dispatch(root, ch_ev(static_cast<int>('b'))));
        EXPECT(pprobe->chars_received == 2);
        EXPECT(d.get_focus_target() == pprobe);  // focus stayed put
        EXPECT(pbtn->get_state() == Button::state::normal);

        // a tab with ch == 0 still navigates (regression)
        pprobe->consume = false;
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pbtn);
    }

    // modal: keyboard focus and navigation stay inside the modal subtree
    // (F5); a focus target left outside the dialog is released
    {
        Panel root;
        root.set_size(100, 100);
        auto a = std::make_unique<Button>();
        a->set_size(20, 20);
        a->set_position(5, 5);
        auto *pa = a.get();
        int a_clicks = 0;
        pa->clicked += [&a_clicks]() { ++a_clicks; };
        root.add_child(std::move(a));

        auto dlg = std::make_unique<Panel>();
        dlg->set_size(50, 50);
        dlg->set_position(40, 40);
        auto b = std::make_unique<Button>();
        b->set_size(20, 20);
        b->set_position(5, 5);  // absolute (45, 45)
        auto *pb = b.get();
        int b_clicks = 0;
        pb->clicked += [&b_clicks]() { ++b_clicks; };
        dlg->add_child(std::move(b));
        auto *pdlg = dlg.get();
        root.add_child(std::move(dlg));

        InputDispatcher d;

        // without modal, tab reaches the background button and enter
        // activates it
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pa);
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::enter))));
        EXPECT(a_clicks == 1);

        // modal open: the outside focus is released, tab lands inside
        // the dialog, enter activates the dialog button only
        d.set_modal(pdlg);
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pb);
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::enter))));
        EXPECT(b_clicks == 1);
        EXPECT(a_clicks == 1);

        // modal released: whole-tree navigation works again
        d.set_modal(nullptr);
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pa);
    }

    // a focused widget hidden after the focus was set (its holder is
    // hidden; its own flag stays true) does not consume keys or
    // characters (F6): the stale focus is released on the next key event
    {
        Panel root;
        root.set_size(100, 100);
        auto a = std::make_unique<Button>();
        a->set_size(20, 20);
        a->set_position(5, 5);
        auto *pa = a.get();
        root.add_child(std::move(a));

        auto holder = std::make_unique<Panel>();
        holder->set_size(30, 30);
        holder->set_position(50, 50);
        auto probe = std::make_unique<CharProbe>();
        probe->set_size(20, 20);
        probe->set_position(5, 5);
        auto *pp = probe.get();
        holder->add_child(std::move(probe));
        auto *ph = holder.get();
        root.add_child(std::move(holder));

        InputDispatcher d;
        // focus the probe by tab (CharProbe does not claim presses);
        // navigation order: a, then the probe
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pa);
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pp);

        ph->set_visible(false);

        zb::input::input_event cev;
        cev.type = zb::input::input_type::key_down;
        cev.ch = 'x';
        EXPECT(!d.dispatch(root, cev));  // not consumed by the hidden probe
        EXPECT(pp->chars_received == 0);
        EXPECT(d.get_focus_target() == nullptr);  // stale focus released

        // tab navigates the visible tree (the hidden holder is skipped)
        EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
        EXPECT(d.get_focus_target() == pa);
    }

    // a press claimed on a widget that is then hidden is cancelled: no
    // further moves or releases reach it (F6)
    {
        Panel root;
        root.set_size(100, 100);
        auto holder = std::make_unique<Panel>();
        holder->set_size(30, 30);
        holder->set_position(10, 10);
        auto cp = std::make_unique<CaptureProbe>();
        cp->set_size(20, 20);
        cp->set_position(5, 5);
        auto *pcp = cp.get();
        holder->add_child(std::move(cp));
        auto *ph = holder.get();
        root.add_child(std::move(holder));

        InputDispatcher d;
        EXPECT(d.dispatch(root, press_at(15, 15)));
        EXPECT(pcp->pressed);

        ph->set_visible(false);
        EXPECT(!d.dispatch(root, move_to(20, 20)));  // not fed to the probe
        EXPECT(pcp->cancelled == 1);                 // cancelled instead
        EXPECT(!d.dispatch(root, release_at(20, 20)));
    }

    return test::report("dispatch");
}
