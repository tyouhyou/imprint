#pragma once

#include <functional>
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
        int flex_shrink = 0;             // deficit share weight (H-7c)
        int flex_basis_px = -1;          // explicit pixel basis (H-7c)
        int flex_basis_pct = 0;          // percent basis 1..100 (H-7c)
        int align_self = 0;              // per-item cross hint (H-7b:
                                         // 0 = auto/inherit, else
                                         // FlexPanel::align ordinal + 1)

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
        // percentage geometry (batch L-4): the axis becomes a percentage
        // of the FlexPanel parent's content box, resolved at layout
        // (docs/code-contract.md 3); stored as the same "N%" string form
        // the .ui parser produces
        ui_node &width_pct(const long long pct)
        {
            return prop("width", std::to_string(pct) + "%");
        }
        ui_node &height_pct(const long long pct)
        {
            return prop("height", std::to_string(pct) + "%");
        }
        // aspect-ratio declaration (H-5): derived-axis sizing in FlexPanel
        ui_node &aspect(const long long w, const long long h)
        {
            return prop("aspect_w", w).prop("aspect_h", h);
        }
        ui_node &pos(const long long x, const long long y)
        {
            return prop("pos_x", x).prop("pos_y", y);
        }
        ui_node &text(std::string t) { return prop("text", std::move(t)); }
        ui_node &visible(const bool v) { return prop("visible", v); }
        // color props (batch H): color value strings in the resolution
        // table's accepted forms (see docs/design-file.md); malformed or
        // "transparent" values are no-ops at materialize time
        ui_node &background(std::string c) { return prop("background", std::move(c)); }
        ui_node &color(std::string c) { return prop("color", std::move(c)); }
        // type size (code-contract §2.4): bare pixel size 1..128, resolved
        // at materialize against the process font family (tolerance:
        // out-of-range/missing-family keeps the current provider)
        ui_node &font_size(const long long px) { return prop("font_size", px); }

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
        ui_node &justify(const long long j) { return prop("justify", j); }
        ui_node &flex(const int g)
        {
            flex_grow = g;
            return *this;
        }
        ui_node &shrink(const int s)
        {
            flex_shrink = s;
            return *this;
        }
        ui_node &basis_px(const int px)
        {
            flex_basis_px = px;
            return *this;
        }
        ui_node &basis_pct(const int pct)
        {
            flex_basis_pct = pct;
            return *this;
        }
        ui_node &self_align(const int a)
        {
            align_self = a;
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
    inline ui_node toggle(const bool checked_value = false)
    {
        ui_node n;
        n.type = "toggle";
        n.checked(checked_value);
        return n;
    }
    inline ui_node gauge(const long long min, const long long max)
    {
        ui_node n;
        n.type = "gauge";
        n.prop("min", min).prop("max", max);
        return n;
    }
    inline ui_node knob(const long long min, const long long max)
    {
        ui_node n;
        n.type = "knob";
        n.prop("min", min).prop("max", max);
        return n;
    }
    inline ui_node trend()
    {
        ui_node n;
        n.type = "trend";
        return n;
    }
    // vector-dial subset (the HTML svg/vectordial front-end builds these;
    // children are svg_line/svg_text nodes consumed at materialize time)
    inline ui_node svg()
    {
        ui_node n;
        n.type = "svg";
        return n;
    }
    inline ui_node svg_line(const long long x1, const long long y1,
                            const long long x2, const long long y2,
                            std::string stroke = {}, const long long alpha = 255)
    {
        ui_node n;
        n.type = "svg_line";
        n.prop("x1", x1).prop("y1", y1).prop("x2", x2).prop("y2", y2);
        n.prop("stroke", std::move(stroke)).prop("stroke_alpha", alpha);
        return n;
    }
    inline ui_node svg_text(const long long x, const long long y,
                            std::string text = {}, std::string fill = {},
                            const long long anchor = 0)
    {
        ui_node n;
        n.type = "svg_text";
        n.prop("x", x).prop("y", y);
        n.text(std::move(text)).prop("fill", std::move(fill));
        n.prop("fill_alpha", 255LL).prop("anchor", anchor);
        return n;
    }
    // one flattened subpath ("x,y x,y ..." viewBox units, decimals;
    // the html converter emits these from `d`, code-contract §3.3)
    inline ui_node svg_path(std::string pts, std::string stroke = {},
                            const bool closed = false,
                            const long long alpha = 255)
    {
        ui_node n;
        n.type = "svg_path";
        n.prop("pts", std::move(pts));
        n.prop("stroke", std::move(stroke));
        n.prop("closed", closed);
        n.prop("stroke_alpha", alpha);
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

    /*
     * Action binding (code-contract §4, P3): walks the same ui_node IR
     * build() materialized and, for every node carrying a non-empty id,
     * subscribes that widget's primary action event to `sink(id)` — the
     * concrete event per tag is chosen in ui_builder.cpp beside the tag
     * table (button click, checkbox/toggle/radio/slider/list_box change,
     * text_input submit). A binder, not a registry: the framework holds
     * no id→handler map, the host maps ids on its side. Handlers live as
     * long as the widget does. Returns the number of bound actions.
     */
    using action_fn = std::function<void(const std::string &id)>;
    int bind_actions(Widget &root, const ui_node &node, const action_fn &sink);

    /*
     * Whether the node tree materializes at least one widget (any known
     * tag): the create-time validation for design files — unknown tags
     * parse ok but produce nothing, so "yields no widget" is decided
     * here, beside the same tag table. No live tree needed.
     */
    bool materializes_widget(const ui_node &node);
}  // namespace zb::ui