#include "test.hpp"

#include "test_alloc_count.hpp"

#include "imapp.hpp"
#include "imui.hpp"
#if IMUI_HAS_UI_FILE
#include "ui_file.hpp"
#endif

using namespace zb::app;
using namespace zb::ui;

namespace
{
    struct Tree
    {
        Panel root;
        GroupBox *group = nullptr;

        Tree()
        {
            root.set_size(200, 150);
            auto g = std::make_unique<GroupBox>();
            g->set_size(150, 100);
            g->set_position(10, 10);
            g->set_title("Settings");
            group = g.get();
            root.add_child(std::move(g));
        }
    };

}
int test_group_box()
{
    // defaults and intrinsic size
    {
        GroupBox g;
        EXPECT(g.get_title().empty());
        EXPECT(g.measure().width == 120 && g.measure().height == 80);
    }

    // title updates and layout dirtying
    {
        GroupBox g;
        g.set_title("Advanced");
        EXPECT(g.get_title() == "Advanced");
    }

    // repaint gate: title changes trigger damage
    {
        CanvasWindow w;
        w.create(200, 150);
        auto g = std::make_unique<GroupBox>();
        g->set_size(150, 100);
        g->set_position(10, 10);
        auto *group = g.get();
        w.root().add_child(std::move(g));
        w.paint();

        group->set_title("Preferences");
        w.paint();
        // expect layout/dirty region updates for container
        int rx = 0, ry = 0, rw = 0, rh = 0;
        EXPECT(w.dirty_region(rx, ry, rw, rh));
        EXPECT(rw > 0 && rh > 0);
    }

    // drawing and border probes against theme tokens
    {
        Tree t;
        core::Graphics g(200, 150, nullptr);
        t.root.draw(g);

        EXPECT(theme().border.pixel == test::pixel_at(g, 10, 14));
        bool title_pixel = false;
        for (int y = 10; y < 18; ++y)
        {
            for (int x = 18; x < 90; ++x)
            {
                if (theme().text.pixel == test::pixel_at(g, x, y))
                {
                    title_pixel = true;
                }
            }
        }
        EXPECT(title_pixel);
    }

    // the title gap leaves the widget's own background intact
    {
        GroupBox group;
        const auto custom_background = core::Color::from(12, 34, 56);
        group.set_size(150, 100);
        group.set_position(10, 10);
        group.set_title("Settings");
        group.set_background_color(custom_background);
        core::Graphics g(200, 150, nullptr);
        group.draw(g);

        EXPECT(custom_background.pixel == test::pixel_at(g, 16, 10));
    }

    // materialization: fluent builder and .ui text parsing
    {
        FlexPanel host;
        host.set_size(300, 200);
        auto gb_node = group_box("Connections").named("gb");
        gb_node.children.push_back(label("Wi-Fi"));
        const auto doc = column({std::move(gb_node)});
        build(host, doc);
        auto *gb = static_cast<GroupBox *>(host.find_by_id("gb"));
        EXPECT(gb != nullptr);
        EXPECT(gb->get_title() == "Connections");
        EXPECT(gb->get_items().size() == 1);
        EXPECT(gb->is_flex_container());
        EXPECT(gb->content_inset() == 8);
    }
#if IMUI_HAS_UI_FILE
    {
        FlexPanel host;
        host.set_size(300, 200);
        bool ok = false;
        const auto parsed = parse_ui_text(
            "column\n"
            "  group_box id=\"gb\" title=\"Network\" width=200 height=100\n"
            "    label text=\"Connected\"\n",
            &ok);
        EXPECT(ok);
        build(host, parsed);
        auto *gb = static_cast<GroupBox *>(host.find_by_id("gb"));
        EXPECT(gb != nullptr);
        EXPECT(gb->get_title() == "Network");
        EXPECT(gb->get_items().size() == 1);
    }
#endif
#if IMUI_HAS_HTML
    {
        bool ok = false;
        const auto doc = parse_html(
            "<body><group-box id=\"gb\" title=\"Telemetry\">\n"
            "  <label>LINK STABLE</label>\n"
            "</group-box></body>",
            &ok);
        EXPECT(ok);
        FlexPanel host;
        host.set_size(300, 200);
        build(host, doc);
        auto *gb = static_cast<GroupBox *>(host.find_by_id("gb"));
        EXPECT(gb != nullptr);
        EXPECT(gb->get_title() == "Telemetry");
        EXPECT(gb->get_items().size() == 1);
    }
#endif

    // zero-allocation steady state: title updates + repaints
    {
        CanvasWindow w;
        w.create(200, 150);
        auto g = std::make_unique<GroupBox>();
        g->set_size(150, 100);
        g->set_position(10, 10);
        auto *group = g.get();
        w.root().add_child(std::move(g));
        w.paint();

        test::scoped_alloc_count c;
        for (int i = 0; i < 5; ++i)
        {
            group->set_title("Test");
            w.paint();
        }
        EXPECT(c.delta() == 0);
    }

    return test::report("group_box");
}
