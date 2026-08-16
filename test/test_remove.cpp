#include "test.hpp"

#include <memory>
#include <string>

#include "imapp.hpp"
#include "imui.hpp"

using namespace zb::ui;
using namespace zb::app;

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

    zb::input::input_event key_down(const int key)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.key = key;
        return ev;
    }

    // a pressable, focusable leaf for the mutation scenarios
    Button *mk_button(Panel &root, const char *id, const int x, const int y)
    {
        auto b = std::make_unique<Button>();
        b->set_id(id);
        b->set_position(x, y);
        b->set_size(60, 30);
        auto *p = b.get();
        root.add_child(std::move(b));
        return p;
    }

    // removes the widget the direct way: caller does the eviction first
    std::unique_ptr<Widget> remove_direct(InputDispatcher &d, Panel &root, Widget *w)
    {
        d.evict(w);
        return root.remove_child(w);
    }

    // widget that holds a Subscription; unsubscribes when destroyed
    struct Subscriber : Widget
    {
        int calls = 0;
        zb::event::Subscription<> sub;

        explicit Subscriber(zb::event::Event<> &ev)
            : sub(ev.subscribe([this]() { ++calls; }))
        {
        }
    };

    // removes the widget the coordinated way (CanvasWindow::remove_from)
    std::unique_ptr<Widget> remove_coordinated(CanvasWindow &w, Panel &root, Widget *child)
    {
        return w.remove_from(root, child);
    }
}  // namespace

int test_remove()
{
        // removing the pressed widget: the press is cancelled, a release
        // issued afterwards is inert (no events, no crash)
        {
            // path A: coordinated through the canvas window
            CanvasWindow w;
            w.create(200, 150);
            auto &root = w.root();
            int clicks = 0;
            auto *b = mk_button(root, "pressed", 10, 10);
            b->clicked += [&]() { ++clicks; };

                    w.input(press_at(30, 20));  // claimed, focused
                    w.input(release_at(30, 20));
            EXPECT(clicks == 1);  // a full press on the live button clicks

                    w.input(press_at(30, 20));  // claimed again
            auto out = remove_coordinated(w, root, b);
            EXPECT(out != nullptr);
            EXPECT(!out->is_descendant_of(&root));

                    w.input(release_at(30, 20));  // inert, no UAF
            EXPECT(clicks == 1);
            out.reset();
        }
        {
            // path B: direct panel access with a manual evict
            Panel root;
            root.set_size(200, 150);
            InputDispatcher d;
            int clicks = 0;
            auto *b = mk_button(root, "pressed", 10, 10);
            b->clicked += [&]() { ++clicks; };

            EXPECT(d.dispatch(root, press_at(30, 20)));
            auto out = remove_direct(d, root, b);
            EXPECT(out != nullptr);

            EXPECT(!d.dispatch(root, release_at(30, 20)));
            EXPECT(clicks == 0);
            out.reset();
        }

        // removing the focused widget: focus is released, Tab lands on
        // the next valid focusable
        {
            // path A: coordinated
            CanvasWindow w;
            w.create(200, 150);
            auto &root = w.root();
            auto *b1 = mk_button(root, "one", 10, 10);
            auto *b2 = mk_button(root, "two", 80, 10);
            auto *b3 = mk_button(root, "three", 150, 10);

            w.input(press_at(30, 20));  // focus b1
            auto out = w.remove_from(root, b1);
            EXPECT(out != nullptr);

            w.input(key_down(static_cast<int>(zb::input::key_code::tab)));
            EXPECT(b2->is_focused());
            w.input(key_down(static_cast<int>(zb::input::key_code::tab)));
            EXPECT(b3->is_focused());
            out.reset();
        }
        {
            // path B: direct panel access with a manual evict
            Panel root;
            root.set_size(200, 150);
            InputDispatcher d;
            auto *b1 = mk_button(root, "one", 10, 10);
            auto *b2 = mk_button(root, "two", 80, 10);
            auto *b3 = mk_button(root, "three", 150, 10);

            EXPECT(d.dispatch(root, press_at(30, 20)));  // focus b1
            auto out = remove_direct(d, root, b1);
            EXPECT(out != nullptr);

            EXPECT(d.dispatch(root, key_down(static_cast<int>(zb::input::key_code::tab))));
            EXPECT(b2->is_focused());
            EXPECT(!b3->is_focused());
            out.reset();
        }

        // removing the modal subtree: the background becomes hittable
        // again; removing an ancestor of the modal drops it too
        {
            // path A: coordinated -- modal == the removed widget
            CanvasWindow w;
            w.create(200, 150);
            auto &root = w.root();
            auto *bg = mk_button(root, "bg", 10, 60);
            auto *dlg = mk_button(root, "dlg", 100, 100);
            w.set_modal(dlg);

            w.input(press_at(20, 70));  // blocked by the modal
            EXPECT(!dlg->is_focused());

            auto out = w.remove_from(root, dlg);  // modal dropped with it
            EXPECT(out != nullptr);
                    w.input(press_at(20, 70));  // hittable again
            EXPECT(bg->is_focused());
            out.reset();
        }
        {
            // path B: direct -- the modal lives inside the removed
            // subtree, the evict must drop it through the ancestor
            Panel root;
            root.set_size(300, 200);
            InputDispatcher d;
            auto *bg = mk_button(root, "bg", 10, 60);
            auto frame = std::make_unique<Panel>();
            frame->set_position(100, 20);
            frame->set_size(120, 100);
            auto *fp = frame.get();
            auto *dlg = mk_button(*fp, "dlg", 10, 10);
            root.add_child(std::move(frame));
            d.set_modal(dlg);

            EXPECT(!d.dispatch(root, press_at(20, 70)));  // blocked
            EXPECT(d.get_focus_target() == nullptr);

            auto out = remove_direct(d, root, fp);  // drops modal + dialog
            EXPECT(out != nullptr);
            EXPECT(!dlg->is_descendant_of(&root));

            EXPECT(d.dispatch(root, press_at(20, 70)));  // hittable again
            EXPECT(bg->is_focused());
            out.reset();
        }

        // state on removal: parent cleared, lookup dead, ownership
        // transferred destructible
        {
            Panel root;
            root.set_size(200, 150);
            auto *b = mk_button(root, "victim", 10, 10);
            EXPECT(root.find_by_id("victim") == b);

            auto out = root.remove_child(b);
            EXPECT(out != nullptr);
            EXPECT(!out->is_descendant_of(&root));
            EXPECT(out->get_id() == "victim");
            EXPECT(root.find_by_id("victim") == nullptr);
            out.reset();  // destroys fine
        }

        // removing the same pointer twice: the second call is a no-op
        {
            Panel root;
            root.set_size(200, 150);
            auto *b = mk_button(root, "again", 10, 10);

            auto out = root.remove_child(b);
            EXPECT(out != nullptr);
            EXPECT(root.remove_child(b) == nullptr);
            out.reset();
        }

        // a removed widget's held Subscription unsubscribes when the
        // transferred ownership is destroyed
        {
            zb::event::Event<> ev;
            int kept = 0;
            ev += [&]() { ++kept; };

            Panel root;
            root.set_size(200, 150);
            auto *s = new Subscriber(ev);
            root.add_child(std::unique_ptr<Widget>(s));
            EXPECT(s->calls == 0);

            ev();  // the live subscriber hears it
            EXPECT(s->calls == 1);

            auto out = root.remove_child(s);
            EXPECT(out != nullptr);
            out.reset();  // destructor unsubscribes

            ev();  // only the kept handler runs
            EXPECT(s->calls == 1);
            EXPECT(kept == 2);
        }

        return test::report("remove");
}