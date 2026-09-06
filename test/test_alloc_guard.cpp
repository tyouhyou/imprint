#include "test.hpp"

#include "test_alloc_count.hpp"

#include "imapp.hpp"
#include "imui.hpp"

using namespace zb::app;
using namespace zb::ui;

// Hot-path zero-allocation gate (batch J7): the contract pins the
// render/dispatch hot paths as allocation-free (docs/code-contract.md
// section 8) and this suite guards them. Scenarios are the ones an
// embedded frame budget depends on: a parked tree repainting, pointer
// motion that changes nothing, widget painting with text, and the list
// box steady state after a scroll (rebuilds themselves are bounded by
// the row cache and pinned by the exact-count checks in
// test_list_box).
int test_alloc_guard()
{
    // 1: a parked tree repaints without allocating (paint hot path)
    {
        CanvasWindow w;
        w.create(200, 80);
        auto b = std::make_unique<Button>();
        b->set_size(60, 20);
        b->set_position(10, 10);
        w.root().add_child(std::move(b));
        w.paint();  // consume the initial frame

        test::scoped_alloc_count c;
        for (int i = 0; i < 10; ++i)
        {
            w.paint();
        }
        std::printf("parked repaint allocations (10 frames): %lld\n", c.delta());
        EXPECT(c.delta() == 0);
    }

    // 2: pointer motion that changes nothing allocates nothing
    //    (dispatch hot path); the press that arms the pointer is
    //    established outside the measured window
    {
        CanvasWindow w;
        w.create(200, 80);
        auto b = std::make_unique<Button>();
        b->set_size(60, 20);
        b->set_position(10, 10);
        w.root().add_child(std::move(b));
        w.paint();

        zb::input::input_event press = {};
        press.type = zb::input::input_type::mouse_left_down;
        press.x = 40;
        press.y = 20;
        press.touch_id = 0;
        w.input(press);
        w.paint();  // consume the button repaint

        // moves inside the press slop of the same widget: neither the
        // dispatch nor a repaint is owed, nothing may allocate
        zb::input::input_event move = {};
        move.type = zb::input::input_type::mouse_move;
        move.x = 40;
        move.y = 20;
        move.touch_id = 0;
        test::scoped_alloc_count c;
        for (int i = 0; i < 10; ++i)
        {
            move.x = 40 + (i % 4);  // stays within the 60x20 button
            move.y = 21;
            w.input(move);
        }
        std::printf("slop move dispatch allocations (10 moves): %lld\n", c.delta());
        EXPECT(c.delta() == 0);
    }

    // 3: widget drawing with visible text allocates nothing (render hot
    //    path: label and button, the two common text widgets)
    {
        CanvasWindow w;
        w.create(200, 80);
        auto label = std::make_unique<Label>();
        label->set_text("AB");
        label->set_size(60, 20);
        label->set_position(10, 10);
        auto btn = std::make_unique<Button>();
        btn->set_text("GO");
        btn->set_size(60, 20);
        btn->set_position(10, 40);
        w.root().add_child(std::move(label));
        w.root().add_child(std::move(btn));
        w.paint();  // park

        test::scoped_alloc_count c;
        for (int i = 0; i < 10; ++i)
        {
            w.paint();
        }
        std::printf("label+button paint allocations (10 frames): %lld\n", c.delta());
        EXPECT(c.delta() == 0);
    }

    // 4: after a scroll, the parked list box repaints warm: the row
    //    cache serves the visible window and the steady state is
    //    allocation-free (the rebuild happens inside the dispatch below,
    //    out of the measured window; its bound is pinned by
    //    test_list_box)
    {
        CanvasWindow w;
        w.create(200, 80);
        auto list = std::make_unique<ListBox>();
        list->set_row_height(16);
        list->set_visible_rows(3);
        list->set_items(std::vector<std::string>{"a", "b", "c", "d", "e", "f"});
        w.root().add_child(std::move(list));
        w.paint();  // park; rows 0..2 cached

        // wheel down shows rows 3..5 (rebuild inside this paint)
        zb::input::input_event wheel = {};
        wheel.type = zb::input::input_type::mouse_wheel;
        wheel.x = 50;
        wheel.y = 20;
        wheel.delta = -1;
        w.input(wheel);
        // wheel up returns to rows 0..2 (rebuilt here as well)
        wheel.delta = 1;
        w.input(wheel);

        test::scoped_alloc_count c;
        for (int i = 0; i < 5; ++i)
        {
            w.paint();
        }
        std::printf("list box warm repaint allocations (5 frames): %lld\n", c.delta());
        EXPECT(c.delta() == 0);
    }

    return test::report("alloc_guard");
}