#include "test.hpp"

#include "imui.hpp"

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

    zb::input::input_event key_down(const int key)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.key = key;
        return ev;
    }

    zb::input::input_event char_down(const char ch)
    {
        zb::input::input_event ev;
        ev.type = zb::input::input_type::key_down;
        ev.ch = static_cast<int>(ch);
        return ev;
    }
}  // namespace

// the descriptive tree mirrors the fluent builder calls
int test_builder()
{
    // node DOM: tags, ordered props, ids, children, flex hints
    {
        auto doc = column({
            label("Level").named("lvl"),
            slider(50, 200).step(10).named("sld"),
            checkbox("Turbo").checked(true).named("turbo"),
            list_box({"A", "B", "C"}, 2).named("lst"),
            row({
                radio("X", 1).named("rx"),
                radio("Y", 1).checked(true).named("ry"),
            }).named("bar").flex(2),
        }).spacing(4).padding(2);

        EXPECT(doc.type == "column");
        EXPECT(doc.children.size() == 5);

        EXPECT(doc.children[0].type == "label");
        EXPECT(test::vget<std::string>(doc.children[0].props[0].second) == "Level");
        EXPECT(doc.children[0].id == "lvl");
        EXPECT(doc.children[0].flex_grow == 0);

        EXPECT(doc.children[1].type == "slider");
        EXPECT(test::vget<long long>(doc.children[1].props[0].second) == 50);
        EXPECT(test::vget<long long>(doc.children[1].props[1].second) == 200);
        EXPECT(test::vget<long long>(doc.children[1].props[2].second) == 10);
        EXPECT(doc.children[1].id == "sld");

        EXPECT(doc.children[2].type == "checkbox");
        EXPECT(test::vget<bool>(doc.children[2].props[1].second));

        EXPECT(doc.children[3].type == "list_box");
        EXPECT(doc.children[3].items.size() == 3);
        EXPECT(doc.children[3].items[2] == "C");
        EXPECT(test::vget<long long>(doc.children[3].props[0].second) == 2);

        const auto &bar = doc.children[4];
        EXPECT(bar.type == "row");
        EXPECT(bar.flex_grow == 2);
        EXPECT(bar.children.size() == 2);
        EXPECT(bar.children[0].type == "radio");
        EXPECT(test::vget<long long>(bar.children[0].props[1].second) == 1);
        EXPECT(bar.children[0].id == "rx");
        EXPECT(test::vget<bool>(bar.children[1].props[2].second));  // checked

        EXPECT(test::vget<long long>(doc.props[0].second) == 4);  // spacing
        EXPECT(test::vget<long long>(doc.props[1].second) == 2);  // padding
    }

    // materialize into a flex host: tree shape, ids, property landing
    {
        auto doc = column({
            label("Level").named("lvl"),
            slider(50, 200).named("sld"),
            checkbox("Turbo").checked(true).named("turbo"),
            list_box({"A", "B", "C"}, 2).named("lst"),
            row({radio("X", 1).named("rx"), radio("Y", 1).checked(true).named("ry")}),
        }).spacing(0);
        FlexPanel host;
        host.set_size(300, 240);
        build(host, doc);
        host.layout();

        EXPECT(host.get_items().size() == 5);

        auto *lvl = host.find_by_id("lvl");
        auto *sld = static_cast<Slider *>(host.find_by_id("sld"));
        auto *turbo = static_cast<Checkbox *>(host.find_by_id("turbo"));
        auto *lst = static_cast<ListBox *>(host.find_by_id("lst"));
        auto *rx = static_cast<RadioButton *>(host.find_by_id("rx"));
        auto *ry = static_cast<RadioButton *>(host.find_by_id("ry"));
        EXPECT(lvl != nullptr && lvl->get_id() == "lvl");
        EXPECT(lvl->get_text() == u"Level");
        EXPECT(sld->get_value() == 50);  // range clamps the initial value
        EXPECT(turbo->is_checked());
        EXPECT(lst->get_size().height == 32);  // 2 rows
        EXPECT(rx->get_group() == 1 && !rx->is_checked());
        EXPECT(ry->is_checked());
        EXPECT(host.find_by_id("missing") == nullptr);

        // ids widened by the materialize step (fresh tree per call)
        auto *lvl2 = host.find_by_id("lvl");
        EXPECT(lvl2 == lvl);
        // keep the pointer live: materialize drops the temporary handles
        EXPECT(lvl != nullptr);
    }

    // a declared 0 is an explicit value, not an omission (batch K / N8)
    {
        ui_node n;
        n.type = "label";
        n.id = "z";
        n.prop("width", 0LL).prop("height", 20LL);
        n.prop("pos_x", 0LL).prop("pos_y", 5LL);
        ui_node doc;
        doc.type = "column";
        doc.children.push_back(std::move(n));

        Panel host;
        host.set_size(100, 100);
        build(host, doc);

        auto *z = host.find_by_id("z");
        EXPECT(z != nullptr);
        EXPECT(z->get_size().width == 0 && z->get_size().height == 20);
        EXPECT(z->is_size_explicit());
        EXPECT(z->get_position().x == 0 && z->get_position().y == 5);
    }

    // flex layout: spacing/padding/auto sizes land on the widgets
    {
        auto doc = column({
            label("L").named("l"),
            slider(0, 100).named("s"),
        }).spacing(2).padding(1);
        FlexPanel host;
        host.set_size(120, 80);
        build(host, doc);
        host.layout();

        // label 6x7, slider 100x20; column: y at padding(1) then spacing
        EXPECT(host.find_by_id("l")->get_position().y == 1);
        EXPECT(host.find_by_id("s")->get_position().y == 10);
        // auto sizes materialized (hit-testing works)
        EXPECT(host.find_by_id("l")->get_size().width == 6);
        EXPECT(host.find_by_id("s")->get_size().width == 100);
    }

    // panel host: linear layout with explicit spacing/padding
    {
        auto doc = panel();
        doc.spacing(2).padding(3);
        doc.children.push_back(checkbox("On").named("cb"));
        doc.children.push_back(checkbox("Go").named("go"));
        Panel host;
        host.set_size(200, 100);
        build(host, doc);
        host.layout();

        auto *cb = static_cast<Checkbox *>(host.find_by_id("cb"));
        auto *go = static_cast<Checkbox *>(host.find_by_id("go"));
        EXPECT(cb != nullptr && go != nullptr);
        EXPECT(cb->get_position().x == 3 && cb->get_position().y == 3);
        EXPECT(go->get_position().x == 3);  // vertical: x stays at padding
        EXPECT(go->get_position().y == 3 + cb->get_size().height + 2);
    }

    // end to end: materialized controls are live (inputs, events)
    {
        auto doc = column({
            checkbox("On").named("cb"),
            slider(0, 100).step(10).named("sld"),
        }).spacing(0);
        FlexPanel host;
        host.set_size(200, 60);
        build(host, doc);
        host.layout();

        auto *cb = static_cast<Checkbox *>(host.find_by_id("cb"));
        auto *sld = static_cast<Slider *>(host.find_by_id("sld"));
        EXPECT(cb != nullptr && sld != nullptr);

        int toggles = 0;
        cb->changed += [&](bool) { ++toggles; };
        EXPECT(cb->is_checked() == false);

        zb::ui::InputDispatcher d;
        EXPECT(d.dispatch(host, press_at(5, 3)));    // hit the checkbox
        EXPECT(d.dispatch(host, release_at(5, 3)));  // click toggles
        EXPECT(toggles == 1 && cb->is_checked());

        EXPECT(d.dispatch(host, press_at(10, 28)));  // hit the slider track
        EXPECT(sld->get_value() > 0);

        // arrow keys move by the declarative step: 5 -> 5+10
        // (slider has capture: refire on the focused widget)
        EXPECT(d.dispatch(host, key_down(static_cast<int>(zb::input::key_code::right))));
        EXPECT(sld->get_value() == 15);
    }

    // text_input: DOM + materialize + editable after build (B4)
    {
        auto doc = column({
            label("Name").named("lbl"),
            text_input("hello").named("name"),
        });
        FlexPanel host;
        host.set_size(200, 80);
        build(host, doc);
        host.layout();

        EXPECT(doc.children[1].type == "text_input");
        EXPECT(doc.children[1].props[0].first == "text");
        EXPECT(test::vget<std::string>(doc.children[1].props[0].second) == "hello");

        auto *ti = static_cast<TextInput *>(host.find_by_id("name"));
        EXPECT(ti != nullptr && ti->get_text() == u"hello");

        // the materialized widget is a live editor: type into it
        InputDispatcher d;
        d.dispatch(host, press_at(40, 12));  // click the far right of the input
        d.dispatch(host, char_down('!'));
        EXPECT(ti->get_text() == u"hello!");
    }
    
     // alignment attributes materialization
    {
        const std::pair<const char*, Widget::h_align> h_cases[] = {
            {"left", Widget::h_align::left},
            {"center", Widget::h_align::center},
            {"right", Widget::h_align::right},
        };
        for (const auto& c : h_cases) {
            auto doc = column({
                label("Test").named("l").prop("halign", std::string(c.first)),
            });
            FlexPanel host;
            host.set_size(100, 50);
            build(host, doc);
            host.layout();
            EXPECT(host.find_by_id("l")->get_h_align() == c.second);
        }

        const std::pair<const char*, Widget::v_align> v_cases[] = {
            {"top", Widget::v_align::top},
            {"center", Widget::v_align::center},
            {"bottom", Widget::v_align::bottom},
        };
        for (const auto& c : v_cases) {
            auto doc = column({
                label("Test").named("l").prop("valign", std::string(c.first)),
            });
            FlexPanel host;
            host.set_size(100, 50);
            build(host, doc);
            host.layout();
            EXPECT(host.find_by_id("l")->get_v_align() == c.second);
        }

    // V-5 step 3: the composition widgets ride the tag table like every
    // other tag -- fluent builder and .ui parser both land on them
    {
        auto doc = column({
            toggle(true).named("sw"),
            gauge(0, 200).named("gd").value(120),
            knob(0, 100).named("kn").value(40).step(5),
            trend().named("tr"),
        });
        FlexPanel host;
        host.set_size(300, 240);
        build(host, doc);
        host.layout();

        auto *sw = static_cast<ToggleSwitch *>(host.find_by_id("sw"));
        auto *gd = static_cast<GaugeDial *>(host.find_by_id("gd"));
        auto *kn = static_cast<Knob *>(host.find_by_id("kn"));
        auto *tr = static_cast<TrendLine *>(host.find_by_id("tr"));
        EXPECT(sw != nullptr && sw->is_checked());
        EXPECT(gd != nullptr && gd->get_min() == 0 && gd->get_max() == 200 &&
               gd->get_value() == 120);
        EXPECT(kn != nullptr && kn->get_min() == 0 && kn->get_max() == 100 &&
               kn->get_value() == 40 && kn->get_step() == 5);
        EXPECT(tr != nullptr && tr->get_count() == 0);
        // the widgets are live after materialize (measures, not the
        // protected focus query)
        EXPECT(sw->measure().width == 40 && gd->measure().height == 64);
    }

    // the same tags parse identically from a design file (the parser
    // and the fluent builders share the tag table, contract 4.10)
    {
        bool ok = false;
        zb::ui::ui_node tree = parse_ui_text(
            "column spacing=4\n"
            "  toggle id=\"sw\" checked=true\n"
            "  gauge id=\"gd\" min=0 max=200 value=120\n"
            "  knob id=\"kn\" min=0 max=100 value=40 step=5\n",
            &ok);
        EXPECT(ok);
        FlexPanel host;
        host.set_size(300, 240);
        build(host, tree);
        host.layout();
        auto *sw = static_cast<ToggleSwitch *>(host.find_by_id("sw"));
        auto *gd = static_cast<GaugeDial *>(host.find_by_id("gd"));
        auto *kn = static_cast<Knob *>(host.find_by_id("kn"));
        EXPECT(sw != nullptr && sw->is_checked());
        EXPECT(gd != nullptr && gd->get_value() == 120);
        EXPECT(kn != nullptr && kn->get_value() == 40 && kn->get_step() == 5);
    }

    // a one-axis size declaration keeps the other axis's own measure
    // (regression: width= only used to force the height to an explicit
    // 0 -- buttons and sliders collapsed flat and became unclickable)
    {
        bool ok = false;
        zb::ui::ui_node tree = parse_ui_text(
            "column spacing=4\n"
            "  button id=\"bw\" text=\"SELF-CHECK\" width=68\n"
            "  slider id=\"sh\" height=20\n",
            &ok);
        EXPECT(ok);
        FlexPanel host;
        host.set_size(300, 240);
        build(host, tree);
        host.layout();
        auto *bw = static_cast<Button *>(host.find_by_id("bw"));
        auto *sh = static_cast<Slider *>(host.find_by_id("sh"));
        EXPECT(bw != nullptr && sh != nullptr);
        EXPECT(bw->get_size().width == 68);
        EXPECT(bw->get_size().height > 0);      // auto text height, not 0
        EXPECT(bw->is_width_explicit() && !bw->is_height_explicit());
        EXPECT(sh->get_size().height == 20);
        EXPECT(sh->get_size().width > 0);       // auto track width, not 0
        EXPECT(!sh->is_width_explicit() && sh->is_height_explicit());
    }

    // host root node is documentation only... panel() children flow
    return test::report("builder");
    }
}
