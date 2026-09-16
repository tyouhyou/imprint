#include "test.hpp"

#include "html.hpp"
#include "imui.hpp"

using namespace zb::ui;

namespace
{
    // index of the first prop named `name`, or -1
    int find_prop(const ui_node &n, const char *name)
    {
        for (std::size_t i = 0; i < n.props.size(); ++i)
        {
            if (n.props[i].first == name)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // the value of the first prop named `name`; a missing prop records a
    // failure and returns a static monostate instead of indexing [-1]
    const prop_value &node_prop_v(const ui_node &n, const char *name)
    {
        static const prop_value kNone{};
        const int i = find_prop(n, name);
        if (i >= 0)
        {
            return n.props[i].second;
        }
        ++test::failures;
        std::printf("FAIL (node_prop_v): missing prop '%s'\n", name);
        return kNone;
    }
}  // namespace

int test_html()
{
    // single top-level container becomes the document root; the CSS gap
    // lands as the .ui spacing prop on the root
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body>\n"
            "  <div style=\"gap:4\">\n"
            "    <button id=\"go\">GO</button>\n"
            "    <label>Status</label>\n"
            "  </div>\n"
            "</body>\n",
            &ok);
        EXPECT(ok);
        EXPECT(root.type == "column");
        EXPECT(root.children.size() == 2);
        EXPECT(test::vget<long long>(node_prop_v(root, "spacing")) == 4);
        EXPECT(root.children[0].type == "button");
        EXPECT(root.children[0].id == "go");
        EXPECT(test::vget<std::string>(node_prop_v(root.children[0], "text")) == "GO");
        EXPECT(root.children[1].type == "label");
        EXPECT(test::vget<std::string>(node_prop_v(root.children[1], "text")) == "Status");
    }

    // multiple top-level widgets -> pseudo root
    {
        ui_node r = parse_html("<body><label>a</label><button>b</button></body>", nullptr);
        EXPECT(r.type == "root");
        EXPECT(r.children.size() == 2);
        EXPECT(r.children[0].type == "label");
        EXPECT(r.children[1].type == "button");

        // a single non-container top-level widget stays under the root
        ui_node r2 = parse_html("<body><button>x</button></body>", nullptr);
        EXPECT(r2.type == "root");
        EXPECT(r2.children.size() == 1);
        EXPECT(r2.children[0].type == "button");
    }

    // text normalization: whitespace collapse + entity subset; unknown
    // entities stay literal
    {
        ui_node r = parse_html(
            "<label>  A &amp;&lt;B&gt;&quot;C&#39;  </label>", nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "text")) ==
               "A &<B>\"C'");

        ui_node r2 = parse_html("<label>R &copy; C</label>", nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r2.children[0], "text")) ==
               "R &copy; C");

        ui_node r3 = parse_html("<label>-0.482&nbsp;BAR</label>", nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r3.children[0], "text")) ==
               "-0.482 BAR");
    }

    // an off-whitelist element is skipped and drops its whole subtree;
    // checked + text on buttons/labels parse
    {
        ui_node r = parse_html(
            "<label>a</label>\n"
            "<widget><button>x</button></widget>\n"
            "<checkbox checked>On</checkbox>\n",
            nullptr);
        EXPECT(r.type == "root");
        EXPECT(r.children.size() == 2);
        EXPECT(r.children[0].type == "label");
        EXPECT(r.children[1].type == "checkbox");
        EXPECT(test::vget<bool>(node_prop_v(r.children[1], "checked")) == true);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[1], "text")) == "On");
    }

    // C2: br is void -- bare, slashed, spaced, and explicitly closed
    // forms are equivalent; a stray </br> is purely ignored
    {
        const char *spellings[] = {
            "<div>a<br>b</div>",
            "<div>a<br/>b</div>",
            "<div>a<br />b</div>",
            "<div>a<br></br>b</div>",
        };
        for (const char *doc : spellings)
        {
            ui_node r = parse_html(doc, nullptr);
            EXPECT(r.type == "column");
            EXPECT(r.children.size() == 3);
            EXPECT(r.children[0].type == "label");
            EXPECT(test::vget<std::string>(
                       node_prop_v(r.children[0], "text")) == "a");
            EXPECT(r.children[1].type == "label");
            EXPECT(test::vget<long long>(
                       node_prop_v(r.children[1], "height")) == 7);
            EXPECT(find_prop(r.children[1], "text") < 0);  // empty spacer
            EXPECT(test::vget<std::string>(
                       node_prop_v(r.children[2], "text")) == "b");
        }

        ui_node r = parse_html("<div>a</br>b</div>\n", nullptr);
        EXPECT(r.type == "column");  // </br> ignored: div stays open
        EXPECT(r.children.size() == 2);
        EXPECT(test::vget<std::string>(
                   node_prop_v(r.children[0], "text")) == "a");
        EXPECT(test::vget<std::string>(
                   node_prop_v(r.children[1], "text")) == "b");
    }

    // style rules: #id beats tag, inline beats #id, document order within
    // a bucket, unknown properties are ignored
    {
        bool ok = false;
        ui_node r = parse_html(
            "<style>\n"
            "  label { color: red; border-right: 2px; }\n"
            "  #hot { color: green; background-color: #0000ff; }\n"
            "  #hot2 { color: green; }\n"
            "</style>\n"
            "<label id=\"hot\" style=\"color:yellow\">H</label>\n"
            "<label id=\"hot2\">N</label>\n"
            "<label>P</label>\n",
            &ok);
        EXPECT(ok);
        EXPECT(r.children.size() == 3);

        const auto &hot = r.children[0];
        EXPECT(test::vget<std::string>(node_prop_v(hot, "color")) == "yellow");
        EXPECT(test::vget<std::string>(node_prop_v(hot, "background")) == "#0000ff");
        EXPECT(find_prop(hot, "border-right") < 0);  // ignored, not stored

        const auto &hot2 = r.children[1];
        EXPECT(test::vget<std::string>(node_prop_v(hot2, "color")) == "green");

        const auto &plain = r.children[2];
        EXPECT(test::vget<std::string>(node_prop_v(plain, "color")) == "red");

        // same-property document order within the id bucket: last wins
        ui_node r2 = parse_html(
            "<style>#x { height:10px; } #x { height:20px; }</style>\n"
            "<label id=\"x\">X</label>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r2.children[0], "height")) == 20);
    }

    // B5: !important lifts above every normal declaration; the bucket
    // order still applies inside the important tier
    {
        ui_node r = parse_html(
            "<style>\n"
            "  label { color: red !important; background-color: #111111; }\n"
            "  #h { color: green; }\n"
            "</style>\n"
            "<label id=\"h\" style=\"color: yellow\">A</label>\n"
            "<label id=\"h\" style=\"color: yellow !IMPORTANT\">B</label>\n"
            "<label>C</label>\n"
            "<label style=\"border: 1px !important\">D</label>\n"
            "<label style=\"color: blue ! important\">E</label>\n",
            nullptr);
        EXPECT(r.children.size() == 5);

        const auto &a = r.children[0];  // important tag > normal inline + id
        EXPECT(test::vget<std::string>(node_prop_v(a, "color")) == "red");
        EXPECT(test::vget<std::string>(node_prop_v(a, "background")) == "#111111");

        const auto &b = r.children[1];  // important inline > important tag
        EXPECT(test::vget<std::string>(node_prop_v(b, "color")) == "yellow");

        const auto &c = r.children[2];  // important tag applies plainly
        EXPECT(test::vget<std::string>(node_prop_v(c, "color")) == "red");

        const auto &d = r.children[3];  // unknown stays ignored, flag or not
        EXPECT(find_prop(d, "border") < 0);

        const auto &e = r.children[4];  // spaced marker tolerated
        EXPECT(test::vget<std::string>(node_prop_v(e, "color")) == "blue");
    }
    // container props and flex direction; display:none hides a column
    {
        ui_node r = parse_html(
            "<div style=\"flex-direction: row; gap: 3; padding: 2; flex-wrap: wrap\">\n"
            "  <div style=\"flex: 1\"><label>x</label></div>\n"
            "</div>\n",
            nullptr);
        EXPECT(r.type == "row");
        EXPECT(test::vget<long long>(node_prop_v(r, "spacing")) == 3);
        EXPECT(test::vget<long long>(node_prop_v(r, "padding")) == 2);
        EXPECT(test::vget<bool>(node_prop_v(r, "wrap")) == true);
        EXPECT(r.children[0].flex_grow == 1);
        ui_node r2 = parse_html(
            "<div style=\"display: none\"><label>x</label></div>\n", nullptr);
        EXPECT(r2.type == "column");  // single container unwraps to the root
        EXPECT(r2.children.size() == 1);
        EXPECT(r2.children[0].type == "label");
        EXPECT(test::vget<bool>(node_prop_v(r2, "visible")) == false);

        // H-7a justify-content: codes follow the FlexPanel::justify
        // ordinal; unknown values emit no prop (warn + keep)
        ui_node j1 = parse_html(
            "<div style=\"display: flex; justify-content: space-between\">\n"
            "  <label>x</label>\n"
            "</div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(j1, "justify")) == 3);
        ui_node j2 = parse_html(
            "<div style=\"display: flex; justify-content: SPACE-AROUND\">\n"
            "  <label>x</label>\n"
            "</div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(j2, "justify")) == 4);
        ui_node j3 = parse_html(
            "<div style=\"display: flex; justify-content: left\">\n"
            "  <label>x</label>\n"
            "</div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(j3, "justify")) == 0);
        ui_node j4 = parse_html(
            "<div style=\"display: flex; justify-content: stretch\">\n"
            "  <label>x</label>\n"
            "</div>\n",
            nullptr);
        EXPECT(find_prop(j4, "justify") < 0);

        // H-7b align-items maps to the container prop (FlexPanel::align
        // ordinal); align-self rides the child node; baseline warns and
        // emits nothing on either channel
        ui_node a1 = parse_html(
            "<div style=\"display: flex; align-items: center\">\n"
            "  <label style=\"align-self: flex-end\">x</label>\n"
            "</div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(a1, "align")) == 1);
        EXPECT(a1.children[0].align_self == 3);
        ui_node a2 = parse_html(
            "<div style=\"display: flex; align-items: baseline\">\n"
            "  <label style=\"align-self: middle\">x</label>\n"
            "</div>\n",
            nullptr);
        EXPECT(find_prop(a2, "align") < 0);
        EXPECT(a2.children[0].align_self == 0);
    }

    // B4: CSS keyword values are ASCII case-insensitive; ids are not
    {
        ui_node r = parse_html(
            "<div style=\"flex-direction: ROW; gap: 3; flex-wrap: WRAP\">\n"
            "  <label>x</label>\n"
            "</div>\n",
            nullptr);
        EXPECT(r.type == "row");
        EXPECT(test::vget<bool>(node_prop_v(r, "wrap")) == true);

        ui_node r2 = parse_html(
            "<div style=\"display: NONE\"><label>x</label></div>\n", nullptr);
        EXPECT(r2.type == "column");
        EXPECT(test::vget<bool>(node_prop_v(r2, "visible")) == false);

        ui_node r3 = parse_html(
            "<label style=\"width: 120PX; height: AUTO\">w</label>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r3.children[0], "width")) == 120);
        EXPECT(find_prop(r3.children[0], "height") < 0);
    }

    // B4 end-to-end: named colors resolve case-insensitively through the
    // shared color parser (uppercase TRANSPARENT stays a no-op). Note:
    // label text is element *content* in HTML, and size comes from the
    // style (width=/height= are not element attributes).
    {
        ui_node root = parse_html(
            "<label id=\"c\" style=\"width: 60px; height: 20px; color: RED; "
            "background-color: BLUE\">Hi</label>\n"
            "<label id=\"t\" style=\"width: 60px; height: 20px; "
            "background-color: TRANSPARENT\">x</label>\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 60);
        build(host, root);
        host.layout();
        auto *c = static_cast<Label *>(host.find_by_id("c"));
        auto *t = static_cast<Label *>(host.find_by_id("t"));
        EXPECT(c != nullptr && t != nullptr);
        EXPECT(c->has_background());
        EXPECT(!t->has_background());

        core::Graphics g(200, 60, nullptr);
        host.draw(g);
        const auto cp = c->get_position();
        const auto cs = c->get_size();
        // background face sampled away from the top-left 5x7 text
        EXPECT(test::pixel_at(g, cp.x + cs.width - 1, cp.y + cs.height - 1) ==
               core::Color::from(0, 0, 255).pixel);  // BLUE face
        bool saw_red = false;  // RED text somewhere in the box
        for (int y = 0; y < cs.height && !saw_red; ++y)
        {
            for (int x = 0; x < cs.width; ++x)
            {
                if (test::pixel_at(g, cp.x + x, cp.y + y) ==
                    core::Color::from(255, 0, 0).pixel)
                {
                    saw_red = true;
                    break;
                }
            }
        }
        EXPECT(saw_red);
    }

    // lengths: px and percent; bad values silently unset
    {
        ui_node r = parse_html(
            "<label style=\"width:120px; height:40px\">w</label>\n"
            "<label style=\"width: 50%\">p</label>\n"
            "<label style=\"height: auto\">a</label>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "width")) == 120);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "height")) == 40);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[1], "width")) == "50%");
        EXPECT(find_prop(r.children[2], "height") < 0);  // auto -> unset
        EXPECT(find_prop(r.children[2], "width") < 0);
    }

    // meter degrades to progress_bar with its numeric attributes
    {
        ui_node r = parse_html(
            "<meter id=\"m\" min=\"0\" max=\"200\" value=\"50\"></meter>\n", nullptr);
        EXPECT(r.children[0].type == "progress_bar");
        EXPECT(r.children[0].id == "m");
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "min")) == 0);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "max")) == 200);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "value")) == 50);
    }

    // B3: full integer grammar for range/value/group (negatives reach
    // the widgets, which clamp); step stays non-negative; malformed
    // values are still dropped
    {
        ui_node r = parse_html(
            "<meter id=\"m\" min=\"-5\" max=\"5\" value=\"-3\"></meter>\n"
            "<meter id=\"o\" min=\"0\" max=\"100\" value=\"150\"></meter>\n"
            "<radio id=\"g\" group=\"-2\">r</radio>\n"
            "<knob id=\"k\" step=\"-2\" min=\"0\" max=\"10\" value=\"3\"></knob>\n"
            "<meter id=\"bad\" value=\"abc\"></meter>\n",
            nullptr);
        EXPECT(r.children.size() == 5);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "min")) == -5);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "max")) == 5);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "value")) == -3);
        EXPECT(test::vget<long long>(node_prop_v(r.children[1], "value")) == 150);
        EXPECT(test::vget<long long>(node_prop_v(r.children[2], "group")) == -2);
        EXPECT(find_prop(r.children[3], "step") < 0);  // negative step dropped
        EXPECT(test::vget<long long>(node_prop_v(r.children[3], "value")) == 3);
        EXPECT(find_prop(r.children[4], "value") < 0);  // malformed dropped
    }

    // B3 end-to-end: the widgets clamp what the parser passes through
    {
        ui_node root = parse_html(
            "<meter id=\"p\" min=\"-5\" max=\"5\" value=\"10\"/>\n"
            "<meter id=\"q\" min=\"10\" max=\"0\" value=\"3\"/>\n"
            "<radio id=\"r\" group=\"-2\">x</radio>\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 60);
        build(host, root);
        host.layout();
        auto *p = static_cast<ProgressBar *>(host.find_by_id("p"));
        auto *q = static_cast<ProgressBar *>(host.find_by_id("q"));
        auto *rb = static_cast<RadioButton *>(host.find_by_id("r"));
        EXPECT(p != nullptr && q != nullptr && rb != nullptr);
        EXPECT(p->get_min() == -5 && p->get_max() == 5);
        EXPECT(p->get_value() == 5);  // clamped into range
        EXPECT(q->get_min() == 10 && q->get_max() == 10);  // reversed collapses
        EXPECT(q->get_value() == 10);
        EXPECT(rb->get_group() == -2);
    }

    // <html>/<head> never construct but <style> still collects; <title>
    // is silently ignored; attribute entities decode
    {
        ui_node r = parse_html(
            "<html>\n"
            "  <head>\n"
            "    <title>ignored</title>\n"
            "    <style>button { color: red; }</style>\n"
            "  </head>\n"
            "  <body>\n"
            "    <button id=\"a&amp;b\">X</button>\n"
            "  </body>\n"
            "</html>\n",
            nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(r.children[0].type == "button");
        EXPECT(r.children[0].id == "a&b");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "color")) == "red");
    }

    // nested inline tags merge their text into the enclosing leaf
    {
        ui_node r = parse_html("<span>a<span>b</span>c</span>\n", nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(r.children[0].type == "label");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "text")) == "abc");
    }

    // small is an inline label (no size distinction until a font-size
    // seam exists): it merges like span, and the model500 subtitle
    // pattern keeps its text instead of dropping it
    {
        ui_node r = parse_html("<label>a<small>b</small>c</label>\n", nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(r.children[0].type == "label");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "text")) == "abc");
        ui_node r2 = parse_html(
            "<div>IMPRINT<small>MODEL 500 · STEREO</small></div>\n", nullptr);
        EXPECT(r2.type == "column");
        EXPECT(r2.children.size() == 2);
        EXPECT(r2.children[1].type == "label");
        // the middle dot rides along (and feeds the glyph-subset
        // scanner, so the dot renders instead of skipping)
        EXPECT(test::vget<std::string>(node_prop_v(r2.children[1], "text")) ==
               "MODEL 500 · STEREO");
    }

    // B1: br inside a leaf warns (no line breaking until H-1) and
    // degrades to a word space in the single-line label; edges trim
    // clean and runs collapse
    {
        ui_node r = parse_html("<label>a<br/>b</label>\n", nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "text")) == "a b");

        ui_node r2 = parse_html("<label><br/>b</label>\n", nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r2.children[0], "text")) == "b");

        ui_node r3 = parse_html("<label>a<br/></label>\n", nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r3.children[0], "text")) == "a");

        ui_node r4 = parse_html("<label>a<br/><br/>b</label>\n", nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r4.children[0], "text")) == "a b");
    }

    // C1/C5: still-inert selectors are consumed with their bodies, so
    // following rules still apply; a bare '#' matches nothing. Comma
    // groups split (label,button both live now).
    {
        ui_node r = parse_html(
            "<style>\n"
            "  label, button { color: red; }\n"
            "  label:hover { color: green; }\n"
            "  @media x { label { color: blue; } }\n"
            "  # { color: yellow; }\n"
            "  { color: magenta; }\n"
            "  span { color: cyan; }\n"
            "</style>\n"
            "<label>L</label><span>S</span><button>B</button>\n",
            nullptr);
        EXPECT(r.children.size() == 3);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "color")) ==
               "red");  // comma group reaches label, pseudo stays dead
        EXPECT(test::vget<std::string>(node_prop_v(r.children[1], "color")) ==
               "cyan");  // valid rule after the junk survives
        EXPECT(test::vget<std::string>(node_prop_v(r.children[2], "color")) ==
               "red");  // comma group reaches button
    }

    // H-5: class, multi-class, descendant and specificity cascade
    {
        ui_node r = parse_html(
            "<style>\n"
            "  div { color: red; }\n"
            "  .amp { color: green; }\n"
            "  div.amp { color: blue; }\n"
            "  #one { color: magenta; }\n"
            "</style>\n"
            "<div class=\"amp\" id=\"one\">x</div>\n",
            nullptr);
        // id beats tag.class beats class beats tag
        EXPECT(test::vget<std::string>(node_prop_v(r, "color")) == "magenta");
        ui_node rd = parse_html(
            "<style>\n"
            "  div { color: red; }\n"
            "  .outer .inner { color: cyan; }\n"
            "</style>\n"
            "<div class=\"outer\"><div class=\"inner\">x</div></div>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(rd, "color")) == "red");
        EXPECT(rd.children.size() == 1);
        // inner: descendant beats the tag rule (div would say red)
        EXPECT(test::vget<std::string>(node_prop_v(rd.children[0], "color")) == "cyan");
        // multi-class needs every class
        ui_node r2 = parse_html(
            "<style>.a.b { color: yellow; }</style>\n"
            "<div class=\"a\">x</div><div class=\"a b\">y</div>\n",
            nullptr);
        EXPECT(find_prop(r2.children[0], "color") < 0);
        EXPECT(test::vget<std::string>(node_prop_v(r2.children[1], "color")) == "yellow");
        // specificity beats document order, order breaks ties
        ui_node r3 = parse_html(
            "<style>\n"
            "  .x { color: red; }\n"
            "  div { color: green; }\n"
            "  .y { color: blue; }\n"
            "  .y { color: cyan; }\n"
            "</style>\n"
            "<div class=\"x y\">z</div>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r3, "color")) == "cyan");
        // classes keep their case, tags fold it
        ui_node r4 = parse_html(
            "<style>.Amp { color: red; } DIV { color: green; }</style>\n"
            "<div class=\"amp\">x</div>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r4, "color")) == "green");
    }

    // H-5 follow-up: a bare display:flex takes the CSS flex default
    // (row); an explicit flex-direction still wins; block stays column
    {
        ui_node r = parse_html("<div style=\"display: flex\"><label>x</label></div>\n", nullptr);
        EXPECT(r.type == "row");
        ui_node r2 = parse_html("<div style=\"display: block\"><label>x</label></div>\n", nullptr);
        EXPECT(r2.type == "column");
        ui_node r3 = parse_html(
            "<div style=\"display: flex; flex-direction: column\"><label>x</label></div>\n",
            nullptr);
        EXPECT(r3.type == "column");
    }

    // aspect-ratio mapping: W/H, bare number, auto/malformed absent
    {
        ui_node r = parse_html(
            "<div style=\"width: 100px; aspect-ratio: 2 / 1\"><label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r, "aspect_w")) == 2);
        EXPECT(test::vget<long long>(node_prop_v(r, "aspect_h")) == 1);
        ui_node r2 = parse_html("<div style=\"aspect-ratio: 3\"><label>x</label></div>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r2, "aspect_w")) == 3);
        EXPECT(test::vget<long long>(node_prop_v(r2, "aspect_h")) == 1);
        ui_node r3 = parse_html("<div style=\"aspect-ratio: auto\"><label>x</label></div>\n", nullptr);
        EXPECT(find_prop(r3, "aspect_w") < 0);
        ui_node r4 = parse_html("<div style=\"aspect-ratio: 2 / 0\"><label>x</label></div>\n", nullptr);
        EXPECT(find_prop(r4, "aspect_w") < 0);
        // end-to-end: the ratio sizes a real child from its settled width
        ui_node doc2 = parse_html(
            "<div><div style=\"width: 50%; aspect-ratio: 2/1\"><label>x</label></div>"
            "<label>y</label></div>\n",
            nullptr);
        FlexPanel host2;
        host2.set_size(200, 200);
        build(host2, doc2);
        host2.layout();
        EXPECT(doc2.children.size() == 2);
        const auto &items2 = host2.get_items();
        EXPECT(items2.size() == 2);
        EXPECT(items2[0].child->get_size().width == 100);
        EXPECT(items2[0].child->get_size().height == 50);
    }

    // H-5 variables: :root map, substitution, fallback, silent drop
    {
        ui_node r = parse_html(
            "<style>\n"
            "  :root { --ink: #112233; --gap: 7px; }\n"
            "  div { color: var(--ink); gap: var(--gap); }\n"
            "  label { color: var(--missing, blue); }\n"
            "  span { color: var(--missing); }\n"
            "</style>\n"
            "<div><label>L</label><span>S</span></div>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r, "color")) == "#112233");
        EXPECT(test::vget<long long>(node_prop_v(r, "spacing")) == 7);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "color")) == "blue");
        EXPECT(find_prop(r.children[1], "color") < 0);  // no fallback: dropped
    }

    // P-1 paint: background shorthand (solid/linear/radial layers),
    // border, radius
    {
        ui_node r = parse_html(
            "<div style=\"background:linear-gradient(90deg, #111111 0%, #eeeeee 100%)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r, "bg_lin_from")) == "#111111");
        EXPECT(test::vget<std::string>(node_prop_v(r, "bg_lin_to")) == "#eeeeee");
        EXPECT(test::vget<bool>(node_prop_v(r, "bg_lin_h")) == true);
        EXPECT(find_prop(r, "background") < 0);
        ui_node r2 = parse_html(
            "<div style=\"background:linear-gradient(180deg, #111111, #eeeeee)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<bool>(node_prop_v(r2, "bg_lin_h")) == false);
        ui_node r3 = parse_html(
            "<div style=\"background:radial-gradient(circle at 50% 20%, #0c0f14 0%, #05060a 75%)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r3, "bg_rad_cx")) == 50);
        EXPECT(test::vget<long long>(node_prop_v(r3, "bg_rad_cy")) == 20);
        EXPECT(test::vget<std::string>(node_prop_v(r3, "bg_rad_from")) == "#0c0f14");
        EXPECT(test::vget<long long>(node_prop_v(r3, "bg_rad_from_p")) == 0);
        EXPECT(test::vget<std::string>(node_prop_v(r3, "bg_rad_to")) == "#05060a");
        EXPECT(test::vget<long long>(node_prop_v(r3, "bg_rad_to_p")) == 75);
        // 3-stop keeps the ends; texture overlays fall through to the base
        ui_node r4 = parse_html(
            "<style>:root{--tex:repeating-linear-gradient(90deg,#fff 0 2px);}</style>"
            "<div style=\"background:var(--tex), linear-gradient(180deg, #aa0000 0%, #00aa00 45%, #0000aa 100%)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r4, "bg_lin_from")) == "#aa0000");
        EXPECT(test::vget<std::string>(node_prop_v(r4, "bg_lin_to")) == "#0000aa");
        // exactly 3 stops also land the mid section (P-2c)
        EXPECT(test::vget<std::string>(node_prop_v(r4, "bg_lin3_mid")) == "#00aa00");
        EXPECT(test::vget<long long>(node_prop_v(r4, "bg_lin3_p")) == 45);
        // ...and the repeating top layer lands as the overlay (P-2c)
        EXPECT(test::vget<long long>(node_prop_v(r4, "bg_rep_n")) == 2);
        EXPECT(test::vget<long long>(node_prop_v(r4, "bg_rep_period")) == 2);
        // unknown var without fallback drops the whole declaration (CSS)
        ui_node r5 = parse_html(
            "<div style=\"background:var(--tex), linear-gradient(180deg,#aa0000,#0000aa)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(find_prop(r5, "background") < 0);
        EXPECT(find_prop(r5, "bg_lin_from") < 0);
        // conic skips the layer; a repeating layer lands as the
        // overlay (P-2c) — double-position pairs split in two
        ui_node r6 = parse_html(
            "<div style=\"background:repeating-linear-gradient(90deg, #fff 0 2px)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(find_prop(r6, "background") < 0);
        EXPECT(find_prop(r6, "bg_lin_from") < 0);
        EXPECT(test::vget<bool>(node_prop_v(r6, "bg_rep_h")) == true);
        EXPECT(test::vget<long long>(node_prop_v(r6, "bg_rep_period")) == 2);
        EXPECT(test::vget<long long>(node_prop_v(r6, "bg_rep_n")) == 2);
        EXPECT(test::vget<long long>(node_prop_v(r6, "bg_rep_p0")) == 0);
        EXPECT(test::vget<long long>(node_prop_v(r6, "bg_rep_p1")) == 2);
        EXPECT(test::vget<std::string>(node_prop_v(r6, "bg_rep_c0")) == "#fff");
        // solid shorthand + border + radius; shorthand beats background-color
        ui_node r7 = parse_html(
            "<div style=\"background-color:#111111;background:rgba(20,30,40,0.5);"
            "border:2px solid #0a0b09;border-radius:50%\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r7, "background")) == "rgba(20,30,40,0.5)");
        EXPECT(test::vget<long long>(node_prop_v(r7, "border_w")) == 2);
        EXPECT(test::vget<std::string>(node_prop_v(r7, "border_color")) == "#0a0b09");
        EXPECT(test::vget<bool>(node_prop_v(r7, "radius_half")) == true);
        ui_node r8 = parse_html(
            "<div style=\"border:1px dashed #000;border-radius:10px\"><label>x</label></div>\n",
            nullptr);
        EXPECT(find_prop(r8, "border_w") < 0);  // non-solid drops
        EXPECT(test::vget<long long>(node_prop_v(r8, "radius_px")) == 10);
    }

    // P-3 positioning: relative/absolute, offsets, translate
    {
        ui_node r = parse_html(
            "<div style=\"position:relative\"><div style=\"position:absolute;left:0;right:0;"
            "bottom:0;height:28%\"><label>x</label></div></div>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r, "position")) == "relative");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "position")) == "absolute");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "abs_l")) == "0");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "abs_r")) == "0");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "abs_b")) == "0");
        EXPECT(find_prop(r.children[0], "abs_t") < 0);
        ui_node r2 = parse_html(
            "<div style=\"position:absolute;left:50%;top:50%;transform:translate(-50%, -50%)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r2, "abs_l")) == "50%");
        EXPECT(test::vget<std::string>(node_prop_v(r2, "abs_t")) == "50%");
        EXPECT(test::vget<std::string>(node_prop_v(r2, "translate_x")) == "-50%");
        EXPECT(test::vget<std::string>(node_prop_v(r2, "translate_y")) == "-50%");
        // static/fixed are not positioned; auto offsets drop;
        // non-translate transforms drop
        ui_node r3 = parse_html(
            "<div style=\"position:fixed;top:auto;transform:rotate(10deg)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(find_prop(r3, "position") < 0);
        EXPECT(find_prop(r3, "abs_t") < 0);
        EXPECT(find_prop(r3, "translate_x") < 0);
        // end-to-end: the overlay leaves the flow and the flow child
        // keeps the full height (the anchor div instantiates: the abs
        // child resolves against its positioned parent)
        ui_node doc = parse_html(
            "<div style=\"width:200px;height:200px\">"
            "<div style=\"position:relative;width:200px;height:200px\">"
            "<div style=\"width:100%;aspect-ratio:2/1\"><label>x</label></div>"
            "<div style=\"position:absolute;left:0;right:0;bottom:0;height:28%\">"
            "<label>y</label></div></div></div>\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 200);
        build(host, doc);
        host.layout();
        EXPECT(host.get_items().size() == 1);
        auto *anchor =
            static_cast<FlexPanel *>(host.get_items()[0].child.get());
        EXPECT(anchor->get_size().height == 200);
        const auto &akids = anchor->get_items();
        EXPECT(akids.size() == 2);
        EXPECT(akids[0].child->get_size().width == 200);
        EXPECT(akids[0].child->get_size().height == 100);
        EXPECT(akids[1].child->is_absolute());
        EXPECT(akids[1].child->get_size().width == 200);
        EXPECT(akids[1].child->get_size().height == 56);
        EXPECT(akids[1].child->get_position().y == 144);
        // no positioned ancestor: the direct parent box is the fallback
        // (the abs div is a child here: roots never instantiate)
        ui_node doc2 = parse_html(
            "<div style=\"width:200px;height:200px\">"
            "<div style=\"position:absolute;left:10px;top:10px\">"
            "<label>z</label></div></div>\n",
            nullptr);
        FlexPanel host2;
        host2.set_size(200, 200);
        build(host2, doc2);
        host2.layout();
        EXPECT(host2.get_items().size() == 1);
        EXPECT(host2.get_items()[0].child->get_position().x == 10);
        EXPECT(host2.get_items()[0].child->get_position().y == 10);
    }


    // C4: an unquoted value ends at whitespace or '/': value=30/> is
    // value "30", self-closed
    {
        ui_node r = parse_html(
            "<meter id=\"m\" min=\"0\" max=\"100\" value=30/>\n", nullptr);
        EXPECT(r.children.size() == 1);
        EXPECT(r.children[0].type == "progress_bar");
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "value")) == 30);

        ui_node r2 = parse_html("<div id=x><label>y</label></div>\n", nullptr);
        EXPECT(r2.type == "column");
        EXPECT(r2.id == "x");
    }

    // C6: single-quoted values mirror double-quoted ones (entities
    // included); the other quote stays literal inside
    {
        ui_node r = parse_html(
            "<div id='box' style='gap: 2'><label>x</label></div>\n", nullptr);
        EXPECT(r.type == "column");
        EXPECT(r.id == "box");
        EXPECT(test::vget<long long>(node_prop_v(r, "spacing")) == 2);

        ui_node r2 = parse_html(
            "<label id=\"a'b\">x</label><label id='c\"d'>y</label>\n", nullptr);
        EXPECT(r2.children[0].id == "a'b");
        EXPECT(r2.children[1].id == "c\"d");

        ui_node r3 = parse_html("<label title='a>b'>x</label>\n", nullptr);
        EXPECT(r3.children.size() == 1);  // '>' inside quotes ends nothing
        EXPECT(test::vget<std::string>(
                   node_prop_v(r3.children[0], "text")) == "x");
    }

    // C3: gap/padding take the contracted Npx form (bare integers keep
    // working as the legacy tolerance)
    {
        ui_node r = parse_html(
            "<div style=\"gap: 8px; padding: 4px\"><label>x</label></div>\n",
            nullptr);
        EXPECT(r.type == "column");
        EXPECT(test::vget<long long>(node_prop_v(r, "spacing")) == 8);
        EXPECT(test::vget<long long>(node_prop_v(r, "padding")) == 4);

        ui_node r2 = parse_html(
            "<div style=\"gap: 6PX\"><label>x</label></div>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r2, "spacing")) == 6);
    }

    // C7: a styled br height stands alone (no shadowed duplicate);
    // a lone sign is malformed, not zero
    {
        ui_node r = parse_html(
            "<div><br style=\"height: 50px\"></div>\n", nullptr);
        EXPECT(r.children.size() == 1);
        int heights = 0;
        for (const auto &p : r.children[0].props)
        {
            if (p.first == "height")
            {
                ++heights;
            }
        }
        EXPECT(heights == 1);
        EXPECT(test::vget<long long>(
                   node_prop_v(r.children[0], "height")) == 50);

        ui_node r2 = parse_html(
            "<div style=\"gap: -\"><meter value=\"-\">x</meter></div>\n",
            nullptr);
        EXPECT(find_prop(r2, "spacing") < 0);
        EXPECT(find_prop(r2.children[0], "value") < 0);
    }

    // D: contracted mappings the suite never pinned -- radio / toggle /
    // gauge / knob / trend element tags and their whitelisted attrs
    {
        ui_node r = parse_html(
            "<radio id=\"a\" group=\"3\" checked>pick</radio>\n"
            "<toggle id=\"b\" checked=\"yes\"></toggle>\n"
            "<gauge id=\"c\" min=\"-10\" max=\"40\" value=\"20\"></gauge>\n"
            "<knob id=\"d\" min=\"0\" max=\"10\" step=\"2\" value=\"4\"></knob>\n"
            "<trend id=\"e\" style=\"width: 100px; height: 30px\"></trend>\n",
            nullptr);
        EXPECT(r.type == "root");
        EXPECT(r.children.size() == 5);

        const auto &radio = r.children[0];
        EXPECT(radio.type == "radio" && radio.id == "a");
        EXPECT(test::vget<long long>(node_prop_v(radio, "group")) == 3);
        EXPECT(test::vget<bool>(node_prop_v(radio, "checked")) == true);
        EXPECT(test::vget<std::string>(node_prop_v(radio, "text")) == "pick");

        const auto &tog = r.children[1];
        EXPECT(tog.type == "toggle" && tog.id == "b");
        EXPECT(test::vget<bool>(node_prop_v(tog, "checked")) == true);

        const auto &gg = r.children[2];
        EXPECT(gg.type == "gauge" && gg.id == "c");
        EXPECT(test::vget<long long>(node_prop_v(gg, "min")) == -10);
        EXPECT(test::vget<long long>(node_prop_v(gg, "max")) == 40);
        EXPECT(test::vget<long long>(node_prop_v(gg, "value")) == 20);

        const auto &kn = r.children[3];
        EXPECT(kn.type == "knob" && kn.id == "d");
        EXPECT(test::vget<long long>(node_prop_v(kn, "step")) == 2);
        EXPECT(test::vget<long long>(node_prop_v(kn, "value")) == 4);

        const auto &tr = r.children[4];
        EXPECT(tr.type == "trend" && tr.id == "e");
        EXPECT(test::vget<long long>(node_prop_v(tr, "width")) == 100);
        EXPECT(test::vget<long long>(node_prop_v(tr, "height")) == 30);
    }

    // D: contracted minutiae -- unquoted numeric ids, font-size mapping,
    // display/flex-direction no-ops, stacked style blocks incl. body ones
    {
        ui_node r = parse_html("<div id=7><label>x</label></div>\n", nullptr);
        EXPECT(r.type == "column");
        EXPECT(r.id == "7");

        ui_node r2 = parse_html(
            "<label style=\"font-size: 20px\">hi</label>\n", nullptr);
        EXPECT(test::vget<long long>(
                   node_prop_v(r2.children[0], "font_size")) == 20);
        EXPECT(test::vget<std::string>(
                   node_prop_v(r2.children[0], "text")) == "hi");

        ui_node r3 = parse_html(
            "<div style=\"display: block\"><label>x</label></div>\n"
            "<div style=\"display: flex; flex-direction: column\">"
            "<label>y</label></div>\n"
            "<label style=\"display: block\">z</label>\n",
            nullptr);
        EXPECT(r3.children[0].type == "column");
        EXPECT(r3.children[1].type == "column");
        EXPECT(r3.children[2].type == "label");  // display keeps table type

        ui_node r4 = parse_html(
            "<head><style>label { color: red; }</style></head>\n"
            "<body>\n"
            "  <style>label { color: green; }</style>\n"
            "  <label>w</label>\n"
            "</body>\n",
            nullptr);
        EXPECT(r4.children.size() == 1);
        EXPECT(test::vget<std::string>(
                   node_prop_v(r4.children[0], "color")) == "green");
    }

    // font-size mapping (code-contract §2.4): Npx/bare land as the
    // font_size node prop; small defaults to 12 unless explicit wins;
    // malformed/negative/zero drop the declaration silently
    {
        ui_node bare = parse_html("<label style=\"font-size: 24\">x</label>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(bare.children[0], "font_size")) == 24);

        ui_node sm = parse_html("<small>cap</small>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(sm.children[0], "font_size")) == 12);

        ui_node smx = parse_html("<small style=\"font-size: 18px\">cap</small>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(smx.children[0], "font_size")) == 18);

        ui_node bad = parse_html("<label style=\"font-size: banana\">x</label>\n", nullptr);
        EXPECT(find_prop(bad.children[0], "font_size") < 0);

        ui_node neg = parse_html("<label style=\"font-size: -4px\">x</label>\n", nullptr);
        EXPECT(find_prop(neg.children[0], "font_size") < 0);

        ui_node plain = parse_html("<label>x</label>\n", nullptr);
        EXPECT(find_prop(plain.children[0], "font_size") < 0);
    }

    // E: lexical doc-claims -- unclosed frames finalize at EOF, a
    // general stray close pops to the match, single-quoted entities
    // decode, a lone '<' consumes through the next '>'
    {
        ui_node r = parse_html("<div><label>x", nullptr);
        EXPECT(r.type == "column");
        EXPECT(r.children.size() == 1);
        EXPECT(test::vget<std::string>(
                   node_prop_v(r.children[0], "text")) == "x");

        ui_node r2 = parse_html(
            "<div><label>a</label></span><button>b</button></div>\n", nullptr);
        EXPECT(r2.type == "root");
        EXPECT(r2.children.size() == 2);
        EXPECT(r2.children[0].type == "column");
        EXPECT(r2.children[1].type == "button");

        ui_node r3 = parse_html("<label id='a&amp;b'>x</label>\n", nullptr);
        EXPECT(r3.children[0].id == "a&b");

        ui_node r4 = parse_html("<div>a < b</div>\n", nullptr);
        EXPECT(r4.type == "column");
        EXPECT(r4.children.size() == 1);
        EXPECT(test::vget<std::string>(
                   node_prop_v(r4.children[0], "text")) == "a");
    }

    // top-level bare text becomes an anonymous label
    {
        ui_node r = parse_html("<body>hello <button>b</button></body>\n", nullptr);
        EXPECT(r.type == "root");
        EXPECT(r.children.size() == 2);
        EXPECT(r.children[0].type == "label");
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "text")) == "hello");
    }

    // B2: the <body> style feeds the page box (pixels only, per axis);
    // %/auto/malformed stay absent; a null page pointer is tolerated
    {
        html_page pg;
        bool ok = false;
        ui_node r = parse_html(
            "<body style=\"width: 320px; height: 240px; "
            "background-color: #112233\"><label>x</label></body>\n",
            &ok, &pg);
        EXPECT(ok);
        EXPECT(pg.has_width && pg.width == 320);
        EXPECT(pg.has_height && pg.height == 240);
        EXPECT(pg.has_background);
        EXPECT(pg.background.pixel ==
               core::Color::from(0x11, 0x22, 0x33).pixel);
        EXPECT(r.children.size() == 1);

        html_page pg2;
        parse_html("<div><label>x</label></div>\n", nullptr, &pg2);
        EXPECT(!pg2.has_width && !pg2.has_height && !pg2.has_background);

        html_page pg3;
        parse_html(
            "<body style=\"width: 50%; height: auto\"><label>x</label></body>\n",
            nullptr, &pg3);
        EXPECT(!pg3.has_width && !pg3.has_height);

        html_page pg4;
        parse_html(
            "<body style=\"width: 0px; height: -5px; background-color: nope\">"
            "<label>x</label></body>\n",
            nullptr, &pg4);
        EXPECT(!pg4.has_width && !pg4.has_height && !pg4.has_background);

        // last declaration wins within the inline list, like everywhere
        html_page pg5;
        parse_html(
            "<body style=\"width: 100px; width: 200px\"><label>x</label></body>\n",
            nullptr, &pg5);
        EXPECT(pg5.has_width && pg5.width == 200);

        ui_node r6 = parse_html("<label>x</label>\n", nullptr, nullptr);
        EXPECT(r6.children.size() == 1);
    }

    // comments and doctype are skipped; an empty document yields ok=false
    {
        bool ok = true;
        ui_node r = parse_html(
            "<!-- a comment -->\n"
            "<!DOCTYPE html>\n",
            &ok);
        EXPECT(!ok);
        EXPECT(r.children.empty());

        bool ok2 = true;
        ui_node r2 = parse_html("", &ok2);
        EXPECT(!ok2);
        EXPECT(r2.children.empty());
    }

    // end-to-end: parse and materialize into a live tree
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body>\n"
            "  <div style=\"gap:4\">\n"
            "    <checkbox id=\"cb\" checked>On</checkbox>\n"
            "    <meter id=\"p\" min=\"0\" max=\"100\" value=\"30\"/>\n"
            "  </div>\n"
            "</body>\n",
            &ok);
        EXPECT(ok);
        EXPECT(root.type == "column");

        FlexPanel host;
        host.set_size(200, 60);
        build(host, root);
        host.layout();
        auto *cb = static_cast<Checkbox *>(host.find_by_id("cb"));
        auto *p = static_cast<ProgressBar *>(host.find_by_id("p"));
        EXPECT(cb != nullptr && p != nullptr);
        EXPECT(cb->is_checked());
        EXPECT(p->get_value() == 30);
    }

    // svg vector-dial subset: viewBox, g inheritance, decimals, opacity
    // product, text anchoring, and the vectordial alias
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body><div>\n"
            "  <svg id=\"vu\" viewBox=\"0 0 100 50\" preserveAspectRatio=\"none\">\n"
            "    <g stroke=\"#7f9cb0\" stroke-width=\"1\" opacity=\"0.7\">\n"
            "      <line x1=\"6\" y1=\"38\" x2=\"12\" y2=\"35\"/>\n"
            "      <line x1=\"50\" y1=\"44\" x2=\"38\" y2=\"20\" stroke=\"#dce7ee\""
            " stroke-width=\"2.5\" opacity=\"0.35\"/>\n"
            "    </g>\n"
            "    <text x=\"50\" y=\"48\" fill=\"#dce7ee\" text-anchor=\"middle\">dB</text>\n"
            "  </svg>\n"
            "</div></body>\n",
            &ok);
        EXPECT(ok);
        EXPECT(root.type == "column");
        EXPECT(root.children.size() == 1);
        const ui_node &s = root.children[0];
        EXPECT(s.type == "svg");
        EXPECT(s.id == "vu");
        EXPECT(test::vget<long long>(node_prop_v(s, "vb_w")) == 100);
        EXPECT(test::vget<long long>(node_prop_v(s, "vb_h")) == 50);
        EXPECT(s.children.size() == 3);
        EXPECT(s.children[0].type == "svg_line");
        EXPECT(test::vget<long long>(node_prop_v(s.children[0], "x1")) == 6);
        EXPECT(test::vget<std::string>(node_prop_v(s.children[0], "stroke")) == "#7f9cb0");
        // inherited group opacity 0.7 -> alpha 179
        EXPECT(test::vget<long long>(node_prop_v(s.children[0], "stroke_alpha")) == 179);
        // own 0.35 times inherited 0.7 -> alpha 62
        EXPECT(test::vget<std::string>(node_prop_v(s.children[1], "stroke")) == "#dce7ee");
        EXPECT(test::vget<long long>(node_prop_v(s.children[1], "stroke_alpha")) == 62);
        EXPECT(s.children[2].type == "svg_text");
        EXPECT(test::vget<std::string>(node_prop_v(s.children[2], "text")) == "dB");
        EXPECT(test::vget<long long>(node_prop_v(s.children[2], "anchor")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(s.children[2], "fill_alpha")) == 255);

        // decimals round half away from zero (single container div
        // keeps the svg one level down; a bare svg would stay wrapped
        // in the pseudo-root)
        ui_node r2 = parse_html(
            "<div><svg viewBox=\"0 0 100 50\">"
            "<line x1=\"2.5\" y1=\"-1.5\" x2=\"0\" y2=\"0\" stroke=\"red\"/></svg></div>\n",
            nullptr);
        EXPECT(r2.children[0].children.size() == 1);
        EXPECT(test::vget<long long>(node_prop_v(r2.children[0].children[0], "x1")) == 3);
        EXPECT(test::vget<long long>(node_prop_v(r2.children[0].children[0], "y1")) == -2);

        // vectordial is the same node type
        ui_node r3 = parse_html("<vectordial viewBox=\"0 0 10 10\"></vectordial>\n", nullptr);
        EXPECT(r3.children.size() == 1 && r3.children[0].type == "svg");

        // malformed viewBox / missing stroke / zero width drop silently
        ui_node r4 = parse_html(
            "<div><svg viewBox=\"0 0 oops\">"
            "<line x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"/></svg></div>\n",
            nullptr);
        EXPECT(find_prop(r4.children[0], "vb_w") < 0);
        ui_node r5 = parse_html(
            "<div><svg><line x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"/>"
            "<line x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\" stroke=\"red\" stroke-width=\"0\"/></svg></div>\n",
            nullptr);
        EXPECT(r5.children[0].children.empty());

        // line/text outside svg build nothing
        bool ok6 = true;
        ui_node r6 = parse_html("<line x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"/>\n", &ok6);
        EXPECT(!ok6);
        EXPECT(r6.children.empty());
        bool ok7 = false;
        ui_node r7 = parse_html("<label>x</label><line x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"/>\n", &ok7);
        EXPECT(ok7);
        EXPECT(r7.children.size() == 1 && r7.children[0].type == "label");
    }

    // end-to-end: an svg document materializes into a live SvgCanvas
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body><div>\n"
            "  <svg id=\"vu\" viewBox=\"0 0 100 50\">\n"
            "    <line x1=\"0\" y1=\"25\" x2=\"100\" y2=\"25\" stroke=\"white\"/>\n"
            "    <text x=\"50\" y=\"48\" fill=\"white\" text-anchor=\"middle\">dB</text>\n"
            "  </svg>\n"
            "</div></body>\n",
            &ok);
        EXPECT(ok);
        FlexPanel host;
        host.set_size(200, 100);
        build(host, root);
        host.layout();
        auto *v = static_cast<SvgCanvas *>(host.find_by_id("vu"));
        EXPECT(v != nullptr);
        EXPECT(v->lines().size() == 1 && v->texts().size() == 1);
        EXPECT(v->view_w() == 100 && v->view_h() == 50);
    }

    // P-2a text dressing: tracking px, bold (700/600/bold on,
    // 400/normal off), one solid offset shadow; malformed drops
    {
        ui_node r = parse_html(
            "<div style=\"display:flex;flex-direction:row\">"
            "<label style=\"letter-spacing:3px;font-weight:700;"
            "text-shadow:1px 1px rgba(255,0,0,1)\">AB</label>"
            "<label style=\"font-weight:400\">C</label>"
            "<label style=\"letter-spacing:big;text-shadow:red\">D</label>"
            "</div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "letter_px")) == 3);
        EXPECT(test::vget<bool>(node_prop_v(r.children[0], "bold")) == true);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "shadow_dx")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "shadow_dy")) == 1);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "shadow_color")) ==
               "rgba(255,0,0,1)");
        EXPECT(test::vget<bool>(node_prop_v(r.children[1], "bold")) == false);
        EXPECT(find_prop(r.children[2], "letter_px") < 0);
        EXPECT(find_prop(r.children[2], "shadow_color") < 0);

        // build: tracking widens the measure (2 chars x (6 + 3)),
        // bold + shadow add pixels over the plain face
        FlexPanel host;
        host.set_size(200, 60);
        build(host, r);
        host.layout();
        auto *dressed = static_cast<Label *>(host.get_items()[0].child.get());
        auto *plain = static_cast<Label *>(host.get_items()[1].child.get());
        EXPECT(dressed->get_size().width == 2 * (6 + 3));
        EXPECT(plain->get_size().width == 6);
        EXPECT(dressed->bold());
        EXPECT(!plain->bold());
        EXPECT(dressed->has_text_shadow());
        core::Graphics g(200, 60, nullptr);
        host.draw(g);
        const auto dp = dressed->get_position();
        const auto ds = dressed->get_size();
        const uint32_t red_px = core::Color::from(255, 0, 0).pixel;
        int ink = 0;
        int red = 0;
        for (int y = dp.y; y < dp.y + ds.height; ++y)
        {
            for (int x = dp.x; x < dp.x + ds.width; ++x)
            {
                const uint32_t p = test::pixel_at(g, x, y);
                if (p == red_px)
                {
                    ++red;
                }
                else if (p != 0)
                {
                    ++ink;
                }
            }
        }
        // face pixels plus a red offset copy inside the same box
        EXPECT(ink > 0 && red > 0);
    }

    // build(): the root's box dress styles the host, geometry never
    // transfers (a fixed host buffer always wins)
    {
        ui_node doc = parse_html(
            "<div style=\"width:620px;background:#dfdfda;"
            "border:2px solid #0a0b09;border-radius:10px\">"
            "<label>x</label></div>\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 60);
        build(host, doc);
        EXPECT(host.has_background());
        EXPECT(host.has_border());
        EXPECT(host.get_size().width == 200);
        EXPECT(host.get_size().height == 60);
        EXPECT(host.get_items().size() == 1);
    }

    // P-2b conic: from + deg stops land as props; % positions, `at`
    // centers, and >4 stops drop the layer; the built widget paints
    // the sweep (bottom half toward `to`)
    {
        ui_node r = parse_html(
            "<div>"
            "<div style=\"width:41px;height:41px;background:"
            "conic-gradient(from 0deg, black 0deg, white 180deg, black 360deg)\">"
            "<label>x</label></div>"
            "<div style=\"background:conic-gradient(at 50% 50%, black, white)\">"
            "<label>y</label></div>"
            "</div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "bg_con_from")) == 0);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "bg_con_p1")) == 180);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[0], "bg_con_c1")) == "white");
        EXPECT(find_prop(r.children[1], "bg_con_from") < 0);
        FlexPanel host;
        host.set_size(200, 100);
        build(host, r);
        host.layout();
        auto *knob = host.get_items()[0].child.get();
        EXPECT(knob->has_background());
        EXPECT(knob->get_size().width == 41);
        core::Graphics g(200, 100, nullptr);
        host.draw(g);
        const auto kp = knob->get_position();
        const auto ks = knob->get_size();
        // face center column: top reads the 0 stop, bottom the 180
        // stop (both axis-exact in either trig path, whatever the
        // cross-axis width settles at)
        EXPECT(test::pixel_at(g, kp.x + ks.width / 2, kp.y) ==
               core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, kp.x + ks.width / 2, kp.y + ks.height - 1) ==
               core::colors::White.pixel);
    }

    // P-2c extended linear: a 3-stop face reads mid at the center;
    // a repeating face stripes with the period
    {
        ui_node doc = parse_html(
            "<div>"
            "<div style=\"width:41px;height:41px;background:"
            "linear-gradient(90deg, black, red 50%, white)\">"
            "<label>x</label></div>"
            "<div style=\"width:41px;height:12px;background:"
            "repeating-linear-gradient(90deg, white 0 2px, black 2px 6px)\">"
            "<label>y</label></div>"
            "</div>\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 100);
        build(host, doc);
        host.layout();
        auto *tri = host.get_items()[0].child.get();
        auto *striped = host.get_items()[1].child.get();
        EXPECT(tri->has_background() && striped->has_background());
        core::Graphics g(200, 100, nullptr);
        host.draw(g);
        const auto tp = tri->get_position();
        EXPECT(test::pixel_at(g, tp.x, tp.y + 20) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, tp.x + 20, tp.y + 20) == core::colors::Red.pixel);
        EXPECT(test::pixel_at(g, tp.x + 40, tp.y + 20) == core::colors::White.pixel);
        // right side, clear of the "y" glyph: period-6 white[0,2]/black[2,6]
        const auto sp = striped->get_position();
        const int sy = sp.y + striped->get_size().height / 2;
        EXPECT(test::pixel_at(g, sp.x + 36, sy) == core::colors::White.pixel);
        EXPECT(test::pixel_at(g, sp.x + 39, sy) == core::colors::Black.pixel);
    }

    // P-2c: `transparent` is a legal repeating stop (alpha-0), not a
    // malformed layer — the vubottom shape lands and paints
    {
        ui_node doc = parse_html(
            "<div>"
            "<div style=\"width:42px;height:12px;background:"
            "repeating-linear-gradient(90deg, rgba(220,231,238,0.08) 0 2px, "
            "transparent 2px 6px)\">"
            "</div></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(doc.children[0], "bg_rep_n")) == 4);
        EXPECT(test::vget<std::string>(node_prop_v(doc.children[0], "bg_rep_c2")) ==
               "transparent");
        FlexPanel host;
        host.set_size(200, 100);
        build(host, doc);
        host.layout();
        auto *over = host.get_items()[0].child.get();
        EXPECT(over->has_background());
        core::Graphics g(200, 100, nullptr);
        host.draw(g);
        const auto op = over->get_position();
        const int oy = op.y + over->get_size().height / 2;
        // ice stripe vs transparent gap read differently
        EXPECT(test::pixel_at(g, op.x, oy) != test::pixel_at(g, op.x + 3, oy));
    }

    // P-2d opacity + border-top: fixed-point parse, build-time alpha
    // fold, top band paint
    {
        ui_node r = parse_html(
            "<div>"
            "<div style=\"opacity:0.35\"><label>a</label></div>"
            "<div style=\"opacity:35%\"><label>b</label></div>"
            "<div style=\"opacity:2\"><label>c</label></div>"
            "<div style=\"opacity:junk\"><label>d</label></div>"
            "<div style=\"border-top:2px solid #ff0000\"><label>e</label></div>"
            "</div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "elem_opacity")) == 350);
        EXPECT(test::vget<long long>(node_prop_v(r.children[1], "elem_opacity")) == 350);
        EXPECT(test::vget<long long>(node_prop_v(r.children[2], "elem_opacity")) == 1000);
        EXPECT(find_prop(r.children[3], "elem_opacity") < 0);
        EXPECT(test::vget<long long>(node_prop_v(r.children[4], "border_t_w")) == 2);
        EXPECT(test::vget<std::string>(node_prop_v(r.children[4], "border_t_c")) == "#ff0000");

        // white face at 0.35 over clear-black blends to 90 gray
        // (ceil((255 * 350 + 999) / 1000)); on 16bpp the folded bit
        // stays set
        ui_node doc = parse_html(
            "<div>"
            "<div style=\"width:41px;height:41px;background:#ffffff;opacity:0.35\">"
            "<label>x</label></div>"
            "<div style=\"width:41px;height:12px;border-top:2px solid #ff0000\">"
            "<label>y</label></div>"
            "</div>\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 100);
        build(host, doc);
        host.layout();
        auto *dim = host.get_items()[0].child.get();
        auto *ruled = host.get_items()[1].child.get();
        EXPECT(dim->has_background());
        EXPECT(ruled->has_top_border());
        core::Graphics g(200, 100, nullptr);
        host.draw(g);
        const auto dp = dim->get_position();
#if COLOR_DEPTH == 32
        // white at 90 over clear-black: channels 90, source-over alpha
        // 90 (90 + 0)
        EXPECT(test::pixel_at(g, dp.x + 20, dp.y + 20) ==
               core::Color::from(90, 90, 90, 90).pixel);
#else
        EXPECT(test::pixel_at(g, dp.x + 20, dp.y + 20) ==
               core::colors::White.pixel);
#endif
        const auto rp = ruled->get_position();
        const uint32_t red_px = core::Color::from(255, 0, 0).pixel;
        EXPECT(test::pixel_at(g, rp.x + 20, rp.y) == red_px);
        EXPECT(test::pixel_at(g, rp.x + 20, rp.y + 1) == red_px);
        EXPECT(test::pixel_at(g, rp.x + 20, rp.y + 5) != red_px);
    }

    // P-2e box-shadow: list parse (1 outer + 2 inset, cap, malformed
    // entries drop alone), inset bands paint, outer leaves an opaque
    // face alone (clip — contract)
    {
        ui_node r = parse_html(
            "<div style=\"box-shadow: 0 3px 6px rgba(0,0,0,0.5), "
            "inset 0 1px 2px rgba(255,255,255,0.8), "
            "inset 0 -2px 4px rgba(0,0,0,0.5)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r, "sh_o0_ox")) == 0);
        EXPECT(test::vget<long long>(node_prop_v(r, "sh_o0_oy")) == 3);
        EXPECT(test::vget<long long>(node_prop_v(r, "sh_o0_blur")) == 6);
        EXPECT(test::vget<std::string>(node_prop_v(r, "sh_o0_color")) ==
               "rgba(0,0,0,0.5)");
        EXPECT(test::vget<long long>(node_prop_v(r, "sh_i0_oy")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(r, "sh_i1_oy")) == -2);
        EXPECT(test::vget<long long>(node_prop_v(r, "sh_i1_blur")) == 4);
        // a third inset drops, junk entries drop alone
        ui_node r2 = parse_html(
            "<div style=\"box-shadow: inset 0 0 1px black, "
            "inset 0 0 2px black, inset 0 0 3px black, junk entry here\">"
            "<label>y</label></div>\n",
            nullptr);
        EXPECT(find_prop(r2, "sh_i0_ox") >= 0);
        EXPECT(find_prop(r2, "sh_i1_ox") >= 0);
        EXPECT(find_prop(r2, "sh_i2_ox") < 0);

        // inset 0 0 4px black on white: hard edge, fading bands,
        // untouched center
        ui_node doc = parse_html(
            "<div>"
            "<div style=\"width:41px;height:21px;background:#ffffff;"
            "box-shadow: inset 0 0 4px black\">"
            "<label>x</label></div>"
            "<div style=\"width:41px;height:21px;background:#ffffff;"
            "box-shadow: 0 3px 6px black\">"
            "<label>y</label></div>"
            "</div>\n",
            nullptr);
        FlexPanel host;
        host.set_size(200, 100);
        build(host, doc);
        host.layout();
        auto *sunk = host.get_items()[0].child.get();
        auto *cast = host.get_items()[1].child.get();
        core::Graphics g(200, 100, nullptr);
        host.draw(g);
        const auto sp = sunk->get_position();
        EXPECT(test::pixel_at(g, sp.x + 20, sp.y) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, sp.x + 20, sp.y + 10) ==
               core::colors::White.pixel);
#if COLOR_DEPTH == 32
        // band 3 of 4: alpha 63 over white -> 192
        EXPECT(test::pixel_at(g, sp.x + 20, sp.y + 3) ==
               core::Color::from(192, 192, 192).pixel);
#else
        // binary depths keep/drop bands by the half-coverage rule:
        // bands 0..2 stay black, band 3 drops to white
        EXPECT(test::pixel_at(g, sp.x + 20, sp.y + 2) ==
               core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, sp.x + 20, sp.y + 3) ==
               core::colors::White.pixel);
#endif
        // outer-only shadow changes nothing on the opaque face
        const auto cp = cast->get_position();
        const auto cs = cast->get_size();
        bool all_white = true;
        for (int y = cp.y; y < cp.y + cs.height && all_white; ++y)
        {
            for (int x = cp.x; x < cp.x + cs.width; ++x)
            {
                const uint32_t p = test::pixel_at(g, x, y);
                if (p != core::colors::White.pixel && p != 0)
                {
                    // the "y" glyph paints black text: only fail on
                    // shadow-colored strays (dark non-glyph gray)
                    if (p != core::colors::Black.pixel)
                    {
                        all_white = false;
                    }
                }
            }
        }
        EXPECT(all_white);
    }

    // split-side inset bands AA their chord-cut ends (knob-rim
    // staircase): 41x21 white face, radius 10, top-only inset
    {
        ui_node doc = parse_html(
            "<div>"
            "<div style=\"width:41px;height:21px;background:#ffffff;"
            "border-radius:10px;"
            "box-shadow: inset 0 2px 3px black\">"
            "<label>x</label></div>"
            "</div>\n",
            nullptr);
        FlexPanel host;
        host.set_size(100, 60);
        build(host, doc);
        host.layout();
        auto *knob = host.get_items()[0].child.get();
        core::Graphics g(100, 60, nullptr);
        g.fill(core::Color::from(128, 128, 128));
        host.draw(g);
        const auto kp = knob->get_position();
        // band row 1 spans lx=6..34 (chord(10,9)=4); the fringe pixel
        // just outside blends band-over-gray, the span pixel is band
        const uint32_t fringe = test::pixel_at(g, kp.x + 5, kp.y + 1);
        const uint32_t span = test::pixel_at(g, kp.x + 6, kp.y + 1);
#if COLOR_DEPTH == 32
        // fringe stacks Porter-Duff: the face fringe (white@85 over
        // gray) lights (5,1) to 170 first, then the band fringe
        // (coverage 85 x band alpha 170 -> a=56) lands it at 132
        EXPECT(fringe == core::Color::from(132, 132, 132).pixel);
        // span: band alpha 170 over the white face -> 85
        EXPECT(span == core::Color::from(85, 85, 85).pixel);
#else
        // binary: the fringe coverage quantizes away (stays gray),
        // the band keeps black per the half rule
        EXPECT(fringe == core::Color::from(128, 128, 128).pixel);
        EXPECT(span == core::colors::Black.pixel);
#endif
    }

    // circle keeps its silhouette under top/bottom insets (model500
    // knobs): a zero x-offset paints neither left nor right — the
    // centered blur spill is dropped, only the offset axis bands
    {
        ui_node doc = parse_html(
            "<div>"
            "<div style=\"width:54px;height:54px;background:#ffffff;"
            "border-radius:50%;"
            "box-shadow: inset 0 2px 3px black\">"
            "</div></div>\n",
            nullptr);
        FlexPanel host;
        host.set_size(100, 80);
        build(host, doc);
        host.layout();
        auto *face = host.get_items()[0].child.get();
        core::Graphics g(100, 80, nullptr);
        g.fill(core::Color::from(128, 128, 128));
        host.draw(g);
        const auto fp = face->get_position();
        // top band still paints (row 1, center column)
#if COLOR_DEPTH == 32
        EXPECT(test::pixel_at(g, fp.x + 27, fp.y + 1) ==
               core::Color::from(85, 85, 85).pixel);
#else
        EXPECT(test::pixel_at(g, fp.x + 27, fp.y + 1) ==
               core::colors::Black.pixel);
#endif
        // left/right mid-edge stays the white face (no side spill);
        // the old both-sides rule painted these black
        EXPECT(test::pixel_at(g, fp.x, fp.y + 27) ==
               core::colors::White.pixel);
        EXPECT(test::pixel_at(g, fp.x + 53, fp.y + 27) ==
               core::colors::White.pixel);
    }

    // H-7c flex-basis: auto (default demand), px, and %
    {
        ui_node r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex-basis: auto\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_basis_px == -1);
        EXPECT(r.children[0].flex_basis_pct == 0);
        r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex-basis: 50px\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_basis_px == 50);
        EXPECT(r.children[0].flex_basis_pct == 0);
        r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex-basis: 25%\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_basis_px == -1);
        EXPECT(r.children[0].flex_basis_pct == 25);
    }

    // H-7c flex-shrink: integer weight
    {
        ui_node r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex-shrink: 3\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_shrink == 3);
        r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex-shrink: 0\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_shrink == 0);
    }

    // H-7c flex shorthand: 1 token (grow), 2 tokens (grow shrink), 3 tokens (grow shrink basis), none
    {
        ui_node r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex: 2\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_grow == 2);
        EXPECT(r.children[0].flex_shrink == 0);
        EXPECT(r.children[0].flex_basis_px == -1);
        EXPECT(r.children[0].flex_basis_pct == 0);
        r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex: 1 2\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_grow == 1);
        EXPECT(r.children[0].flex_shrink == 2);
        EXPECT(r.children[0].flex_basis_px == -1);
        EXPECT(r.children[0].flex_basis_pct == 0);
        r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex: 1 2 50px\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_grow == 1);
        EXPECT(r.children[0].flex_shrink == 2);
        EXPECT(r.children[0].flex_basis_px == 50);
        EXPECT(r.children[0].flex_basis_pct == 0);
        r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex: 1 2 33%\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_grow == 1);
        EXPECT(r.children[0].flex_shrink == 2);
        EXPECT(r.children[0].flex_basis_px == -1);
        EXPECT(r.children[0].flex_basis_pct == 33);
        r = parse_html(
            "<div style=\"display:flex\"><label style=\"flex: none\">x</label></div>\n",
            nullptr);
        EXPECT(r.children[0].flex_grow == 0);
        EXPECT(r.children[0].flex_shrink == 0);
        EXPECT(r.children[0].flex_basis_px == -1);
        EXPECT(r.children[0].flex_basis_pct == 0);
    }

    // H-7c end-to-end: flex-basis beats explicit size in layout
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body><div style=\"display:flex; flex-direction:column; width:200px; height:100px\">"
            "<label style=\"flex-basis: 30px; width:80px\">x</label>"
            "<label style=\"flex-grow: 1\">y</label>"
            "</div></body>\n",
            &ok);
        EXPECT(ok);
        // root is the single container (row/column) per .ui convention
        FlexPanel host;
        host.set_size(200, 100);
        build(host, root);
        host.layout();
        // root is a FlexPanel (column), its items are the two labels
        const auto &items = host.get_items();
        EXPECT(items.size() == 2);
        // First child: flex-basis:30px sets the column-main (height) to
        // 30, beating both its demand and any grow-zero claim (width:80px
        // is the cross axis here and is unaffected)
        // Second child: flex-grow:1 gets the rest (70px)
        EXPECT(items[0].child->get_size().height == 30);
        EXPECT(items[1].child->get_size().height == 70);
    }

    // H-7 A: a flex body is kept as the document root (inline style)
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body style=\"display: flex; justify-content: center; "
            "align-items: center; padding: 40px\">"
            "<div style=\"width: 620px\"><label>x</label></div></body>\n",
            &ok);
        EXPECT(ok);
        EXPECT(root.type == "row");
        EXPECT(test::vget<long long>(node_prop_v(root, "justify")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(root, "align")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(root, "padding")) == 40);
        EXPECT(root.children.size() == 1);
        FlexPanel host;
        host.set_size(800, 600);
        build(host, root);
        host.layout();
        // the 620px child centers in the 800px host (viewport wrapper)
        const auto &items = host.get_items();
        EXPECT(items.size() == 1);
        EXPECT(items[0].child->get_size().width == 620);
        EXPECT(items[0].child->get_position().x == 90);
    }

    // H-7 A: a flex body from a stylesheet rule is kept too, and a
    // body-anchored descendant selector matches through the chain
    {
        bool ok = false;
        ui_node root = parse_html(
            "<head><style>body { display: flex; } "
            "body label { color: red; }</style></head>\n"
            "<body><div><label>x</label></div></body>\n",
            &ok);
        EXPECT(ok);
        EXPECT(root.type == "row");
        EXPECT(root.children.size() == 1);
        EXPECT(test::vget<std::string>(
                   node_prop_v(root.children[0].children[0], "color")) ==
               "red");
    }

    // H-7 A: a plain body still hoists the single container child
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body><div style=\"width: 100px\"><label>x</label></div></body>\n",
            &ok);
        EXPECT(ok);
        EXPECT(root.type == "column");
        EXPECT(test::vget<long long>(node_prop_v(root, "width")) == 100);
    }

    // H-7 B: absent align-items on an HTML container means stretch
    {
        ui_node r = parse_html("<div><label>x</label></div>\n", nullptr);
        EXPECT(r.type == "column");
        EXPECT(test::vget<long long>(node_prop_v(r, "align")) == 3);
        ui_node r2 = parse_html(
            "<div style=\"align-items: center\"><label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r2, "align")) == 1);
        // labels are not containers: no align default rides them
        EXPECT(find_prop(r.children[0], "align") < 0);
    }

    // H-7 B end-to-end: the auto-cross label stretches to the filled
    // line (the 100px content box, not the 30px sibling max)
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body><div style=\"display: flex; width: 200px\">"
            "<label>x</label>"
            "<label style=\"height: 30px\">y</label>"
            "</div></body>\n",
            &ok);
        EXPECT(ok);
        FlexPanel host;
        host.set_size(200, 100);
        build(host, root);
        host.layout();
        const auto &items = host.get_items();
        EXPECT(items.size() == 2);
        EXPECT(items[0].child->get_size().height == 100);
        EXPECT(items[1].child->get_size().height == 30);
    }

    // H-3 margin shorthand: 1/2/3/4 values with the CSS side mapping
    {
        ui_node r = parse_html(
            "<div><label style=\"margin: 5px\">x</label></div>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_t")) == 5);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_r")) == 5);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_b")) == 5);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_l")) == 5);
        r = parse_html(
            "<div><label style=\"margin: 1px 2px\">x</label></div>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_t")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_r")) == 2);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_b")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_l")) == 2);
        r = parse_html(
            "<div><label style=\"margin: 1px 2px 3px\">x</label></div>\n", nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_t")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_r")) == 2);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_b")) == 3);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_l")) == 2);
        r = parse_html(
            "<div><label style=\"margin: 1px 2px 3px 4px\">x</label></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_t")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_r")) == 2);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_b")) == 3);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_l")) == 4);
    }

    // H-3 margin longhands win over the shorthand; negatives drop
    {
        ui_node r = parse_html(
            "<div><label style=\"margin: 5px; margin-top: 12px\">x</label></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_t")) == 12);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_r")) == 5);
        r = parse_html(
            "<div><label style=\"margin-top: -3px\">x</label></div>\n", nullptr);
        EXPECT(find_prop(r.children[0], "margin_t") < 0);
    }

    // H-3 end-to-end: the footer gap lands between the rows
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body><div style=\"display: flex; flex-direction: column; width: 200px\">"
            "<label>x</label>"
            "<label style=\"margin-top: 12px\">y</label>"
            "</div></body>\n",
            &ok);
        EXPECT(ok);
        FlexPanel host;
        host.set_size(200, 100);
        build(host, root);
        host.layout();
        const auto &items = host.get_items();
        EXPECT(items.size() == 2);
        const int gap = items[1].child->get_position().y -
                        (items[0].child->get_position().y +
                         items[0].child->get_size().height);
        EXPECT(gap == 12);
    }

    // CSS comments never glue to a neighbor declaration
    {
        // :root vars survive trailing comments (series7 lost every
        // commented var's successor before the strip)
        ui_node r = parse_html(
            "<head><style>:root{--a:#112233;/* c */--b:#445566;}"
            ".x{background-color:var(--b);}</style></head>\n"
            "<body><div class=\"x\"><label>q</label></div></body>\n",
            nullptr);
        EXPECT(test::vget<std::string>(node_prop_v(r, "background")) == "#445566");
        // a comment between declarations kills neither side
        r = parse_html(
            "<head><style>.x{margin-top:1px;/* c */margin-left:2px;}</style></head>\n"
            "<body><div class=\"x\"><label>q</label></div></body>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r, "margin_t")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(r, "margin_l")) == 2);
        // a semicolon inside the comment does not end the value
        r = parse_html(
            "<div><label style=\"margin: 1px /* a;b */ 2px\">x</label></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_t")) == 1);
        EXPECT(test::vget<long long>(node_prop_v(r.children[0], "margin_l")) == 2);
    }

    // N-stop linear: 5 specified stops ride bg_linN_pN/cN verbatim
    {
        ui_node r = parse_html(
            "<div style=\"background: linear-gradient(180deg, #8f9186 0%, "
            "#c9cbc4 8%, #dfdfda 30%, #c9cbc4 70%, #8f9186 100%)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_n")) == 5);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_p0")) == 0);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_p1")) == 8);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_p2")) == 30);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_p3")) == 70);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_p4")) == 100);
        EXPECT(test::vget<std::string>(node_prop_v(r, "bg_linN_c0")) == "#8f9186");
        // the ends-only base still lands for compat
        EXPECT(test::vget<std::string>(node_prop_v(r, "bg_lin_from")) == "#8f9186");
        EXPECT(test::vget<std::string>(node_prop_v(r, "bg_lin_to")) == "#8f9186");
    }

    // N-stop linear: bare stops distribute evenly, ends default 0/100
    {
        ui_node r = parse_html(
            "<div style=\"background: linear-gradient(red, green, blue, yellow)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_n")) == 4);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_p0")) == 0);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_p1")) == 33);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_p2")) == 66);
        EXPECT(test::vget<long long>(node_prop_v(r, "bg_linN_p3")) == 100);
    }

    // N-stop linear: >8 stops stay ends-only (P-1 rule)
    {
        ui_node r = parse_html(
            "<div style=\"background: linear-gradient(red 0%, green 10%, blue 20%, "
            "white 30%, black 40%, red 50%, green 60%, blue 70%, white 100%)\">"
            "<label>x</label></div>\n",
            nullptr);
        EXPECT(find_prop(r, "bg_linN_n") < 0);
        EXPECT(test::vget<std::string>(node_prop_v(r, "bg_lin_from")) == "red");
        EXPECT(test::vget<std::string>(node_prop_v(r, "bg_lin_to")) == "white");
    }

    // N-stop linear end-to-end: the third stop reads exactly at 50%
    {
        bool ok = false;
        ui_node root = parse_html(
            "<body><div style=\"display: flex; width: 100px; height: 101px; "
            "background: linear-gradient(black 0%, red 25%, green 50%, "
            "blue 75%, white 100%)\">"
            "<label>x</label></div></body>\n",
            &ok);
        EXPECT(ok);
        FlexPanel host;
        host.set_size(100, 101);
        build(host, root);
        host.layout();
        core::Graphics g(100, 101, nullptr);
        host.draw(g);
        EXPECT(test::pixel_at(g, 50, 50) == core::colors::Green.pixel);
        EXPECT(test::pixel_at(g, 50, 0) == core::colors::Black.pixel);
        EXPECT(test::pixel_at(g, 50, 100) == core::colors::White.pixel);
    }

    return test::report("html");
}