#include "test.hpp"

#include "button.hpp"
#include "dispatcher.hpp"
#include "flex_panel.hpp"
#include "label.hpp"
#include "widget.hpp"

using namespace zb::ui;

namespace
{
    // text metrics are protected on Widget; a probe lifts them so the
    // H-1 stretch-down suite can assert the wrapped box geometry
    struct ProbeLabel : Label
    {
        using Label::text_advance;
        using Label::text_height;
    };

    std::unique_ptr<Widget> make_child(const int w, const int h)
    {
        auto c = std::make_unique<Widget>();
        c->set_size(w, h);
        return c;
    }

    bool at(const Widget &w, const int x, const int y)
    {
        const auto p = w.get_position();
        return p.x == x && p.y == y;
    }
}

int test_flex()
{
    // column: children stack top to bottom with spacing
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.add_child(make_child(10, 10));
        p.add_child(make_child(10, 20));
        p.add_child(make_child(10, 30));
        p.set_spacing(5);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 0, 15));
        EXPECT(at(*c[2].child, 0, 40));
    }

    // row: children stack left to right
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.add_child(make_child(10, 10));
        p.add_child(make_child(20, 10));
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 10, 0));
    }

    // an explicit cross-axis size survives a flex-grown main axis (Z5):
    // set_size(80, 40) + grow in a row keeps the height 40 across
    // relayouts (the old two-axis set_size_auto cleared both flags)
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(200, 100);
        auto c = std::make_unique<Widget>();
        c->set_size(80, 40);
        const Widget *cw = c.get();
        p.add_child(std::move(c), 1);
        p.layout();
        EXPECT(cw->get_size().width == 200);  // grown to fill the row
        EXPECT(cw->get_size().height == 40);  // explicit cross size kept
        p.layout();                           // and it survives relayout
        EXPECT(cw->get_size().height == 40);
    }

    // padding offsets every child
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.set_padding(5);
        p.add_child(make_child(10, 10));
        p.layout();
        EXPECT(at(*p.get_items()[0].child, 5, 5));
    }

    // per-side padding (html shorthand fold): the column content box
    // shrinks by each side and children start below the top inset
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.set_padding_sides(2, 4, 6, 8);
        p.add_child(make_child(10, 10));
        p.add_child(make_child(10, 10));
        p.layout();
        const auto &c = p.get_items();
        // column: horizontal insets left 8 / right 4, vertical top 2 /
        // bottom 6; children stack from the top inset
        EXPECT(at(*c[0].child, 8, 2));
        EXPECT(c[0].child->get_size().width == 10);
        EXPECT(at(*c[1].child, 8, 12));
        // measure includes the per-side sums: cross width 10 + 8 + 4,
        // main height 10 + 10 + 2 + 6
        EXPECT(p.measure().width == 22);
        EXPECT(p.measure().height == 28);
    }

    // flex grow: two flex children share the leftover space 1:1
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.add_child(make_child(10, 0), 1);
        p.add_child(make_child(10, 0), 1);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(c[0].child->get_size().height == 50);
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(c[1].child->get_size().height == 50);
        EXPECT(at(*c[1].child, 0, 50));
    }

    // flex grow 1:2 splits the leftover accordingly
    {
        FlexPanel p;
        p.set_size(50, 90);
        p.add_child(make_child(10, 0), 1);
        p.add_child(make_child(10, 0), 2);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(c[0].child->get_size().height == 30);
        EXPECT(c[1].child->get_size().height == 60);
    }

    // flex grow with a non-divisible leftover: integer division would drop
    // the remainder (3x33=99); the last flex item takes the leftover pixel
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.add_child(make_child(10, 0), 1);
        p.add_child(make_child(10, 0), 1);
        p.add_child(make_child(10, 0), 1);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(c[0].child->get_size().height == 33);
        EXPECT(c[1].child->get_size().height == 33);
        EXPECT(c[2].child->get_size().height == 34);
        EXPECT(at(*c[2].child, 0, 66));
    }

    // fixed child first: the flex child gets everything that is left
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.set_spacing(10);
        p.add_child(make_child(10, 20));
        p.add_child(make_child(10, 0), 1);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 0, 30));
        EXPECT(c[1].child->get_size().height == 70);
    }

    // wrap (row): overflowing children break to the next line
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(40, 100);
        p.add_child(make_child(20, 10));
        p.add_child(make_child(20, 10));
        p.add_child(make_child(20, 10));
        p.set_wrap(true);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 20, 0));
        EXPECT(at(*c[2].child, 0, 10));  // wrapped
    }

    // wrap + spacing: 20+5+20=45 exceeds the 50 width, so the third child
    // breaks to a new line below the first two
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(50, 100);
        p.set_spacing(5);
        p.add_child(make_child(20, 10));
        p.add_child(make_child(20, 10));
        p.add_child(make_child(20, 10));
        p.set_wrap(true);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 25, 0));
        EXPECT(at(*c[2].child, 0, 15));  // second line, below the first
    }

    // flex grow inside a wrapped line: a 40-wide fixed child forces the
    // second child to its own line, where a flex child gets the leftover
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(60, 100);
        p.add_child(make_child(40, 10));        // line 1, 20 left over
        p.add_child(make_child(40, 10));        // line 2, 20 left over
        p.add_child(make_child(20, 10), 1);
        p.set_wrap(true);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 0, 10));
        EXPECT(c[2].child->get_size().width == 20);  // line 2 leftover
        EXPECT(at(*c[2].child, 40, 10));             // same line, after c[1]
    }

    // intrinsic measure (contract §3, S3): content-derived size keeps
    // auto-sized containers hittable; flex items contribute 0 on the
    // main axis, spacing and padding included
    {
        FlexPanel r;
        r.set_direction(FlexPanel::flex_direction::row);
        r.set_spacing(5);
        r.set_padding(3);
        r.add_child(make_child(20, 10));
        r.add_child(make_child(20, 12));
        EXPECT(r.measure().width == 6 + 20 + 5 + 20);
        EXPECT(r.measure().height == 6 + 12);
        r.add_child(make_child(20, 10), 1);  // flex: no main contribution
        EXPECT(r.measure().width == 6 + 20 + 5 + 20 + 5);
        FlexPanel c;  // column: axes swap
        c.set_spacing(4);
        c.add_child(make_child(20, 10));
        c.add_child(make_child(30, 10));
        EXPECT(c.measure().width == 30 && c.measure().height == 10 + 4 + 10);
    }

    // a nested auto-sized row picks up its content size from measure
    // during the parent layout, so its children stay clickable
    {
        FlexPanel page;  // stand-in for a .ui column page
        page.set_direction(FlexPanel::flex_direction::column);
        page.set_size(200, 200);
        auto row = std::make_unique<FlexPanel>();
        row->set_direction(FlexPanel::flex_direction::row);
        auto *row_ptr = row.get();
        auto b = std::make_unique<Button>();
        b->set_text("X");
        auto *btn = b.get();
        row->add_child(std::move(b));
        page.add_child(std::move(row));
        page.layout();

        // the row got its content size, not 0x0
        EXPECT(row_ptr->get_size().width == btn->get_size().width);
        EXPECT(row_ptr->get_size().height == btn->get_size().height);

        // ...and the button inside is pickable by the dispatcher
        InputDispatcher d;
        const auto bp = btn->get_absolute_position();
        const auto bs = btn->get_size();
        zb::input::input_event down = {};
        down.type = zb::input::input_type::mouse_left_down;
        down.x = bp.x + bs.width / 2;
        down.y = bp.y + bs.height / 2;
        EXPECT(d.dispatch(page, down));
        EXPECT(d.get_focus_target() == btn);
    }

    // H-9 convergent passes: an auto-height ancestor fits an
    // aspect-derived child whose cross width only settles top-down in
    // the same pass (the .vu shape: row section > column cap > percent-
    // width aspect child). One layout() call converges — no second
    // paint needed — where the old single pass measured ~title only.
    {
        FlexPanel section;
        section.set_direction(FlexPanel::flex_direction::row);
        section.set_size(200, 200);
        auto cap = std::make_unique<FlexPanel>();
        auto title = std::make_unique<Widget>();
        title->set_size(40, 10);
        cap->add_child(std::move(title));
        auto vu = std::make_unique<FlexPanel>();
        vu->set_width_percent(100);
        vu->set_aspect_ratio(2, 1);
        auto inner = std::make_unique<Widget>();
        inner->set_size(20, 5);
        vu->add_child(std::move(inner));
        const Widget *vu_ptr = vu.get();
        cap->add_child(std::move(vu));
        const Widget *cap_ptr = cap.get();
        section.add_child(std::move(cap), 1);
        section.layout();
        EXPECT(vu_ptr->get_size().width == 200);
        EXPECT(vu_ptr->get_size().height == 100);
        EXPECT(cap_ptr->get_size().height == 110);
        // steady state stays single-pass: a relayout changes nothing
        section.layout();
        EXPECT(cap_ptr->get_size().height == 110);
    }

    // P-3 absolute positioning: the abs child leaves the flow (anchor
    // keeps label height only) and resolves against the anchor box —
    // the .vubottom shape (left/right/bottom + % height overlay)
    {
        FlexPanel anchor;
        anchor.set_size(200, 200);
        anchor.set_relative();
        auto title = std::make_unique<Widget>();
        title->set_size(200, 20);
        anchor.add_child(std::move(title));
        auto strip = std::make_unique<Widget>();
        strip->set_absolute();
        strip->set_abs_offset(0, 0, false);
        strip->set_abs_offset(2, 0, false);
        strip->set_abs_offset(3, 0, false);
        strip->set_height_percent(25);
        const Widget *strip_ptr = strip.get();
        anchor.add_child(std::move(strip));
        anchor.layout();
        EXPECT(strip_ptr->get_size().width == 200);
        EXPECT(strip_ptr->get_size().height == 50);
        const auto sp = strip_ptr->get_position();
        EXPECT(sp.x == 0 && sp.y == 150);
        // an auto-height anchor measures the flow only, not the overlay
        auto auto_anchor = std::make_unique<FlexPanel>();
        auto_anchor->set_relative();
        auto t2 = std::make_unique<Widget>();
        t2->set_size(60, 20);
        auto_anchor->add_child(std::move(t2));
        auto s2 = std::make_unique<Widget>();
        s2->set_absolute();
        s2->set_abs_offset(3, 0, false);
        s2->set_height_percent(50);
        auto_anchor->add_child(std::move(s2));
        FlexPanel root;
        root.set_size(200, 200);
        root.add_child(std::move(auto_anchor));
        root.layout();
        const auto &ritems = root.get_items();
        EXPECT(ritems[0].child->get_size().height == 20);
        // steady state: a relayout changes nothing
        anchor.layout();
        EXPECT(strip_ptr->get_size().height == 50);
        EXPECT(strip_ptr->get_position().y == 150);
    }

    // P-3 centering: left/top 50% of the anchor + translate(-50%,-50%)
    // of self lands centered (the .knob-dot shape)
    {
        FlexPanel knob;
        knob.set_size(54, 54);
        knob.set_relative();
        auto dot = std::make_unique<Widget>();
        dot->set_size(20, 20);
        dot->set_absolute();
        dot->set_abs_offset(0, 50, true);
        dot->set_abs_offset(1, 50, true);
        dot->set_translate(0, -50, true);
        dot->set_translate(1, -50, true);
        const Widget *dot_ptr = dot.get();
        knob.add_child(std::move(dot));
        knob.layout();
        EXPECT(dot_ptr->get_size().width == 20);
        const auto dp = dot_ptr->get_position();
        EXPECT(dp.x == 17 && dp.y == 17);
    }

    // P-3 abs explicitness survives H-9 re-passes for containers too:
    // an empty FlexPanel abs child keeps its declared size instead of
    // collapsing to its zero demand on the second pass (the real
    // .knob-dot collapse; Widget::measure defaults to size and hides
    // it, FlexPanel::measure does not)
    {
        FlexPanel knob;
        knob.set_size(54, 54);
        knob.set_relative();
        auto dot = std::make_unique<FlexPanel>();
        dot->set_size(20, 20);
        dot->set_absolute();
        dot->set_abs_offset(0, 50, true);
        dot->set_abs_offset(1, 50, true);
        dot->set_translate(0, -50, true);
        dot->set_translate(1, -50, true);
        const Widget *dot_ptr = dot.get();
        knob.add_child(std::move(dot));
        knob.layout();
        knob.layout();  // second full layout must not collapse either
        EXPECT(dot_ptr->get_size().width == 20);
        EXPECT(dot_ptr->get_size().height == 20);
        const auto dp = dot_ptr->get_position();
        EXPECT(dp.x == 17 && dp.y == 17);
    }

    // P-3 nested anchor: an abs child under a static intermediate
    // resolves against the positioned ancestor above, converted into
    // the parent's coordinates
    {
        FlexPanel card;
        card.set_size(300, 300);
        card.set_relative();
        auto mid = std::make_unique<FlexPanel>();
        mid->set_width_percent(100);
        auto badge = std::make_unique<Widget>();
        badge->set_size(40, 10);
        badge->set_absolute();
        badge->set_abs_offset(2, 5, false);
        badge->set_abs_offset(1, 5, false);
        const Widget *badge_ptr = badge.get();
        mid->add_child(std::move(badge));
        FlexPanel *mid_ptr = mid.get();
        card.add_child(std::move(mid));
        card.layout();
        // mid fills the card (only child, auto); badge hugs the card's
        // top-right corner through the intermediate
        const auto mp = mid_ptr->get_position();
        const auto bp = badge_ptr->get_position();
        const auto ms = mid_ptr->get_size();
        EXPECT(bp.x == mp.x + ms.width - 5 - 40);
        EXPECT(bp.y == mp.y + 5);
    }

    // H-7a justify: row of 3x10 in 100 with spacing 5 -> used 40,
    // free 60; each mode places the same sizes differently
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.set_spacing(5);
        p.add_child(make_child(10, 10));
        p.add_child(make_child(10, 10));
        p.add_child(make_child(10, 10));
        // default start: padding origin
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 15, 0));
        EXPECT(at(*c[2].child, 30, 0));
        // end: lead by the full free space
        p.set_justify_content(FlexPanel::justify::end);
        p.layout();
        EXPECT(at(*c[0].child, 60, 0));
        EXPECT(at(*c[1].child, 75, 0));
        EXPECT(at(*c[2].child, 90, 0));
        // center: lead by half (floor)
        p.set_justify_content(FlexPanel::justify::center);
        p.layout();
        EXPECT(at(*c[0].child, 30, 0));
        EXPECT(at(*c[1].child, 45, 0));
        EXPECT(at(*c[2].child, 60, 0));
        // space-between: k*free/(n-1) -> 0/30/60 on top of size+gap
        p.set_justify_content(FlexPanel::justify::space_between);
        p.layout();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 45, 0));
        EXPECT(at(*c[2].child, 90, 0));
        // space-around: (2k+1)*free/2n -> 10/50/90 edges included
        p.set_justify_content(FlexPanel::justify::space_around);
        p.layout();
        EXPECT(at(*c[0].child, 10, 0));
        EXPECT(at(*c[1].child, 45, 0));
        EXPECT(at(*c[2].child, 80, 0));
    }

    // H-7a edges: overflow falls back to start; a lone item under
    // space-between behaves as start; each wrapped line justifies alone
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(20, 50);
        p.set_justify_content(FlexPanel::justify::end);
        p.add_child(make_child(15, 10));
        p.add_child(make_child(15, 10));
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 15, 0));
    }
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.set_justify_content(FlexPanel::justify::space_between);
        p.add_child(make_child(10, 10));
        p.layout();
        EXPECT(at(*p.get_items()[0].child, 0, 0));
    }
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(50, 100);
        p.set_wrap(true);
        p.set_justify_content(FlexPanel::justify::center);
        p.add_child(make_child(20, 10));
        p.add_child(make_child(20, 10));
        p.add_child(make_child(20, 10));
        p.layout();
        // line one holds two (free 10 -> lead 5), line two centers alone
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 5, 0));
        EXPECT(at(*c[1].child, 25, 0));
        EXPECT(at(*c[2].child, 15, 10));
    }

    // H-7b align: row 100x50, single line fills the 50px content
    // cross box (H-7 A+B); center/end place within the real extent
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.add_child(make_child(10, 10));
        p.add_child(make_child(10, 20));
        p.add_child(make_child(10, 30));
        p.set_align_items(FlexPanel::align::center);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 20));
        EXPECT(at(*c[1].child, 10, 15));
        EXPECT(at(*c[2].child, 20, 10));
        p.set_align_items(FlexPanel::align::end);
        p.layout();
        EXPECT(at(*c[0].child, 0, 40));
        EXPECT(at(*c[1].child, 10, 30));
        EXPECT(at(*c[2].child, 20, 20));
    }

    // H-7b stretch: an auto-cross child grows to the filled line extent
    // (the content box for a single line); an explicit-cross sibling
    // keeps its size at the line top; relayout is stable (no H-9 drift)
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.set_align_items(FlexPanel::align::stretch);
        p.add_child(make_child(10, 20));
        auto inner = std::make_unique<FlexPanel>();
        inner->add_child(make_child(8, 10));
        FlexPanel *inner_ptr = inner.get();
        p.add_child(std::move(inner));
        p.layout();
        p.layout();
        const auto &c = p.get_items();
        EXPECT(c[0].child->get_size().height == 20);
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(inner_ptr->get_size().width == 8);
        EXPECT(inner_ptr->get_size().height == 50);
        EXPECT(at(*inner_ptr, 10, 0));
    }

    // H-7b self override: container stays start, one item centers
    // itself; column direction centers on x instead
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.add_child(make_child(10, 10), 0, FlexPanel::self_align::center);
        p.add_child(make_child(10, 30));
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 20));
        EXPECT(at(*c[1].child, 10, 0));
    }
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.set_align_items(FlexPanel::align::center);
        p.add_child(make_child(10, 10));
        p.add_child(make_child(20, 10));
        p.add_child(make_child(30, 10));
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 20, 0));
        EXPECT(at(*c[1].child, 15, 10));
        EXPECT(at(*c[2].child, 10, 20));
    }

    // H-7c flex-basis: explicit pixel basis overrides demand
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.add_child(make_child(10, 10), 0, FlexPanel::self_align::auto_, 0, 30); // basis_px = 30
        p.add_child(make_child(10, 10), 1);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(c[0].child->get_size().height == 30);  // basis wins
        EXPECT(c[1].child->get_size().height == 70);  // grow gets rest
    }

    // H-7c flex-basis: percent basis resolves against content box
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.add_child(make_child(10, 10), 0, FlexPanel::self_align::auto_, 0, -1, 50); // basis_pct = 50
        p.add_child(make_child(10, 10), 1);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(c[0].child->get_size().height == 50);  // 50% of 100
        EXPECT(c[1].child->get_size().height == 50);  // grow gets rest
    }

    // H-7c flex-basis: basis beats explicit size and percent child
    {
        FlexPanel p;
        p.set_size(50, 100);
        auto c1 = make_child(80, 10);  // explicit size 80
        c1->set_height_percent(100);   // percent also 100
        p.add_child(std::move(c1), 0, FlexPanel::self_align::auto_, 0, 30); // basis_px = 30
        p.add_child(make_child(10, 10), 1);
        p.layout();
        const auto &c = p.get_items();
        EXPECT(c[0].child->get_size().height == 30);  // basis wins over both
        EXPECT(c[1].child->get_size().height == 70);
    }

    // H-7c flex-shrink: deficit shared by shrink * claim
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.add_child(make_child(10, 60), 0, FlexPanel::self_align::auto_, 1); // claim 60, shrink 1
        p.add_child(make_child(10, 60), 0, FlexPanel::self_align::auto_, 2); // claim 60, shrink 2
        p.layout();
        const auto &c = p.get_items();
        // total claim = 120, deficit = 20
        // scaled: 1*60=60, 2*60=120, total=180
        // cuts: 20*60/180=6, remainder 20-6=14 to the last participant
        // final: 60-6=54, 60-14=46
        EXPECT(c[0].child->get_size().height == 54);
        EXPECT(c[1].child->get_size().height == 46); // 100 - 54
    }

    // H-7c flex-shrink: no shrink weight = historical overflow
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.add_child(make_child(10, 60));
        p.add_child(make_child(10, 60));
        p.layout();
        const auto &c = p.get_items();
        // No shrink: claims stand, line overflows
        EXPECT(c[0].child->get_size().height == 60);
        EXPECT(c[1].child->get_size().height == 60);
    }

    // H-3 margins: the main-axis pitch counts margin-before + size +
    // margin-after around every claim (no collapse with spacing)
    {
        FlexPanel p;
        p.set_size(100, 100);
        p.add_child(make_child(10, 10));
        auto m = make_child(10, 10);
        m->set_margin(12, 0, 0, 0);  // margin-top only
        p.add_child(std::move(m));
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 0, 0));
        EXPECT(at(*c[1].child, 0, 22));  // 10 + 12
    }
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_spacing(5);
        p.set_size(100, 100);
        auto m = make_child(10, 10);
        m->set_margin(0, 8, 0, 3);  // right 8, left 3
        p.add_child(std::move(m));
        p.add_child(make_child(10, 10));
        p.layout();
        const auto &c = p.get_items();
        EXPECT(at(*c[0].child, 3, 0));
        // 3 + 10 + 8 + spacing 5
        EXPECT(at(*c[1].child, 26, 0));
    }

    // H-3 margins: stretch fills the line minus the cross margins;
    // measure() counts margins so shrink-fit parents fit them
    {
        FlexPanel p;
        p.set_size(100, 100);
        p.set_align_items(FlexPanel::align::stretch);
        auto inner = std::make_unique<FlexPanel>();
        inner->add_child(make_child(8, 10));
        inner->set_margin(0, 10, 0, 10);
        FlexPanel *inner_ptr = inner.get();
        p.add_child(std::move(inner));
        p.layout();
        EXPECT(inner_ptr->get_size().width == 80);
        EXPECT(at(*inner_ptr, 10, 0));
    }
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        auto m = make_child(10, 10);
        m->set_margin(4, 0, 6, 0);
        p.add_child(std::move(m));
        // demand 10 + cross margins 4 + 6
        EXPECT(p.measure().height == 20);
        // main margins ride the measure too ( tested via column below )
        FlexPanel q;
        auto m2 = make_child(10, 10);
        m2->set_margin(5, 0, 7, 0);
        q.add_child(std::move(m2));
        EXPECT(q.measure().height == 22);
    }

    // H-7c flex-shrink: baseless grower collapses to 0 in deficit
    {
        FlexPanel p;
        p.set_size(50, 100);
        p.add_child(make_child(10, 40), 0, FlexPanel::self_align::auto_, 1); // claim 40, shrink 1
        p.add_child(make_child(10, 0), 1); // grower, no basis -> claim 0
        p.layout();
        const auto &c = p.get_items();
        // total claim = 40, deficit = -60 (surplus 60)
        // grower gets surplus over claim 0 -> 60
        EXPECT(c[0].child->get_size().height == 40);
        EXPECT(c[1].child->get_size().height == 60);
    }

    // H-7c: measure() counts px basis as demand, % basis as 0
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_padding(5);
        p.set_spacing(5);
        p.add_child(make_child(10, 10), 0, FlexPanel::self_align::auto_, 0, 30); // basis_px = 30
        p.add_child(make_child(10, 10), 0, FlexPanel::self_align::auto_, 0, -1, 50); // basis_pct = 50
        p.add_child(make_child(10, 10), 1); // flex grow
        // measure: 30 (px basis) + 0 (% basis) + 5 (spacing) + 5 (spacing) + 10 (padding*2) = 50
        // Wait, let's verify: main axis is row (width), so measure().width
        EXPECT(p.measure().width == 50); // 30 + 0 + 5 + 5 + 10 = 50
    }

    // H-1: a wrapping child under stretch is assigned the container's
    // content box (the block fill) instead of its natural single-line
    // width -- so a paragraph wraps at the box; convergence across the
    // stepped relayouts reads the wrapped height on the next round
    {
        auto mkpara = []() {
            auto l = std::make_unique<ProbeLabel>();
            l->set_text("one two three four five six seven eight nine ten");
            l->set_text_wrap(true);
            return l;
        };
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::column);
        p.set_padding(10);
        p.set_size(110, 80);
        p.set_align_items(FlexPanel::align::stretch);
        ProbeLabel *para = mkpara().release();
        p.add_child(std::unique_ptr<Widget>(para));
        p.add_child(make_child(10, 10));
        // stepped layout like the host loop (each round re-reads measure)
        for (int i = 0; i < 4; ++i)
        {
            p.layout();
        }
        const auto &c = p.get_items();
        // fill = 110 - 2*10 padding = 90 (< natural single-line width)
        EXPECT(para->get_size().width == 90);
        EXPECT(para->measure().height > para->text_height());
        EXPECT(para->wrap_spans(para->get_size().width).size() > 1);
        EXPECT(c[1].child->get_position().x == 10);

        ProbeLabel *dmd = mkpara().release();        // identical, but no wrap
        dmd->set_text_wrap(false);
        ProbeLabel &demand = *dmd;
        FlexPanel q;
        q.set_direction(FlexPanel::flex_direction::column);
        q.set_padding(10);
        q.set_size(110, 80);
        q.set_align_items(FlexPanel::align::stretch);
        q.add_child(std::unique_ptr<Widget>(&demand));
        q.add_child(make_child(10, 10));
        for (int i = 0; i < 4; ++i)
        {
            q.layout();
        }
        // a non-wrapping label is a stretch child too: its box caps at
        // the container content box (the block fill, H-9e) instead of
        // its natural single-line demand; wrap is off, so the text stays
        // single-line and merely overflows the box
        EXPECT(demand.get_size().width == 90);
        EXPECT(demand.text_advance() > 90);
        EXPECT(demand.measure().height == demand.text_height());
    }

    // H-9e: an auto-axis stretch child whose natural cross demand exceeds
    // the container is capped at the container's content box; it must not
    // drag the line extent (or its stretch siblings) past the layout box.
    // Regression: the demo's wrapping paragraph (1201px single-line demand)
    // ballooned the whole 360px column -- the tube/power modules stretched
    // to 1201 and overflowed, even though the column kept its width.
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::column);
        p.set_padding(10);
        p.set_size(110, 80);
        p.set_align_items(FlexPanel::align::stretch);
        auto wide = std::make_unique<ProbeLabel>();
        wide->set_text("one two three four five six seven eight nine ten");
        wide->set_text_wrap(false);  // natural single-line demand > 90
        ProbeLabel *widep = wide.get();
        auto sib = std::make_unique<ProbeLabel>();
        sib->set_text("hi");
        ProbeLabel *sibp = sib.get();
        p.add_child(std::move(wide));
        p.add_child(std::move(sib));
        for (int i = 0; i < 4; ++i)
        {
            p.layout();
        }
        const auto &c = p.get_items();
        // the wide child caps at the content box, not its 6-digit demand
        EXPECT(widep->get_size().width == 90);
        EXPECT(widep->text_advance() > 90);
        // the auto sibling stretch child never balloons to the wide
        // demand: it fills the same content box (H-9e regression: the
        // demo's tube/power modules stretched to 1201 and overflowed)
        EXPECT(sibp->get_size().width == 90);
        EXPECT(c[0].child->get_position().x == 10);
    }

    return test::report("flex");
}
