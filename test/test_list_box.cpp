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

    // FI-06 height models. array_height reads a table (the VarTree
    // model: 10, 30, 20, 5, 40, 15 -- total 120); flat_height ignores
    // the index; bad_height returns the degenerate values (<= 0).
    constexpr int var_heights[6] = {10, 30, 20, 5, 40, 15};

    int array_height(const void *arg, const size_t i)
    {
        return static_cast<const int *>(arg)[i];
    }

    int flat_height(const void *, const size_t)
    {
        return 20;
    }

    int bad_height(const void *, const size_t i)
    {
        return i == 2 ? 8 : (i == 1 ? -5 : 0);  // -> 1, 1, 8, 1
    }

    // variable-height tree: the provider re-derives the widget height
    // (rows 0..2 fill the 60px window exactly); gutter at x 102..109
    struct VarTree
    {
        Panel root;
        ListBox *list = nullptr;

        VarTree()
        {
            root.set_size(140, 80);
            auto l = std::make_unique<ListBox>();
            l->set_size(100, 1);
            l->set_visible_rows(3);
            l->set_item_count(6);
            l->set_item_text(item_text, nullptr);
            l->set_item_height(array_height, var_heights);
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
        t.list->changed += [&](const size_t v)
        { last = v; };

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
        t.list->set_item_count(20);                       // 17 rows of travel
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

    // a cancelled thumb drag must not poison the next press: without
    // the on_cancel reset the stale dragging flag turns a plain row
    // press into a phantom thumb drag (Z6)
    {
        Tree t;
        InputDispatcher d;
        t.list->set_item_count(20);
        EXPECT(d.dispatch(t.root, press_at(107, 14)));  // thumb grab
        d.reset();                                      // e.g. focus lost
        EXPECT(t.list->get_top() == 0);

        // a row press behaves as a press, not as a drag
        EXPECT(d.dispatch(t.root, press_at(20, 18)));  // row 0
        EXPECT(t.list->get_value() == 0);
        EXPECT(!d.dispatch(t.root, move_to(20, 30)));  // plain move
        EXPECT(t.list->get_top() == 0);                // nothing scrolled
    }

    // row_height clamps to >= 1 (a zero height divided by zero on
    // the next click) and re-derives the widget height (Z7)
    {
        Tree t;
        InputDispatcher d;
        t.list->set_row_height(0);
        EXPECT(t.list->get_size().height == 3);        // 3 visible rows * 1
        EXPECT(d.dispatch(t.root, press_at(20, 10)));  // local y 0: no trap
        EXPECT(t.list->get_value() == 0);
        t.list->set_row_height(16);
        EXPECT(t.list->get_size().height == 48);
    }

    // the scrollbar gutter is not the row area: a click on the track
    // below the thumb selects nothing (F12)
    {
        Tree t;
        InputDispatcher d;
        EXPECT(!d.dispatch(t.root, press_at(106, 40)));  // local (96, 30)
        EXPECT(t.list->get_value() == ListBox::invalid);
    }

    // set_value is silent and range-checked; shrinking the model
    // clears an out-of-range selection
    {
        Tree t;
        int calls = 0;
        t.list->changed += [&](const size_t)
        { ++calls; };

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
        // fetch -- zero allocations and zero rasterizations
        {
            test::scoped_alloc_count c;
            const long long r0 = meas.list->rasterization_count();
            paint(meas, gm);
            std::printf("warm list repaint allocations: %lld\n", c.delta());
            EXPECT(c.delta() == 0);
            EXPECT(meas.list->rasterization_count() == r0);
        }
        EXPECT(same());

        // Rebuild proof is the rasterization counter, not the
        // allocation delta: the row images are allocated inside
        // imcore, which on Windows is a DLL the test binary's
        // operator-new replacement cannot see (code-contract §8).
        // A full invalidation re-rasterizes exactly the visible
        // window (3 rows).

        // selection change (value): invalidated, rebuilt on paint
        dref.dispatch(ref.root, press_at(70, 34));  // local (60, 24): row 1
        dmeas.dispatch(meas.root, press_at(70, 34));
        paint(ref, gr);
        {
            test::scoped_alloc_count c;
            const long long r0 = meas.list->rasterization_count();
            paint(meas, gm);
            std::printf("selection rebuild rasterizations: %lld (allocations %lld)\n",
                        meas.list->rasterization_count() - r0, c.delta());
            EXPECT(meas.list->rasterization_count() - r0 == 3);
        }
        EXPECT(same());

        // scroll (top): same
        dref.dispatch(ref.root, wheel_at(20, 20, -1));  // top 0 -> 3
        dmeas.dispatch(meas.root, wheel_at(20, 20, -1));
        paint(ref, gr);
        {
            test::scoped_alloc_count c;
            const long long r0 = meas.list->rasterization_count();
            paint(meas, gm);
            std::printf("scroll rebuild rasterizations: %lld (allocations %lld)\n",
                        meas.list->rasterization_count() - r0, c.delta());
            EXPECT(meas.list->rasterization_count() - r0 == 3);
        }
        EXPECT(same());

        // model swap (set_items): same
        ref.list->set_items(std::vector<std::string>{"a", "b", "c", "d", "e", "f"});
        meas.list->set_items(std::vector<std::string>{"a", "b", "c", "d", "e", "f"});
        paint(ref, gr);
        {
            test::scoped_alloc_count c;
            const long long r0 = meas.list->rasterization_count();
            paint(meas, gm);
            std::printf("model swap rebuild rasterizations: %lld (allocations %lld)\n",
                        meas.list->rasterization_count() - r0, c.delta());
            EXPECT(meas.list->rasterization_count() - r0 == 3);
        }
        EXPECT(same());

        // ... and the rebuilt cache is warm again
        {
            test::scoped_alloc_count c;
            const long long r0 = meas.list->rasterization_count();
            paint(meas, gm);
            std::printf("post-rebuild warm allocations: %lld\n", c.delta());
            EXPECT(c.delta() == 0);
            EXPECT(meas.list->rasterization_count() == r0);
        }
        EXPECT(same());
    }

    // L-3: a declarative list without a width stays measured (100
    // wide) instead of freezing the pre-layout 0 as explicit
    {
        ui_node doc;
        doc.type = "column";
        ui_node lb;
        lb.type = "list_box";
        lb.prop("rows", 3LL);
        lb.named("l");
        doc.children.push_back(std::move(lb));

        FlexPanel host;
        host.set_size(200, 200);
        build(host, doc);
        host.layout();
        auto *l = static_cast<ListBox *>(host.find_by_id("l"));
        EXPECT(l != nullptr);
        EXPECT(l->get_size().width == 100);
        EXPECT(l->get_size().height == 48);
        EXPECT(!l->is_width_explicit());
    }

    // L-3: an explicit width survives rows changes, declarative or
    // programmatic
    {
        ui_node doc;
        doc.type = "column";
        ui_node lb;
        lb.type = "list_box";
        lb.prop("rows", 3LL);
        lb.prop("width", 120LL);
        lb.named("l");
        doc.children.push_back(std::move(lb));

        FlexPanel host;
        host.set_size(200, 200);
        build(host, doc);
        host.layout();
        auto *l = static_cast<ListBox *>(host.find_by_id("l"));
        EXPECT(l != nullptr);
        EXPECT(l->get_size().width == 120);
        EXPECT(l->get_size().height == 48);
        EXPECT(l->is_width_explicit());

        l->set_visible_rows(2);
        EXPECT(l->get_size().width == 120);
        EXPECT(l->get_size().height == 32);
        EXPECT(l->is_width_explicit());
    }

    // L-3: programmatic rows/row-height keep an auto width auto
    {
        ListBox l;
        l.set_visible_rows(3);
        EXPECT(!l.is_width_explicit());
        EXPECT(l.is_height_explicit());
        EXPECT(l.get_size().height == 48);

        l.set_row_height(20);
        EXPECT(!l.is_width_explicit());
        EXPECT(l.get_size().height == 60);  // 3 rows * 20
    }

    // F7: rows/visible clamp — zero and absurd sizes never stick
    {
        ListBox l;
        l.set_visible_rows(0);
        EXPECT(l.get_size().height >= 1);  // clamped to 1 row
        l.set_visible_rows(1000000);
        EXPECT(l.get_size().height > 0);
        EXPECT(l.get_size().height <= 100000 * 16);
    }

    // FI-06 variable-height rows (code-contract §3.7): the widget
    // height and the offsets are prefix sums over the provider's
    // heights (10, 30, 20, 5, 40, 15 -- total 120)
    {
        VarTree t;
        // set_visible_rows(3) sized the fixed 48 before the provider;
        // installing the provider re-derived the height from the model
        EXPECT(t.list->get_size().height == 60);  // 10 + 30 + 20
        EXPECT(t.list->measure().height == 60);
        EXPECT(t.list->is_width_explicit());  // L-3: width untouched

        // rows sizing derives from the provider's own heights
        t.list->set_visible_rows(5);
        EXPECT(t.list->get_size().height == 105);  // 10+30+20+5+40
        t.list->set_visible_rows(10);              // past the model: all of it
        EXPECT(t.list->get_size().height == 120);
        t.list->set_visible_rows(3);
        EXPECT(t.list->get_size().height == 60);
        EXPECT(t.list->get_top() == 0);
    }

    // mixed-height hit testing: click coordinates map through the
    // prefix sums to the right row, boundaries exact
    {
        VarTree t;
        InputDispatcher d;
        size_t last = ListBox::invalid;
        t.list->changed += [&](const size_t v)
        { last = v; };

        // local y 45 sits in row 2 ([40, 60))
        EXPECT(d.dispatch(t.root, press_at(30, 55)));  // local (20, 45)
        EXPECT(t.list->get_value() == 2);
        EXPECT(last == 2);

        // local y 9 sits in row 0 ([0, 10)) ...
        last = ListBox::invalid;
        d.dispatch(t.root, release_at(30, 55));
        EXPECT(d.dispatch(t.root, press_at(30, 19)));
        EXPECT(t.list->get_value() == 0);
        // ... and the boundary is exact: local y 10 is row 1
        last = ListBox::invalid;
        d.dispatch(t.root, release_at(30, 19));
        EXPECT(d.dispatch(t.root, press_at(30, 20)));
        EXPECT(t.list->get_value() == 1);
    }

    // wheel: one notch = 3 x the topmost visible row's height
    {
        VarTree t;
        InputDispatcher d;
        // top row 0 (h 10): one notch = 30px -> mid-row-1 window
        EXPECT(d.dispatch(t.root, wheel_at(20, 20, -1)));
        EXPECT(t.list->get_top() == 1);
        // top row 1 (h 30): one notch = 90px -> clamps at the bottom
        EXPECT(d.dispatch(t.root, wheel_at(20, 20, -1)));
        EXPECT(t.list->get_top() == 3);  // window top 60 = row 3's span
        EXPECT(!d.dispatch(t.root, wheel_at(20, 20, -1)));
        // top row 3 (h 5): one notch up = 15px
        EXPECT(d.dispatch(t.root, wheel_at(20, 20, 1)));
        EXPECT(t.list->get_top() == 2);
        // top row 2 (h 20): one notch up = 60px -> clamps at the top
        EXPECT(d.dispatch(t.root, wheel_at(20, 20, 1)));
        EXPECT(t.list->get_top() == 0);
        EXPECT(!d.dispatch(t.root, wheel_at(20, 20, 1)));
    }

    // deep scroll renders the right rows: at the bottom the window top
    // sits at pixel 60 (row 3's span), and selecting row 5 fills its
    // own band with the selection face where it draws
    {
        VarTree t;
        InputDispatcher d;
        while (d.dispatch(t.root, wheel_at(20, 20, -1)))
        {
        }
        EXPECT(t.list->get_top() == 3);
        t.list->set_value(5);
        size_t last = ListBox::invalid;
        t.list->changed += [&](const size_t v)
        { last = v; };
        core::Graphics g(140, 80, nullptr);
        g.fill_rect(0, 0, 139, 79, core::colors::White);
        t.root.draw(g);
        // row 5 spans [105, 120) -> local y 45..60 -> abs 55..70
        EXPECT(core::Color::from(0, 80, 200).pixel == test::pixel_at(g, 70, 56));
        // row 4's band (abs 15..55) stays unselected
        EXPECT(core::Color::from(0, 80, 200).pixel != test::pixel_at(g, 70, 21));
        // the thumb derives from the pixel totals: 29 of the 58px
        // travel at the bottom of the track
        EXPECT(core::Color::from(120, 120, 120).pixel == test::pixel_at(g, 104, 55));
        EXPECT(core::Color::from(200, 200, 200).pixel == test::pixel_at(g, 104, 14));
        // a click on row 4 where it draws selects it (row 5 is already
        // selected: a re-select press is a no-op)
        EXPECT(d.dispatch(t.root, press_at(70, 30)));  // local (60, 20)
        EXPECT(last == 4);
    }

    // keyboard reveal keeps the selected row's whole span inside the
    // window (prefix-sum scroll)
    {
        VarTree t;
        InputDispatcher d;
        EXPECT(d.dispatch(t.root, press_at(20, 18)));  // row 0, grabs focus
        const auto down = key_down(static_cast<int>(zb::input::key_code::down));
        const auto up = key_down(static_cast<int>(zb::input::key_code::up));
        EXPECT(d.dispatch(t.root, down));
        EXPECT(t.list->get_value() == 1);
        EXPECT(t.list->get_top() == 0);  // row 1 already inside [0, 60)
        EXPECT(d.dispatch(t.root, down));
        EXPECT(t.list->get_value() == 2);
        EXPECT(t.list->get_top() == 0);
        EXPECT(d.dispatch(t.root, down));
        EXPECT(t.list->get_value() == 3);
        EXPECT(t.list->get_top() == 0);  // window top 5: mid-row-0
        EXPECT(d.dispatch(t.root, down));
        EXPECT(t.list->get_value() == 4);
        EXPECT(t.list->get_top() == 2);  // window top 45: mid-row-2
        EXPECT(d.dispatch(t.root, down));
        EXPECT(t.list->get_value() == 5);
        EXPECT(t.list->get_top() == 3);  // clamped at the bottom
        EXPECT(d.dispatch(t.root, up));
        EXPECT(t.list->get_value() == 4);
        EXPECT(d.dispatch(t.root, up));
        EXPECT(t.list->get_value() == 3);
        EXPECT(d.dispatch(t.root, up));
        EXPECT(t.list->get_value() == 2);
        EXPECT(t.list->get_top() == 2);  // revealed upward: window top 40
    }

    // a provider change invalidates the row cache even when the heights
    // are identical: the next paint re-rasterizes the visible window
    // (code-contract §8 observability)
    {
        VarTree t;
        core::Graphics g(140, 80, nullptr);
        const auto paint = [&]()
        {
            g.fill_rect(0, 0, 139, 79, core::colors::White);
            t.root.draw(g);
        };
        paint();
        const long long warm = t.list->rasterization_count();
        paint();
        EXPECT(t.list->rasterization_count() == warm);  // warm repaint

        t.list->set_item_height(array_height, var_heights);  // same provider
        paint();
        EXPECT(t.list->rasterization_count() - warm == 3);

        t.list->set_item_height(flat_height, nullptr);  // replaced
        paint();
        EXPECT(t.list->rasterization_count() - warm == 6);

        // the flat provider re-derived the same 60px height (3 x 20):
        // same geometry, rebuilt rows, warm again after that
        EXPECT(t.list->get_size().height == 60);
        paint();
        EXPECT(t.list->rasterization_count() == warm + 6);
    }

    // a provider returning <= 0 is tolerated as 1 (a zero-height row
    // can neither be hit nor seen)
    {
        Panel root;
        root.set_size(140, 80);
        auto l = std::make_unique<ListBox>();
        l->set_size(100, 1);
        l->set_visible_rows(3);
        l->set_item_count(4);
        l->set_item_text(item_text, nullptr);
        l->set_item_height(bad_height, nullptr);
        EXPECT(l->get_size().height == 10);  // 1 + 1 + 8 for rows [0, 3)
        EXPECT(l->measure().height == 10);
        l->set_position(10, 10);
        auto *pl = l.get();
        root.add_child(std::move(l));

        InputDispatcher d;
        size_t last = ListBox::invalid;
        pl->changed += [&](const size_t v)
        { last = v; };
        EXPECT(d.dispatch(root, press_at(20, 10)));  // local y 0: the 1px row 0
        EXPECT(last == 0);
        EXPECT(d.dispatch(root, release_at(20, 10)));
        EXPECT(d.dispatch(root, press_at(20, 16)));  // local y 6: row 2
        EXPECT(last == 2);
    }

    // the natural order provider -> visible_rows -> model: a count
    // change under a provider re-derives the widget height from the
    // fresh sums (mirrors set_item_height). visible_height clamps to
    // the count, so shrinking the model under the window pulls the
    // height in and growing it restores the full window sum.
    {
        Panel root;
        root.set_size(140, 80);
        auto l = std::make_unique<ListBox>();
        l->set_size(100, 1);
        l->set_item_count(6);
        l->set_item_text(item_text, nullptr);
        l->set_item_height(array_height, var_heights);
        l->set_visible_rows(3);
        EXPECT(l->get_size().height == 60);  // 10 + 30 + 20
        l->set_item_count(2);
        EXPECT(l->get_size().height == 40);  // [0, 3) clamps to 2 rows
        l->set_item_count(6);
        EXPECT(l->get_size().height == 60);
    }

    return test::report("list_box");
}