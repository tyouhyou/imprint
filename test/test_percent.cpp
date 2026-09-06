#include "test.hpp"

#include "flex_panel.hpp"
#include "panel.hpp"
#include "ui_builder.hpp"
#include "ui_file.hpp"
#include "widget.hpp"

using namespace zb::ui;

namespace
{
    std::unique_ptr<Widget> fixed(const int w, const int h)
    {
        auto c = std::make_unique<Widget>();
        c->set_size(w, h);
        return c;
    }

    std::unique_ptr<Widget> pct_w(const int pct)
    {
        auto c = std::make_unique<Widget>();
        c->set_width_percent(pct);
        return c;
    }

    std::unique_ptr<Widget> pct_h(const int pct)
    {
        auto c = std::make_unique<Widget>();
        c->set_height_percent(pct);
        return c;
    }

    bool at(const Widget &w, const int x, const int y)
    {
        const auto p = w.get_position();
        return p.x == x && p.y == y;
    }
}

// percentage sizes (batch L-4): a FlexPanel resolves a percent child
// against its own content box at layout time; the declaration never
// becomes an explicit size
int test_percent()
{
    // main axis: the fixed sibling claims its space first, the percent
    // child takes its declared share of the content box, the leftover
    // stays free
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(200, 50);
        p.add_child(fixed(60, 20));
        auto c = pct_w(25);
        Widget *cw = c.get();
        p.add_child(std::move(c));
        p.layout();
        EXPECT(cw->get_size().width == 50);
        EXPECT(at(*cw, 60, 0));
        EXPECT(!cw->is_width_explicit());  // never becomes explicit
        EXPECT(cw->is_width_percent());
        EXPECT(cw->width_percent() == 25);
    }

    // two 50% siblings fill the line exactly
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(200, 50);
        auto a = pct_w(50);
        auto b = pct_w(50);
        Widget *aw = a.get();
        Widget *bw = b.get();
        p.add_child(std::move(a));
        p.add_child(std::move(b));
        p.layout();
        EXPECT(aw->get_size().width == 100);
        EXPECT(bw->get_size().width == 100);
        EXPECT(at(*bw, 100, 0));
    }

    // overflow: the shares are scaled into the remainder proportionally
    // to their percentages (60%+60% of 100 -> 50/50, exact sum)
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        auto a = pct_w(60);
        auto b = pct_w(60);
        Widget *aw = a.get();
        Widget *bw = b.get();
        p.add_child(std::move(a));
        p.add_child(std::move(b));
        p.layout();
        EXPECT(aw->get_size().width == 50);
        EXPECT(bw->get_size().width == 50);
    }

    // uneven overflow: 30%+90% of 100 -> weighted 25/75, the last takes
    // the leftover pixel so the line sums to the remainder
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        auto a = pct_w(30);
        auto b = pct_w(90);
        Widget *aw = a.get();
        Widget *bw = b.get();
        p.add_child(std::move(a));
        p.add_child(std::move(b));
        p.layout();
        EXPECT(aw->get_size().width == 25);
        EXPECT(bw->get_size().width == 75);
    }

    // fixed sibling + overflow: 40 fixed, then 50%+50% of the remaining
    // 60 -> 30/30 after the fixed child
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.add_child(fixed(40, 20));
        auto a = pct_w(50);
        auto b = pct_w(50);
        Widget *aw = a.get();
        Widget *bw = b.get();
        p.add_child(std::move(a));
        p.add_child(std::move(b));
        p.layout();
        EXPECT(aw->get_size().width == 30);
        EXPECT(bw->get_size().width == 30);
        EXPECT(at(*aw, 40, 0));
        EXPECT(at(*bw, 70, 0));
    }

    // column: the axes swap
    {
        FlexPanel p;
        p.set_size(50, 200);
        p.add_child(fixed(20, 60));
        auto c = pct_h(25);
        Widget *cw = c.get();
        p.add_child(std::move(c));
        p.layout();
        EXPECT(cw->get_size().height == 50);
        EXPECT(at(*cw, 0, 60));
        EXPECT(!cw->is_height_explicit());
    }

    // cross axis: resolves against the content-box cross size, no
    // sibling interaction; an explicit main size survives. The explicit
    // size comes first: set_size clears the percent declarations (the
    // last geometry setter on an axis wins)
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(200, 100);
        auto c = std::make_unique<Widget>();
        Widget *cw = c.get();
        cw->set_size(60, 0);
        cw->set_height_percent(50);
        p.add_child(std::move(c));
        p.layout();
        EXPECT(cw->get_size().width == 60);   // explicit main kept
        EXPECT(cw->get_size().height == 50);  // percent cross resolved
        EXPECT(cw->is_width_explicit());
        EXPECT(!cw->is_height_explicit());
    }

    // padding shrinks the base (percentages resolve against the content
    // box, not the widget bounds); spacing is claimed before the shares
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.set_padding(10);
        p.add_child(fixed(20, 10));
        auto c = pct_w(50);
        Widget *cw = c.get();
        p.add_child(std::move(c));
        p.layout();
        EXPECT(cw->get_size().width == 40);  // 50% of the 80-wide content box
        EXPECT(at(*cw, 30, 10));
    }
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.set_spacing(5);
        p.add_child(fixed(20, 10));
        p.add_child(fixed(20, 10));
        auto c = pct_w(50);
        Widget *cw = c.get();
        p.add_child(std::move(c));
        p.layout();
        // fixed 20+20 plus 2*5 spacing leaves exactly 50 -> desired fits
        EXPECT(cw->get_size().width == 50);
        EXPECT(at(*cw, 50, 0));
    }

    // integer math: the desired share floors (33% of 105 -> 34)
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(105, 50);
        auto c = pct_w(33);
        Widget *cw = c.get();
        p.add_child(std::move(c));
        p.layout();
        EXPECT(cw->get_size().width == 34);
    }

    // flex_grow and percentage are mutually exclusive, percentage wins:
    // the grow weight is excluded from the distribution entirely
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        auto a = pct_w(50);
        Widget *aw = a.get();
        p.add_child(std::move(a), 2);  // grow 2 must be ignored
        p.add_child(fixed(10, 10), 1);
        p.layout();
        EXPECT(aw->get_size().width == 50);  // its share, not a grow cut
        const auto &items = p.get_items();
        EXPECT(items[1].child->get_size().width == 50);  // full leftover
    }

    // flex items absorb what the percent children leave behind
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 50);
        p.add_child(fixed(30, 10));
        auto a = pct_w(25);
        Widget *aw = a.get();
        p.add_child(std::move(a));
        p.add_child(fixed(10, 10), 1);
        p.layout();
        EXPECT(aw->get_size().width == 25);
        const auto &items = p.get_items();
        EXPECT(items[2].child->get_size().width == 45);  // 100-30-25
        EXPECT(at(*items[2].child, 55, 0));
    }

    // re-layout re-resolves: shrinking the parent shrinks the percent
    // child and leaves the explicit sibling untouched
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(200, 50);
        p.add_child(fixed(40, 10));
        auto c = pct_w(25);
        Widget *cw = c.get();
        p.add_child(std::move(c));
        p.layout();
        EXPECT(cw->get_size().width == 50);
        p.set_size(120, 50);
        p.layout();
        EXPECT(cw->get_size().width == 30);
        EXPECT(!cw->is_width_explicit());
        EXPECT(p.get_items()[0].child->get_size().width == 40);
    }

    // measure(): a percent child contributes 0 on its percent axis, like
    // a flex item -- an auto-sized container does not grow for it
    {
        FlexPanel r;
        r.set_direction(FlexPanel::flex_direction::row);
        r.add_child(fixed(20, 10));
        r.add_child(pct_w(50));
        EXPECT(r.measure().width == 20);
        EXPECT(r.measure().height == 10);
        FlexPanel c;
        c.add_child(fixed(20, 10));
        c.add_child(pct_h(50));
        EXPECT(c.measure().height == 10);
    }

    // top-down resolution: a nested row's own percent size is written
    // before its layout() runs, so the inner percent child resolves
    // against the fresh size (25% of the row's resolved 100 = 25)
    {
        FlexPanel page;
        page.set_direction(FlexPanel::flex_direction::column);
        page.set_size(200, 200);
        auto row = std::make_unique<FlexPanel>();
        row->set_direction(FlexPanel::flex_direction::row);
        row->set_width_percent(50);
        auto c = pct_w(25);
        Widget *cw = c.get();
        row->add_child(std::move(c));
        auto *row_ptr = row.get();
        page.add_child(std::move(row));
        page.layout();
        EXPECT(row_ptr->get_size().width == 100);  // 50% of the page
        EXPECT(cw->get_size().width == 25);        // 25% of the fresh 100
    }

    // an auto-sized nested row cannot honor a percent child beside a
    // fixed sibling: the container shrinks to the fixed content (the
    // percent child contributes 0 to measure), the remainder is 0 and
    // the scaled share is 0
    {
        FlexPanel page;
        page.set_direction(FlexPanel::flex_direction::column);
        page.set_size(200, 200);
        auto row = std::make_unique<FlexPanel>();
        row->set_direction(FlexPanel::flex_direction::row);
        row->add_child(fixed(20, 10));
        auto c = pct_w(50);
        Widget *cw = c.get();
        row->add_child(std::move(c));
        auto *row_ptr = row.get();
        page.add_child(std::move(row));
        page.layout();
        EXPECT(row_ptr->get_size().width == 20);  // measure ignored the pct child
        EXPECT(cw->get_size().width == 0);        // scaled into the zero remainder
    }

    // wrap: the percent child participates in line breaking with its
    // desired size, then resolves against its own line
    {
        FlexPanel p;
        p.set_direction(FlexPanel::flex_direction::row);
        p.set_size(100, 60);
        p.set_wrap(true);
        p.add_child(fixed(70, 10));
        auto c = pct_w(50);
        Widget *cw = c.get();
        p.add_child(std::move(c));
        p.layout();
        EXPECT(cw->get_size().width == 50);
        EXPECT(at(*cw, 0, 10));  // wrapped to the second line
    }

    // setter clamps and clearing: >100 clamps to 100, <=0 clears,
    // set_size clears the declaration (the last setter wins)
    {
        Widget w;
        w.set_width_percent(150);
        EXPECT(w.width_percent() == 100);
        w.set_width_percent(-5);
        EXPECT(!w.is_width_percent());
        EXPECT(w.width_percent() == 0);
        w.set_height_percent(30);
        EXPECT(w.is_height_percent());
        w.set_size(10, 10);
        EXPECT(!w.is_height_percent());
        EXPECT(w.is_width_explicit() && w.is_height_explicit());
    }

    // outside a FlexPanel the declaration stays unresolved: the axis
    // keeps its current size
    {
        Panel p;
        p.set_size(100, 100);
        auto c = pct_w(50);
        Widget *cw = c.get();
        p.add_child(std::move(c));
        p.layout();
        EXPECT(cw->get_size().width == 0);
        EXPECT(cw->is_width_percent());
    }

    // .ui source: quoted and bare percent values land as the same "N%"
    // string prop and materialize into resolved sizes
    {
        const char *doc = "row id=\"page\"\n"
                          "  button id=\"a\" width=\"50%\" height=20\n"
                          "  button id=\"b\" width=25% height=20\n";
        bool ok = false;
        ui_node root = parse_ui_text(doc, &ok);
        EXPECT(ok);
        const auto &a = root.children[0];
        EXPECT(test::vget<std::string>(a.props[0].second) == "50%");
        EXPECT(test::vget<long long>(a.props[1].second) == 20);
        const auto &b = root.children[1];
        EXPECT(test::vget<std::string>(b.props[0].second) == "25%");

        FlexPanel host;
        host.set_direction(FlexPanel::flex_direction::row);
        host.set_size(200, 40);
        build(host, root);
        host.layout();
        auto *wa = host.find_by_id("a");
        auto *wb = host.find_by_id("b");
        EXPECT(wa != nullptr && wb != nullptr);
        EXPECT(wa->get_size().width == 100);  // 50% of 200
        EXPECT(wa->get_size().height == 20);  // explicit pixels kept
        EXPECT(wb->get_size().width == 50);   // 25% of 200
        EXPECT(at(*wb, 100, 0));
    }

    // fluent builder: .width_pct/.height_pct produce the same percent form
    {
        ui_node doc = row({button("x").width_pct(50).height_pct(50)});
        FlexPanel host;
        host.set_size(200, 200);
        build(host, doc);
        host.layout();
        const auto &items = host.get_items();
        EXPECT(items.size() == 1);
        EXPECT(items[0].child->get_size().width == 100);
        EXPECT(items[0].child->get_size().height == 100);
    }

    return test::report("percent");
}
