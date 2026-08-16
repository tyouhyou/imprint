#include "test.hpp"

#include <cstdio>
#include <cstring>

#include "test_alloc_count.hpp"

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

    zb::input::input_event key_down(const int key)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.key = key;
        return ev;
    }

    zb::input::input_event wheel_at(const int x, const int y, const int delta)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_wheel;
        ev.x = x;
        ev.y = y;
        ev.delta = delta;
        return ev;
    }

    std::string item_text(const void *, const size_t i)
    {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%zu", i);
        return buf;
    }

    // 6-row model in a 100x48 list at (10,10): 3 rows visible (16px each),
    // scrollbar gutter at x 102..109
    struct Tree
    {
        Panel root;
        ListBox *list = nullptr;

        Tree()
        {
            root.set_size(140, 80);
            auto l = std::make_unique<ListBox>();
            l->set_row_height(16);
            l->set_size(100, 1);
            l->set_visible_rows(3);
            l->set_item_text(item_text, nullptr);
            l->set_item_count(6);
            l->set_position(10, 10);
            list = l.get();
            root.add_child(std::move(l));
        }
    };
}  // namespace

int test_list_box()
{
        // geometry: the widget height follows the visible row count
        {
            Tree t;
            EXPECT(t.list->get_size().height == 48);
            EXPECT(t.list->get_top() == 0);
            EXPECT(t.list->get_value() == ListBox::invalid);
        }

        // press a row: selection + changed; presses on the selected row
        // are no-ops (note: += keeps the handler; a discarded
        // subscribe() guard unsubscribes immediately by design)
        {
            Tree t;
            InputDispatcher d;
            size_t last = ListBox::invalid;
            t.list->changed += [&](const size_t v) { last = v; };

            EXPECT(d.dispatch(t.root, press_at(20, 50)));  // local (10, 40): row 2
            EXPECT(t.list->get_value() == 2);
            EXPECT(last == 2);

            last = ListBox::invalid;
            d.dispatch(t.root, release_at(20, 50));
            EXPECT(!d.dispatch(t.root, press_at(20, 50)));
            EXPECT(last == ListBox::invalid);

            // below the last visible row there is nothing to select
            EXPECT(!d.dispatch(t.root, press_at(20, 70)));  // local y 60: row 3
        }

        // keyboard: up/down move the selection and reveal it
        {
            Tree t;
            InputDispatcher d;
            d.dispatch(t.root, press_at(20, 18));  // row 0, grabs focus
            EXPECT(d.get_focus_target() == t.list);

            for (int i = 0; i < 5; ++i)
            {
                EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::down))));
            }
            EXPECT(t.list->get_value() == 5);
            EXPECT(t.list->get_top() == 3);  // window 3..5

            EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::up))));
            EXPECT(t.list->get_value() == 4);
            EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::up))));
            EXPECT(t.list->get_value() == 3);
            EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::up))));
            EXPECT(t.list->get_value() == 2);
            EXPECT(t.list->get_top() == 2);  // revealed upward
            EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::up))));
            EXPECT(t.list->get_value() == 1);
            EXPECT(d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::up))));
            EXPECT(t.list->get_value() == 0);
            EXPECT(t.list->get_top() == 0);

            // at the top of the model the key is not consumed: the focus
            // navigation takes over (single focusable: wraps to itself)
            d.dispatch(t.root, key_down(static_cast<int>(zb::input::key_code::up)));
            EXPECT(t.list->get_value() == 0);
            EXPECT(t.list->get_top() == 0);
        }

        // wheel: scrolls the window, clamps at both ends, needs overflow
        {
            Tree t;
            InputDispatcher d;
            EXPECT(d.dispatch(t.root, wheel_at(20, 20, -1)));  // down: top 0 -> 3
            EXPECT(t.list->get_top() == 3);
            EXPECT(!d.dispatch(t.root, wheel_at(20, 20, -1)));  // already clamped
            EXPECT(t.list->get_top() == 3);
            EXPECT(d.dispatch(t.root, wheel_at(20, 20, 1)));  // up: 3 -> 0
            EXPECT(t.list->get_top() == 0);
            EXPECT(!d.dispatch(t.root, wheel_at(20, 20, 1)));  // at the top
            EXPECT(!d.dispatch(t.root, wheel_at(5, 5, 1)));    // outside the list

            t.list->set_item_count(3);  // fits: nothing to scroll
            EXPECT(!d.dispatch(t.root, wheel_at(20, 20, -1)));
        }

        // thumb drag: grabs with the press offset, clamps to both ends,
        // survives moves far outside the widget; the release ends it
        {
            Tree t;
            InputDispatcher d;
            t.list->set_item_count(20);  // 17 rows of travel
            EXPECT(t.list->get_value() == ListBox::invalid);  // no selection

            // thumb spans local y 1..8 at top=0: press local (97, 4)
            EXPECT(d.dispatch(t.root, press_at(107, 14)));
            EXPECT(d.dispatch(t.root, move_to(107, 33)));  // new thumb top 20
            EXPECT(t.list->get_top() == static_cast<size_t>(20 * 17 / 38));
            d.dispatch(t.root, move_to(400, 300));  // far away: clamps
            EXPECT(t.list->get_top() == 17);

            EXPECT(d.dispatch(t.root, release_at(400, 300)));
            EXPECT(!d.dispatch(t.root, move_to(107, 14)));  // no in-flight drag

            // drag back up: thumb at max is local y 39..46
            EXPECT(d.dispatch(t.root, press_at(107, 50)));  // grab = 1
            d.dispatch(t.root, move_to(107, 30));           // new thumb top 19
            EXPECT(t.list->get_top() == static_cast<size_t>(19 * 17 / 38));
        }

        // set_value is silent and range-checked; shrinking the model
        // clears an out-of-range selection
        {
            Tree t;
            int calls = 0;
            t.list->changed += [&](const size_t) { ++calls; };

            t.list->set_value(4);
            EXPECT(t.list->get_value() == 4);
            t.list->set_value(999);
            EXPECT(t.list->get_value() == 4);
            t.list->set_value(ListBox::invalid);
            EXPECT(t.list->get_value() == ListBox::invalid);
            EXPECT(calls == 0);

            InputDispatcher d;
            EXPECT(d.dispatch(t.root, press_at(20, 18)));  // row 0
            EXPECT(calls == 1);

            t.list->set_value(5);
            t.list->set_item_count(3);
            EXPECT(t.list->get_value() == ListBox::invalid);
        }

        // drawing: row background, selection fill, scrollbar track and
        // thumb occupy their pixels
        {
            Tree t;
            InputDispatcher d;
            core::Graphics g(140, 80, nullptr);
            g.fill_rect(0, 0, 139, 79, core::colors::White);
            t.root.draw(g);

            EXPECT(core::Color::from(240, 240, 240).pixel == test::pixel_at(g, 60, 18));
            EXPECT(core::Color::from(200, 200, 200).pixel == test::pixel_at(g, 104, 40));
            EXPECT(core::Color::from(120, 120, 120).pixel == test::pixel_at(g, 104, 14));

            d.dispatch(t.root, press_at(70, 34));  // local (60, 24): row 1
            t.root.draw(g);
            EXPECT(core::Color::from(0, 80, 200).pixel == test::pixel_at(g, 60, 30));
            EXPECT(core::Color::from(0, 80, 200).pixel != test::pixel_at(g, 60, 13));
        }

        // row-image cache (batch J2): warm repaints are zero-allocation
        // and pixel-identical to a cold render; selection, scroll and
        // model changes invalidate the cache and rebuild it. The
        // reference uses a second list instance so its paints are truly
        // cold (the cache lives in the widget, not per surface).
        {
            Tree ref;
            Tree meas;
            InputDispatcher dref;
            InputDispatcher dmeas;
            core::Graphics gr(140, 80, nullptr);
            core::Graphics gm(140, 80, nullptr);

            const auto paint = [](Tree &t, core::Graphics &g)
            {
                g.fill_rect(0, 0, 139, 79, core::colors::White);
                t.root.draw(g);
            };
            const auto same = [&]()
            {
                return std::memcmp(gr.data(), gm.data(),
                                   140u * 80u * sizeof(core::Color)) == 0;
            };

            paint(ref, gr);
            paint(meas, gm);
            EXPECT(same());

            // warm repaint: cache hits, no row re-rasterization, no text
            // fetch -- zero allocations
            {
                test::scoped_alloc_count c;
                paint(meas, gm);
                std::printf("warm list repaint allocations: %lld\n", c.delta());
                EXPECT(c.delta() == 0);
            }
            EXPECT(same());

            // selection change (value): invalidated, rebuilt on paint
            dref.dispatch(ref.root, press_at(70, 34));  // local (60, 24): row 1
            dmeas.dispatch(meas.root, press_at(70, 34));
            paint(ref, gr);
            {
                test::scoped_alloc_count c;
                paint(meas, gm);
                std::printf("selection rebuild allocations: %lld\n", c.delta());
                EXPECT(c.delta() > 0);
            }
            EXPECT(same());

            // scroll (top): same
            dref.dispatch(ref.root, wheel_at(20, 20, -1));  // top 0 -> 3
            dmeas.dispatch(meas.root, wheel_at(20, 20, -1));
            paint(ref, gr);
            {
                test::scoped_alloc_count c;
                paint(meas, gm);
                std::printf("scroll rebuild allocations: %lld\n", c.delta());
                EXPECT(c.delta() > 0);
            }
            EXPECT(same());

            // model swap (set_items): same
            ref.list->set_items(std::vector<std::string>{"a", "b", "c", "d", "e", "f"});
            meas.list->set_items(std::vector<std::string>{"a", "b", "c", "d", "e", "f"});
            paint(ref, gr);
            {
                test::scoped_alloc_count c;
                paint(meas, gm);
                std::printf("model swap rebuild allocations: %lld\n", c.delta());
                EXPECT(c.delta() > 0);
            }
            EXPECT(same());

            // ... and the rebuilt cache is warm again
            {
                test::scoped_alloc_count c;
                paint(meas, gm);
                std::printf("post-rebuild warm allocations: %lld\n", c.delta());
                EXPECT(c.delta() == 0);
            }
            EXPECT(same());
        }

        return test::report("list_box");
}