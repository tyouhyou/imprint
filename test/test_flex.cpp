#include "test.hpp"

#include "button.hpp"
#include "dispatcher.hpp"
#include "flex_panel.hpp"
#include "widget.hpp"

using namespace zb::ui;

namespace
{
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

    return test::report("flex");
}
