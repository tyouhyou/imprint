#include "test.hpp"

#include "imui.hpp"

#include "ui_embed_test.gen.hpp"  // packed by ui_embed at build time

using namespace zb::ui;

namespace
{
    zb::input::input_event press_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_down;
        ev.x = x;
        ev.y = y;
        return ev;
    }

    zb::input::input_event release_at(const int x, const int y)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::mouse_left_up;
        ev.x = x;
        ev.y = y;
        return ev;
    }
}  // namespace

// the .ui document format: tolerant parsing, explicit key=values only
int test_ui_file()
{
    // full document: container root, props, ids, items, kinds
    {
        const char *doc = R"(
# main menu
column id="main" spacing=4 padding=8
  label id="title" text="Welcome"
  button id="start" text="Start"
  slider min=0 max=100 step=5
  list_box rows=3 items="item one" "item two"
)";
        bool ok = false;
        ui_node root = parse_ui_text(doc, &ok);
        EXPECT(ok);
        EXPECT(root.type == "column");  // single container root kept as root
        EXPECT(root.id == "main");
        EXPECT(root.children.size() == 4);

        EXPECT(root.props.size() == 2);
        EXPECT(root.props[0].first == "spacing");
        EXPECT(std::get<long long>(root.props[0].second) == 4);
        EXPECT(root.props[1].first == "padding");
        EXPECT(std::get<long long>(root.props[1].second) == 8);

        const auto &title = root.children[0];
        EXPECT(title.type == "label");
        EXPECT(title.id == "title");
        EXPECT(std::get<std::string>(title.props[0].second) == "Welcome");

        const auto &slider = root.children[2];
        EXPECT(slider.type == "slider");
        EXPECT(std::get<long long>(slider.props[0].second) == 0);
        EXPECT(std::get<long long>(slider.props[1].second) == 100);
        EXPECT(std::get<long long>(slider.props[2].second) == 5);

        const auto &list = root.children[3];
        EXPECT(list.type == "list_box");
        EXPECT(list.items.size() == 2);
        EXPECT(list.items[0] == "item one");
        EXPECT(list.items[1] == "item two");
        EXPECT(std::get<long long>(list.props[0].second) == 3);
    }

    // multiple top-level nodes -> pseudo root
    {
        ui_node r = parse_ui_text(
            "label text=\"a\"\n"
            "button text=\"b\"\n",
            nullptr);
        EXPECT(r.type == "root");
        EXPECT(r.children.size() == 2);
        EXPECT(r.children[0].type == "label");
        EXPECT(r.children[1].type == "button");
    }

    // property kinds: int / bool / negative numbers / escaped quotes
    {
        ui_node r = parse_ui_text(
            "panel pos_x=-50 width=100 height=30 visible=false\n"
            "  checkbox checked=true text=\"say \\\"hi\\\"\"\n"
            "  radio group=7\n"
            "  column wrap=true flex=2\n",
            nullptr);
        // single container root (panel) keeps its children
        EXPECT(r.type == "panel");
        EXPECT(r.children.size() == 3);

        const auto &p = r;
        EXPECT(std::get<long long>(p.props[0].second) == -50);
        EXPECT(std::get<long long>(p.props[1].second) == 100);
        EXPECT(std::get<long long>(p.props[2].second) == 30);
        EXPECT(std::get<bool>(p.props[3].second) == false);

        const auto &cb = r.children[0];
        EXPECT(std::get<bool>(cb.props[0].second) == true);
        EXPECT(std::get<std::string>(cb.props[1].second) == "say \"hi\"");

        const auto &rad = r.children[1];
        EXPECT(std::get<long long>(rad.props[0].second) == 7);

        const auto &col = r.children[2];
        EXPECT(col.flex_grow == 2);
        EXPECT(std::get<bool>(col.props[0].second) == true);  // wrap
    }

    // tolerant: bare token dropped, bad value dropped, id takes string only
    {
        ui_node r = parse_ui_text(
            "label text=Welcome id=7\n"  // bare token values: dropped
            "button \"raw\" text=\"Go\"\n",
            nullptr);
        // the 'label' line survives with no props; 'button' keeps only
        // its key=value pair (the bare leading token is dropped)
        EXPECT(r.children.size() == 2);
        EXPECT(r.children[0].type == "label");
        EXPECT(r.children[0].props.empty());
        EXPECT(r.children[0].id.empty());
        EXPECT(r.children[1].type == "button");
        EXPECT(std::get<std::string>(r.children[1].props[0].second) == "Go");
    }

    // empty / comment-only document: ok == false
    {
        bool ok = true;
        ui_node r = parse_ui_text("# nothing\n\n", &ok);
        EXPECT(!ok);
        EXPECT(r.children.size() == 0);
    }

    // nesting by indentation; children of non-container tags are legal
    // in the file (the materializer drops them with a warning)
    {
        ui_node r = parse_ui_text(
            "column\n"
            "  label text=\"a\"\n"
            "    label text=\"b\"\n",  // deeper level: child of a
            nullptr);
        EXPECT(r.type == "column");
        EXPECT(r.children.size() == 1);
        EXPECT(r.children[0].children.size() == 1);
        EXPECT(std::get<std::string>(r.children[0].children[0].props[0].second) == "b");
    }

    // end-to-end: parse then materialize into a live tree
    {
        ui_node root = parse_ui_text(
            "column spacing=4\n"
            "  checkbox id=\"cb\" text=\"On\"\n"
            "  slider id=\"sld\" min=0 max=100 step=10\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 60);
        build(host, root);
        host.layout();

        auto *cb = static_cast<Checkbox *>(host.find_by_id("cb"));
        auto *sld = static_cast<Slider *>(host.find_by_id("sld"));
        EXPECT(cb != nullptr && sld != nullptr);
        EXPECT(cb->get_text() == u"On");
        EXPECT(sld->get_value() == 0);

        zb::ui::InputDispatcher d;
        EXPECT(d.dispatch(host, press_at(5, 3)));
        EXPECT(d.dispatch(host, release_at(5, 3)));
        EXPECT(cb->is_checked());
    }

    // the ui_embed-packed document parses back identically
    {
        EXPECT(sizeof(kUiFiles) / sizeof(kUiFiles[0]) == 1);
        const embedded_ui_file &f = kUiFiles[0];
        EXPECT(f.data != nullptr && f.size > 0);

        bool ok = false;
        ui_node root = parse_ui_text(reinterpret_cast<const char *>(f.data), &ok);
        EXPECT(ok);
        EXPECT(root.type == "column");
        EXPECT(root.children.size() == 5);

        EXPECT(find_ui_file(kUiFiles[0].name) == &kUiFiles[0]);
        EXPECT(find_ui_file("no_such.ui") == nullptr);
    }

    return test::report("ui_file");
}