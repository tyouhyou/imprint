#include "test.hpp"

#include "imui.hpp"

using namespace zb::ui;

namespace
{
    // minimal concrete widget for geometry/rendering tests
    struct TestWidget : Widget
    {
        explicit TestWidget(const int w, const int h)
        {
            set_size(w, h);
        }

        void draw_at(core::Graphics &area) const override
        {
            area.fill(core::colors::Black);
        }
    };
}

int test_panel()
{
    // vertical layout: padding and spacing applied
    {
        Panel p;
        p.set_size(100, 100);
        p.set_padding(5);
        p.set_spacing(3);
        p.set_orientation(Panel::orientation::vertical);

        auto a = std::make_unique<TestWidget>(10, 10);
        auto b = std::make_unique<TestWidget>(10, 20);
        auto c = std::make_unique<TestWidget>(10, 30);
        auto *pa = a.get();
        auto *pb = b.get();
        auto *pc = c.get();
        p.add_child(std::move(a));
        p.add_child(std::move(b));
        p.add_child(std::move(c));
        p.layout();

        EXPECT(pa->get_position().x == 5 && pa->get_position().y == 5);
        EXPECT(pb->get_position().x == 5 && pb->get_position().y == 18);  // 5 + 10 + 3
        EXPECT(pc->get_position().x == 5 && pc->get_position().y == 41);  // 18 + 20 + 3
    }

    // horizontal layout
    {
        Panel p;
        p.set_size(100, 100);
        p.set_padding(2);
        p.set_spacing(4);
        p.set_orientation(Panel::orientation::horizontal);

        auto a = std::make_unique<TestWidget>(10, 10);
        auto b = std::make_unique<TestWidget>(20, 10);
        auto *pa = a.get();
        auto *pb = b.get();
        p.add_child(std::move(a));
        p.add_child(std::move(b));
        p.layout();

        EXPECT(pa->get_position().x == 2 && pa->get_position().y == 2);
        EXPECT(pb->get_position().x == 16 && pb->get_position().y == 2);  // 2 + 10 + 4
    }

    // nested panels: layout is recursive
    {
        Panel root;
        root.set_size(200, 200);

        auto sub = std::make_unique<Panel>();
        sub->set_size(100, 100);
        sub->set_padding(10);
        auto *psub = sub.get();

        auto leaf = std::make_unique<TestWidget>(10, 10);
        auto *pleaf = leaf.get();
        sub->add_child(std::move(leaf));

        root.add_child(std::move(sub));
        root.layout();

        EXPECT(psub->get_position().x == 0 && psub->get_position().y == 0);
        EXPECT(pleaf->get_position().x == 10 && pleaf->get_position().y == 10);
    }

    // hit test in widget-local coordinates
    {
        TestWidget w(10, 20);
        w.set_position(50, 60);
        w.set_size(10, 20);

        EXPECT(w.hit(0, 0));
        EXPECT(w.hit(9, 19));
        EXPECT(!w.hit(10, 19));
        EXPECT(!w.hit(-1, 0));
        EXPECT(!w.hit(0, -1));

        w.set_visible(false);
        EXPECT(!w.hit(0, 0));
        w.set_visible(true);
        EXPECT(w.hit(0, 0));
    }

    // draw: background filled in the panel's own area only
    {
        auto g = core::Graphics::make_ptr(8, 8);
        Panel p;
        p.set_position(1, 1);
        p.set_size(3, 3);
        p.set_background_color(core::colors::Red);
        p.draw(*g);

        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(*g, 0, 0) == 0);  // untouched
        EXPECT(test::pixel_at(*g, 4, 4) == 0);
    }

    // draw: child painted at its position relative to the parent
    {
        auto g = core::Graphics::make_ptr(8, 8);
        Panel p;
        p.set_position(1, 1);
        p.set_size(5, 5);
        p.set_background_color(core::colors::Red);

        auto child = std::make_unique<Panel>();
        child->set_position(2, 2);
        child->set_size(2, 2);
        child->set_background_color(core::colors::Blue);
        p.add_child(std::move(child));
        p.draw(*g);

        EXPECT(test::pixel_at(*g, 1, 1) == core::colors::Red.pixel);     // panel area
        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Blue.pixel);    // child area (1+2, 1+2)
        EXPECT(test::pixel_at(*g, 2, 2) == core::colors::Red.pixel);     // panel area around child
        EXPECT(test::pixel_at(*g, 0, 0) == 0);
    }

    // draw: fully off-screen child is skipped without throwing
    {
        auto g = core::Graphics::make_ptr(8, 8);
        Panel p;
        p.set_position(0, 0);
        p.set_size(4, 4);
        p.set_background_color(core::colors::Red);

        auto child = std::make_unique<Panel>();
        child->set_position(10, 10);
        child->set_size(2, 2);
        child->set_background_color(core::colors::Blue);
        p.add_child(std::move(child));
        p.draw(*g);

        EXPECT(test::pixel_at(*g, 0, 0) == core::colors::Red.pixel);  // no crash, no stray pixels
    }

    // draw: partially clipped child is drawn clipped
    {
        auto g = core::Graphics::make_ptr(8, 8);
        Panel p;
        p.set_position(0, 0);
        p.set_size(4, 4);
        p.set_background_color(core::colors::Red);

        auto child = std::make_unique<Panel>();
        child->set_position(3, 3);
        child->set_size(3, 3);
        child->set_background_color(core::colors::Blue);
        p.add_child(std::move(child));
        p.draw(*g);

        EXPECT(test::pixel_at(*g, 3, 3) == core::colors::Blue.pixel);   // visible corner of child
        EXPECT(test::pixel_at(*g, 5, 5) == 0);                          // clipped part not drawn
    }

    return test::report("panel");
}
