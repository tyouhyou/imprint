#include "test.hpp"

#include <memory>

#include "imapp.hpp"
#include "imui.hpp"

using namespace zb::ui;
using namespace zb::app;

namespace
{
    // counts its own layout() runs: the window must auto-layout a dirty
    // tree exactly once per paint
    struct LayoutProbe : FlexPanel
    {
        int layouts = 0;

        void layout() override
        {
            ++layouts;
            FlexPanel::layout();
        }
    };
}  // namespace

int test_layout_dirty()
{
        // auto layout is opt-in (set_auto_layout): a default window
        // keeps explicitly placed children untouched
        {
            CanvasWindow w;
            w.create(200, 150);
            auto &root = w.root();
            auto probe = std::make_unique<LayoutProbe>();
            probe->set_direction(LayoutProbe::flex_direction::column);
            probe->set_size(200, 120);
            probe->set_position(20, 10);
            auto *pp = probe.get();
            auto label = std::make_unique<Label>();
            label->set_text("AB");  // auto: 12x7
            label->set_position(5, 3);
            auto *lp = label.get();
            probe->add_child(std::move(label));
            root.add_child(std::move(probe));

            w.paint();  // no layout: the explicit positions survive
            EXPECT(pp->layouts == 0);
            EXPECT(pp->get_position().x == 20 && pp->get_position().y == 10);
            EXPECT(lp->get_position().x == 5 && lp->get_position().y == 3);
        }
        // a fresh tree with auto layout on lays itself out on the first
        // paint; an unchanged tree runs no further layout
        {
            CanvasWindow w;
            w.create(200, 150);
            w.set_auto_layout(true);
            auto &root = w.root();
            auto probe = std::make_unique<LayoutProbe>();
            probe->set_direction(LayoutProbe::flex_direction::column);
            probe->set_size(200, 120);
            auto *pp = probe.get();
            auto label = std::make_unique<Label>();
            label->set_text("AB");  // auto: 12x7
            auto *lp = label.get();
            probe->add_child(std::move(label));
            root.add_child(std::move(probe));

            w.paint();  // the first paint laid the tree out
            EXPECT(pp->layouts == 1);
            EXPECT(lp->get_size().width == 12);
            EXPECT(lp->get_size().height == 7);
            EXPECT(lp->get_position().x == 0);
            EXPECT(lp->get_position().y == 0);
        }
        {
            CanvasWindow w;
            w.create(200, 150);
            w.set_auto_layout(true);
            auto &root = w.root();
            auto probe = std::make_unique<LayoutProbe>();
            probe->set_direction(LayoutProbe::flex_direction::column);
            probe->set_size(200, 120);
            auto *pp = probe.get();
            auto label = std::make_unique<Label>();
            label->set_text("AB");  // auto: 12x7
            auto *lp = label.get();
            probe->add_child(std::move(label));
            root.add_child(std::move(probe));

            w.paint();
            EXPECT(pp->layouts == 1);
            w.paint();  // nothing changed: no layout runs
            EXPECT(pp->layouts == 1);

            // explicit geometry change: relayout happens in the paint,
            // not before, and exactly once
            pp->set_size(300, 120);
            w.paint();
            EXPECT(pp->layouts == 2);
            EXPECT(lp->get_size().width == 12);  // content unchanged

            // intrinsic change (text): relayout picks up the new measure
            lp->set_text("ABCDEFGH");  // auto: 48x7
            w.paint();
            EXPECT(pp->layouts == 3);
            EXPECT(lp->get_size().width == 48);
            EXPECT(lp->get_size().height == 7);

            w.paint();  // settled again
            EXPECT(pp->layouts == 3);
        }
        {
            // a manual layout call stays valid: the window's auto-layout
            // is idempotent with it (same tree, no double work)
            CanvasWindow w;
            w.create(200, 150);
            w.set_auto_layout(true);
            auto &root = w.root();
            auto probe = std::make_unique<LayoutProbe>();
            probe->set_direction(LayoutProbe::flex_direction::column);
            probe->set_size(200, 120);
            auto *pp = probe.get();
            auto label = std::make_unique<Label>();
            label->set_text("AB");
            auto *lp = label.get();
            probe->add_child(std::move(label));
            root.add_child(std::move(probe));

            w.paint();  // window laid it out
            EXPECT(pp->layouts == 1);
            root.layout();  // host code calls it again: no-op hierarchy
            EXPECT(pp->layouts == 2);
            EXPECT(lp->get_size().width == 12);  // geometry unchanged
            w.paint();  // and the window does not re-run it
            EXPECT(pp->layouts == 2);
        }

        // runtime mutation: a child added after the first paint joins
        // the layout on the next paint, and a removal re-lays the rest
        {
            CanvasWindow w;
            w.create(200, 150);
            w.set_auto_layout(true);
            auto &root = w.root();
            auto panel = std::make_unique<Panel>();
            panel->set_size(100, 60);
            auto *pp = panel.get();
            auto a = std::make_unique<Label>();
            a->set_text("AB");
            a->set_size(12, 7);
            auto *pa = a.get();
            panel->add_child(std::move(a));
            root.add_child(std::move(panel));
            w.paint();
            EXPECT(pa->get_position().x == 0 && pa->get_position().y == 0);

            auto b = std::make_unique<Label>();
            b->set_text("CD");
            b->set_size(12, 7);
            auto *pb = b.get();
            pp->add_child(std::move(b));  // mutated after the first paint
            w.paint();
            EXPECT(pa->get_position().x == 0 && pa->get_position().y == 0);
            EXPECT(pb->get_position().x == 0 && pb->get_position().y == 7);  // 0 + 7 + 0

            pp->remove_child(pb);
            w.paint();
            EXPECT(pa->get_position().x == 0 && pa->get_position().y == 0);
        }
        // runtime mutation: Panel spacing / padding / orientation after
        // the first paint are picked up by the next paint
        {
            CanvasWindow w;
            w.create(200, 150);
            w.set_auto_layout(true);
            auto &root = w.root();
            auto panel = std::make_unique<Panel>();
            panel->set_size(100, 60);
            auto *pp = panel.get();
            auto a = std::make_unique<Label>();
            a->set_text("AB");
            a->set_size(12, 7);
            auto b = std::make_unique<Label>();
            b->set_text("CD");
            b->set_size(12, 7);
            auto *pa = a.get();
            auto *pb = b.get();
            panel->add_child(std::move(a));
            panel->add_child(std::move(b));
            root.add_child(std::move(panel));
            w.paint();
            EXPECT(pa->get_position().y == 0);
            EXPECT(pb->get_position().y == 7);

            pp->set_spacing(3);
            w.paint();
            EXPECT(pb->get_position().y == 10);  // 0 + 7 + 3

            pp->set_padding(2);
            w.paint();
            EXPECT(pa->get_position().x == 2 && pa->get_position().y == 2);
            EXPECT(pb->get_position().y == 12);  // 2 + 7 + 3

            pp->set_orientation(Panel::orientation::horizontal);
            w.paint();
            EXPECT(pa->get_position().x == 2 && pa->get_position().y == 2);
            EXPECT(pb->get_position().x == 17 && pb->get_position().y == 2);  // 2 + 12 + 3
        }
        // runtime mutation: FlexPanel spacing / direction / add_child
        // after the first paint (same protocol as Panel; sizes are
        // materialized by the flex layout itself)
        {
            CanvasWindow w;
            w.create(200, 150);
            w.set_auto_layout(true);
            auto &root = w.root();
            auto flex = std::make_unique<FlexPanel>();
            flex->set_size(200, 120);
            auto *ff = flex.get();
            auto a = std::make_unique<Label>();
            a->set_text("AB");  // auto: 12x7
            auto *pa = a.get();
            flex->add_child(std::move(a));
            root.add_child(std::move(flex));
            w.paint();
            EXPECT(pa->get_position().x == 0 && pa->get_position().y == 0);

            ff->set_spacing(6);
            auto b = std::make_unique<Label>();
            b->set_text("CD");  // auto: 12x7
            auto *pb = b.get();
            ff->add_child(std::move(b));  // `ff` outlives the moved-from `flex`
            w.paint();
            EXPECT(pa->get_position().x == 0 && pa->get_position().y == 0);
            EXPECT(pb->get_position().x == 0 && pb->get_position().y == 13);  // 7 + 6

            ff->set_direction(FlexPanel::flex_direction::row);
            w.paint();
            EXPECT(pa->get_position().x == 0 && pa->get_position().y == 0);
            EXPECT(pb->get_position().x == 18 && pb->get_position().y == 0);  // 12 + 6
        }

        return test::report("layout_dirty");
}