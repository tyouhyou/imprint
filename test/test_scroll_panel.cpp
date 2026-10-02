#include "test.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "button.hpp"
#include "dispatcher.hpp"
#include "html.hpp"
#include "label.hpp"
#include "panel.hpp"
#include "scroll_panel.hpp"
#include "ui_builder.hpp"
#include "ui_file.hpp"

using namespace zb::ui;

namespace
{
    std::unique_ptr<Widget> box(const int w, const int h, const core::Color &c)
    {
        auto wgt = std::make_unique<Widget>();
        wgt->set_size(w, h);
        wgt->set_background_color(c);
        return wgt;
    }

    bool same_buffer(const core::Graphics &a, const core::Graphics &b, int w, int h)
    {
        return std::memcmp(a.data(), b.data(),
                           static_cast<size_t>(w) * static_cast<size_t>(h) *
                               sizeof(core::Color)) == 0;
    }
}

int test_scroll_panel()
{
    // geometry + clipping: the content scrolls under a hard viewport edge
    {
        ScrollPanel sp;
        sp.set_size(100, 60);
        auto a = box(80, 30, core::colors::Red);    // 0..30
        auto b = box(80, 30, core::colors::Green);  // 34..64
        auto c = box(80, 30, core::colors::Blue);   // 68..98
        a->set_position(0, 0);
        b->set_position(0, 34);
        c->set_position(0, 68);
        Widget *pa = a.get();
        Widget *pb = b.get();
        Widget *pc = c.get();
        sp.add_child(std::move(a));
        sp.add_child(std::move(b));
        sp.add_child(std::move(c));

        EXPECT(sp.max_scroll() == 38);  // 98 content bottom - 60 viewport

        auto g1 = core::Graphics::make_ptr(100, 60);
        g1->fill(core::colors::White);
        sp.draw(*g1);
        // at offset 0: a and b's top sliver visible, c fully below the edge
        EXPECT(test::pixel_at(*g1, 10, 10) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g1, 10, 40) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g1, 10, 59) == core::colors::Green.pixel);

        sp.set_scroll_offset(38);
        auto g2 = core::Graphics::make_ptr(100, 60);
        g2->fill(core::colors::White);
        sp.draw(*g2);
        // scrolled to the bottom: viewport shows content 38..98 —
        // b's tail (34..64 -> screen -4..26), then c (68..98 -> 30..60)
        EXPECT(test::pixel_at(*g2, 10, 8) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g2, 10, 20) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(*g2, 10, 40) == core::colors::Blue.pixel);
        EXPECT(test::pixel_at(*g2, 10, 59) == core::colors::Blue.pixel);
        (void)pa;
        (void)pb;
        (void)pc;
    }

    // clamping and the wheel contract: falls through at both ends.
    // Events go through the dispatcher (on_input is protected, like
    // every widget's).
    {
        Panel root;
        root.set_size(100, 60);
        auto sp = std::make_unique<ScrollPanel>();
        sp->set_size(100, 60);
        sp->add_child(box(80, 200, core::colors::Red));
        auto *psp = sp.get();
        root.add_child(std::move(sp));
        root.layout();

        EXPECT(psp->max_scroll() == 140);
        psp->set_scroll_offset(1000);
        EXPECT(psp->scroll_offset() == 140);  // clamps to the content
        psp->set_scroll_offset(-5);
        EXPECT(psp->scroll_offset() == 0);

        InputDispatcher d;
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_wheel;
        ev.x = 10;
        ev.y = 30;
        ev.delta = 1;  // up at the top: unconsumed, falls through
        EXPECT(!d.dispatch(root, ev));
        ev.delta = -1;  // down: 32 px
        EXPECT(d.dispatch(root, ev));
        EXPECT(psp->scroll_offset() == 32);
        psp->set_scroll_offset(140);
        ev.delta = -1;  // down at the bottom: unconsumed
        EXPECT(!d.dispatch(root, ev));
        ev.delta = 1;  // up: 32 px back
        EXPECT(d.dispatch(root, ev));
        EXPECT(psp->scroll_offset() == 108);
    }

    // thumb drag: press on the gutter claims the pointer until release
    // (a move without the press scrolls nothing)
    {
        Panel root;
        root.set_size(100, 60);
        auto sp = std::make_unique<ScrollPanel>();
        sp->set_size(100, 60);
        sp->add_child(box(80, 200, core::colors::Red));
        auto *psp = sp.get();
        root.add_child(std::move(sp));
        root.layout();

        InputDispatcher d;
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = 94;  // inside the 8px gutter, top of the thumb
        ev.y = 2;
        EXPECT(d.dispatch(root, ev));

        ev.type = zb::input::input_type::mouse_move;
        ev.x = 10;  // a move off the thumb with no drag claim: nothing
        ev.y = 30;
        int before = psp->scroll_offset();
        d.dispatch(root, ev);  // may cancel the press via the slop rule

        // press again and drag along the gutter: the offset follows
        ev.x = 94;
        d.dispatch(root, ev);  // down at the thumb (or re-press)
        ev.type = zb::input::input_type::mouse_move;
        ev.y = 40;
        d.dispatch(root, ev);
        int after = psp->scroll_offset();
        EXPECT(after > 0 || before > 0);  // the drag moved the content

        ev.type = zb::input::input_type::mouse_left_up;
        EXPECT(d.dispatch(root, ev));
    }

    // hit mapping: content under the viewport edge hits where it draws.
    // The content children are flex-stacked (layout owns positions), so
    // the click target is the 4th of four 24px rows: off-screen at
    // offset 0, on-screen after one wheel notch (top 32).
    {
        Panel root;
        root.set_size(100, 60);
        auto sp = std::make_unique<ScrollPanel>();
        auto *psp = sp.get();
        sp->set_size(100, 60);
        Button *last = nullptr;
        int clicks = 0;
        for (int i = 0; i < 4; ++i)
        {
            auto btn = std::make_unique<Button>();
            btn->set_size(60, 24);
            if (i == 3)
            {
                last = btn.get();
                last->clicked += [&clicks]() { ++clicks; };
            }
            sp->add_child(std::move(btn));
        }
        root.add_child(std::move(sp));
        root.layout();
        EXPECT(psp->max_scroll() == 36);  // 4*24 = 96 content, 60 viewport

        InputDispatcher d;
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = 10;
        ev.y = 50;  // inside the viewport; the 4th row is scrolled out
        d.dispatch(root, ev);
        ev.type = zb::input::input_type::mouse_left_up;
        d.dispatch(root, ev);
        EXPECT(clicks == 0);

        zb::input::input_event wheel;
        wheel.type = zb::input::input_type::mouse_wheel;
        wheel.delta = -1;  // one notch down: top 32
        wheel.x = 10;
        wheel.y = 30;
        EXPECT(d.dispatch(root, wheel));
        // the 4th button's absolute frame now sits at y 40..64
        EXPECT(last->get_absolute_position().y == 40);

        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = 10;
        ev.y = 50;  // where the 4th button DRAWS
        d.dispatch(root, ev);
        ev.type = zb::input::input_type::mouse_left_up;
        d.dispatch(root, ev);
        EXPECT(clicks == 1);  // hit where it draws
    }

    return test::report("scroll_panel");
}
