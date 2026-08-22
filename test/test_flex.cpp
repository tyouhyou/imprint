#include "test.hpp"

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

    return test::report("flex");
}
