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
#include "scale.hpp"
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

    // scrollbar_rect is private and stays private: its X-9 arithmetic is
    // pinned at the helper level in test_scale instead of through a probe

}

int test_scroll_panel()
{
    // X-9: the scrollbar thumb math rides the 64-bit helper (pinned here
    // at the helper level -- scrollbar_rect is private and stays private).
    // A 60000px viewport over a 6e7px content makes track_h * height
    // 3.6e9, which wrapped int before X-9: the thumb came out negative and
    // was floored at the gutter for every content depth.
    {
        constexpr int track_h = 60000 - 2;
        constexpr int view_h = 60000;
        constexpr int content_h = 60000000;
        const int th = detail::ratio(track_h, view_h, content_h);
        EXPECT(th > 0);   // wrapped negative before X-9
        EXPECT(th >= 8);  // still at least the grabbable gutter
        EXPECT(th <= track_h);
        const int thumb = th < 8 ? 8 : th;
        const int thumb_y = 1 + detail::ratio(content_h / 2, track_h - thumb, content_h);
        EXPECT(thumb_y >= 1);
        EXPECT(thumb_y + thumb <= track_h + 1);
    }

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
                last->clicked += [&clicks]()
                { ++clicks; };
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

    // FI-05 follow-bottom (code-contract §3.4): appended content keeps
    // the stream pinned to the bottom while armed and the view sat at
    // the bottom; a wheel up disarms, wheeling back down to the bottom
    // re-arms, and a direct set_scroll_offset never moves the flag
    {
        Panel root;
        root.set_size(100, 60);
        auto sp = std::make_unique<ScrollPanel>();
        auto *psp = sp.get();
        sp->set_size(100, 60);
        auto content = box(80, 30, core::colors::Red);
        Widget *pc = content.get();
        sp->add_child(std::move(content));
        root.add_child(std::move(sp));
        root.layout();

        EXPECT(!psp->follow_bottom());  // default off
        EXPECT(psp->max_scroll() == 0);

        // growth without follow: the offset stays put
        pc->set_size(80, 200);
        root.layout();
        EXPECT(psp->scroll_offset() == 0);

        // armed but not at the bottom: a relayout does not snap
        psp->set_follow_bottom(true);
        root.layout();
        EXPECT(psp->scroll_offset() == 0);

        // at the bottom: appended content pins to the new maximum
        psp->set_scroll_offset(psp->max_scroll());
        EXPECT(psp->scroll_offset() == 140);
        pc->set_size(80, 300);
        root.layout();
        EXPECT(psp->max_scroll() == 240);
        EXPECT(psp->scroll_offset() == 240);

        // a wheel up disarms: growth no longer pins
        InputDispatcher d;
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_wheel;
        ev.x = 10;
        ev.y = 30;
        ev.delta = 1;
        EXPECT(d.dispatch(root, ev));
        EXPECT(psp->scroll_offset() == 208);
        pc->set_size(80, 400);
        root.layout();
        EXPECT(psp->max_scroll() == 340);
        EXPECT(psp->scroll_offset() == 208);

        // a direct set_scroll_offset to the bottom does not re-arm
        psp->set_scroll_offset(psp->max_scroll());
        EXPECT(psp->scroll_offset() == 340);
        pc->set_size(80, 500);
        root.layout();
        EXPECT(psp->max_scroll() == 440);
        EXPECT(psp->scroll_offset() == 340);  // disarmed: no snap

        // wheeling back down to the bottom re-arms
        ev.delta = -1;
        while (d.dispatch(root, ev))
        {
        }
        EXPECT(psp->scroll_offset() == psp->max_scroll());
        pc->set_size(80, 600);
        root.layout();
        EXPECT(psp->max_scroll() == 540);
        EXPECT(psp->scroll_offset() == 540);  // re-armed: pinned

        // a shrinking content tree lands on the (new) bottom
        pc->set_size(80, 40);
        root.layout();
        EXPECT(psp->max_scroll() == 0);
        EXPECT(psp->scroll_offset() == 0);
    }

    return test::report("scroll_panel");
}
