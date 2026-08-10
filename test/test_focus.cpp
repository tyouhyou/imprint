#include "test.hpp"

#include "imui.hpp"

using namespace zb::ui;

namespace
{
    zb::input::input_event key_event(const zb::input::key_code k)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.key = static_cast<int>(k);
        return ev;
    }

    // a root panel with two buttons at (0,0) and (30,0), both 20x20
    struct Tree
    {
        Panel root;
        Button *a = nullptr;
        Button *b = nullptr;

        Tree()
        {
            root.set_size(100, 100);

            auto x = std::make_unique<Button>();
            x->set_size(20, 20);
            a = x.get();
            root.add_child(std::move(x));

            auto y = std::make_unique<Button>();
            y->set_size(20, 20);
            y->set_position(30, 0);
            b = y.get();
            root.add_child(std::move(y));
        }
    };
}

int test_focus()
{
    // Tab cycles through focusable widgets
    {
        Tree t;
        InputDispatcher d;
        EXPECT(!t.a->is_focused() && !t.b->is_focused());

        d.dispatch(t.root, key_event(zb::input::key_code::tab));
        EXPECT(t.a->is_focused());
        EXPECT(!t.b->is_focused());

        d.dispatch(t.root, key_event(zb::input::key_code::tab));
        EXPECT(!t.a->is_focused());
        EXPECT(t.b->is_focused());

        d.dispatch(t.root, key_event(zb::input::key_code::tab));  // wraps
        EXPECT(t.a->is_focused());
    }

    // arrow keys move the focus both ways
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, key_event(zb::input::key_code::down));
        EXPECT(t.a->is_focused());
        d.dispatch(t.root, key_event(zb::input::key_code::down));
        EXPECT(t.b->is_focused());
        d.dispatch(t.root, key_event(zb::input::key_code::up));
        EXPECT(t.a->is_focused());
    }

    // Enter activates the focused button
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.b->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, key_event(zb::input::key_code::tab));
        d.dispatch(t.root, key_event(zb::input::key_code::tab));  // focus b
        d.dispatch(t.root, key_event(zb::input::key_code::enter));
        EXPECT(clicks == 1);
    }

    // Space also activates
    {
        Tree t;
        InputDispatcher d;
        int clicks = 0;
        t.a->clicked += [&clicks]() { ++clicks; };

        d.dispatch(t.root, key_event(zb::input::key_code::tab));
        d.dispatch(t.root, key_event(zb::input::key_code::space));
        EXPECT(clicks == 1);
    }

    // panels are not focusable: Tab with no buttons does nothing
    {
        Panel root;
        root.set_size(100, 100);
        auto sub = std::make_unique<Panel>();
        sub->set_size(50, 50);
        auto *psub = sub.get();
        root.add_child(std::move(sub));

        InputDispatcher d;
        d.dispatch(root, key_event(zb::input::key_code::tab));
        EXPECT(d.get_focus_target() == nullptr);
        EXPECT(!psub->is_focused());
    }

    // focusable widgets inside nested panels are found
    {
        Panel root;
        root.set_size(100, 100);
        auto sub = std::make_unique<Panel>();
        sub->set_size(50, 50);
        auto *psub = sub.get();
        auto b = std::make_unique<Button>();
        b->set_size(10, 10);
        auto *pbtn = b.get();
        sub->add_child(std::move(b));
        root.add_child(std::move(sub));

        InputDispatcher d;
        d.dispatch(root, key_event(zb::input::key_code::tab));
        EXPECT(pbtn->is_focused());
        EXPECT(!psub->is_focused());
    }

    // a mouse press grants the focus
    {
        Tree t;
        InputDispatcher d;

        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = 35;
        ev.y = 10;  // on button b
        d.dispatch(t.root, ev);
        EXPECT(t.b->is_focused());
        EXPECT(!t.a->is_focused());
    }

    // moving focus clears the previous one
    {
        Tree t;
        InputDispatcher d;
        d.dispatch(t.root, key_event(zb::input::key_code::tab));
        d.dispatch(t.root, key_event(zb::input::key_code::tab));
        EXPECT(t.b->is_focused());
        d.clear_focus();
        EXPECT(!t.a->is_focused());
        EXPECT(!t.b->is_focused());
        EXPECT(d.get_focus_target() == nullptr);
    }

    // focus border is drawn instead of the normal border
    {
        auto g = core::Graphics::make_ptr(8, 8);
        Panel root;
        root.set_size(8, 8);
        auto b = std::make_unique<Button>();
        b->set_size(4, 4);
        b->set_background_color(core::colors::White);
        b->set_border_color(core::colors::Black);
        b->set_focus_border_color(core::colors::Red);
        auto *pbtn = b.get();
        root.add_child(std::move(b));

        InputDispatcher d;
        pbtn->draw(*g);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Black.pixel);  // unfocused

        d.dispatch(root, key_event(zb::input::key_code::tab));  // grants focus
        pbtn->draw(*g);
        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Red.pixel);  // focused
        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::White.pixel);
    }

    // buttons inside a closed dialog are not focusable
    {
        Panel root;
        root.set_size(100, 100);
        auto dlg = std::make_unique<Dialog>();
        dlg->set_size(40, 40);
        dlg->set_frame_size(40, 40);
        dlg->add_button("OK");
        dlg->close();
        auto *pdlg = dlg.get();
        root.add_child(std::move(dlg));

        InputDispatcher d;
        d.dispatch(root, key_event(zb::input::key_code::tab));
        EXPECT(d.get_focus_target() == nullptr);

        // after opening the dialog its button can be focused
        pdlg->open();
        d.dispatch(root, key_event(zb::input::key_code::tab));
        EXPECT(d.get_focus_target() != nullptr);
        if (d.get_focus_target() != nullptr)
        {
            EXPECT(d.get_focus_target()->is_focusable());
        }
    }

    return test::report("focus");
}
