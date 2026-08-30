#pragma once

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace zb::ui
{
    class Widget;  // declared here; widgets are only touched via materialize

    /*
     * Descriptive UI: a typed property value
     */
    using prop_value = std::variant<std::monostate, long long, double, std::string, bool>;

    /*
     * Descriptive node: one widget (or container). A document is a tree
     * of ui_nodes; the widget classes and their property table are
     * described in ui_builder.cpp and docs (see code-contract.md).
     *
     * This node is the common intermediate representation: the fluent
     * builders below (and, later, a designer-file deserializer) both
     * produce/consume it, and materialize() builds the live widget tree
     * from it.
     */
    struct ui_node
    {
        std::string type;  // widget tag (see the tag table in ui_builder.cpp)
        std::string id;    // reference handle (Widget::set_id)
        std::vector<std::pair<std::string, prop_value>> props;  // ordered
        std::vector<ui_node> children;  // container tags only
        std::vector<std::string> items;  // static string model (list_box)
        int flex_grow = 0;               // container layout hint

        // generic property entry (all convenience setters go through it:
        // the future designer deserializer fills props the same way)
        ui_node &prop(std::string name, prop_value v)
        {
            props.emplace_back(std::move(name), std::move(v));
            return *this;
        }

        ui_node &named(std::string s)
        {
            id = std::move(s);
            return *this;
        }

        // geometry / text
        ui_node &size(const long long w, const long long h)
        {
            return prop("width", w).prop("height", h);
        }
        ui_node &pos(const long long x, const long long y)
        {
            return prop("pos_x", x).prop("pos_y", y);
        }
        ui_node &text(std::string t) { return prop("text", std::move(t)); }
        ui_node &visible(const bool v) { return prop("visible", v); }

        // control properties
        ui_node &checked(const bool c) { return prop("checked", c); }
        ui_node &group(const long long g) { return prop("group", g); }
        ui_node &step(const long long s) { return prop("step", s); }
        ui_node &value(const long long v) { return prop("value", v); }
        ui_node &rows(const long long n) { return prop("rows", n); }

        // container properties (FlexPanel)
        ui_node &spacing(const long long s) { return prop("spacing", s); }
        ui_node &padding(const long long p) { return prop("padding", p); }
        ui_node &wrap(const bool w) { return prop("wrap", w); }
        ui_node &flex(const int g)
        {
            flex_grow = g;
            return *this;
        }
    };

    // --- fluent builders: one function per widget tag ------------------

    // containers (materialize into FlexPanel with the given direction)
    inline ui_node column(std::vector<ui_node> children = {})
    {
        ui_node n;
        n.type = "column";
        n.children = std::move(children);
        return n;
    }
    inline ui_node row(std::vector<ui_node> children = {})
    {
        ui_node n;
        n.type = "row";
        n.children = std::move(children);
        return n;
    }

    // fix-size multi-purpose container (Panel: positional children)
    inline ui_node panel(const long long width = 0, const long long height = 0)
    {
        ui_node n;
        n.type = "panel";
        if (width != 0 || height != 0)
        {
            n.size(width, height);
        }
        return n;
    }

    // leaf widgets
    inline ui_node label(std::string text = {})
    {
        ui_node n;
        n.type = "label";
        n.text(std::move(text));
        return n;
    }
    inline ui_node button(std::string text = {})
    {
        ui_node n;
        n.type = "button";
        n.text(std::move(text));
        return n;
    }
    inline ui_node checkbox(std::string text = {})
    {
        ui_node n;
        n.type = "checkbox";
        n.text(std::move(text));
        return n;
    }
    inline ui_node radio(std::string text = {}, const long long group = 0)
    {
        ui_node n;
        n.type = "radio";
        n.text(std::move(text)).group(group);
        return n;
    }
    inline ui_node slider(const long long min, const long long max)
    {
        ui_node n;
        n.type = "slider";
        n.prop("min", min).prop("max", max);
        return n;
    }
    inline ui_node progress_bar(const long long min, const long long max)
    {
        ui_node n;
        n.type = "progress_bar";
        n.prop("min", min).prop("max", max);
        return n;
    }
    inline ui_node list_box(std::vector<std::string> items, const long long rows = 4)
    {
        ui_node n;
        n.type = "list_box";
        n.items = std::move(items);
        n.rows(rows);
        return n;
    }
    inline ui_node text_input(std::string text = {})
    {
        ui_node n;
        n.type = "text_input";
        n.text(std::move(text));
        return n;
    }

    /*
     * Materializes a descriptive tree into the live widget tree:
     * host receives the root node's children; the root node's own tag is
     * documentation only (host is the actual container). Container
     * properties (spacing/padding/wrap) apply when host is a FlexPanel.
     * Returns host for chaining.
     */
    Widget &build(Widget &host, const ui_node &root);
}  // namespace zb::ui