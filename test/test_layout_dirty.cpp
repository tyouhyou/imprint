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

        return test::report("layout_dirty");
}