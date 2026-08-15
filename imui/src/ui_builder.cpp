#include "ui_builder.hpp"

#include "button.hpp"
#include "checkbox.hpp"
#include "flex_panel.hpp"
#include "label.hpp"
#include "list_box.hpp"
#include "logging.hpp"
#include "panel.hpp"
#include "radio_button.hpp"
#include "slider.hpp"
#include "widget.hpp"

namespace zb::ui
{
    namespace
    {
        // --- tolerant value extraction (never throws) -------------------

        long long as_int(const prop_value &v, const long long fallback)
        {
            if (const auto *i = std::get_if<long long>(&v))
            {
                return *i;
            }
            return fallback;
        }
        bool as_bool(const prop_value &v)
        {
            if (const auto *b = std::get_if<bool>(&v))
            {
                return *b;
            }
            return false;
        }

        // prop `name`, or `fallback` when missing (kind must match the
        // builder that produced it; a wrong kind yields the fallback)
        template <class T>
        T prop_of(const ui_node &n, const char *name, const T &fallback)
        {
            for (const auto &[k, v] : n.props)
            {
                if (k != name)
                {
                    continue;
                }
                if constexpr (std::is_same_v<T, bool>)
                {
                    return as_bool(v);
                }
                if constexpr (std::is_same_v<T, long long>)
                {
                    return as_int(v, fallback);
                }
                if constexpr (std::is_same_v<T, std::string>)
                {
                    if (const auto *s = std::get_if<std::string>(&v))
                    {
                        return *s;
                    }
                    return fallback;
                }
            }
            return fallback;
        }

        // --- the tag table -----------------------------------------------

        // the string keys here are the ones a future designer-file
        // deserializer must emit; keep in sync with the builders in
        // ui_builder.hpp
        bool is_container_tag(const std::string &t)
        {
            return t == "column" || t == "row" || t == "panel";
        }

        // tag table -> concrete widget; the property table below can
        // static_cast safely because it only runs on widgets this
        // function made (no RTTI on NDS)
        std::unique_ptr<Widget> make_widget(const ui_node &n, bool *is_flex)
        {
            const std::string &t = n.type;
            *is_flex = (t == "column" || t == "row");
            if (t == "label")
            {
                return std::make_unique<Label>();
            }
            if (t == "button")
            {
                return std::make_unique<Button>();
            }
            if (t == "checkbox")
            {
                return std::make_unique<Checkbox>();
            }
            if (t == "radio")
            {
                return std::make_unique<RadioButton>();
            }
            if (t == "slider")
            {
                return std::make_unique<Slider>();
            }
            if (t == "list_box")
            {
                return std::make_unique<ListBox>();
            }
            if (t == "column" || t == "row")
            {
                return std::make_unique<FlexPanel>();
            }
            if (t == "panel")
            {
                return std::make_unique<Panel>();
            }
            LW << "ui_builder: unknown tag '" << t << "'; node skipped";
            return nullptr;
        }

        Checkbox *as_checkbox(Widget &w)
        {
            return static_cast<Checkbox *>(&w);
        }
        RadioButton *as_radio(Widget &w)
        {
            return static_cast<RadioButton *>(&w);
        }
        Slider *as_slider(Widget &w)
        {
            return static_cast<Slider *>(&w);
        }
        ListBox *as_list(Widget &w)
        {
            return static_cast<ListBox *>(&w);
        }
        FlexPanel *as_flex(Widget &w)
        {
            return static_cast<FlexPanel *>(&w);
        }
        Panel *as_panel(Widget &w)
        {
            return static_cast<Panel *>(&w);
        }

        // --- common properties (every widget) ---------------------------

        void apply_common(Widget &w, const ui_node &n)
        {
            if (!n.id.empty())
            {
                w.set_id(n.id);
            }
            const long long ww = prop_of(n, "width", 0LL);
            const long long hh = prop_of(n, "height", 0LL);
            if (ww != 0 || hh != 0)
            {
                w.set_size(static_cast<int>(ww), static_cast<int>(hh));
            }
            const long long px = prop_of(n, "pos_x", 0LL);
            const long long py = prop_of(n, "pos_y", 0LL);
            if (px != 0 || py != 0)
            {
                w.set_position(static_cast<int>(px), static_cast<int>(py));
            }
            const std::string text = prop_of(n, "text", std::string{});
            if (!text.empty())
            {
                w.set_text(text.c_str());
            }
            if (!prop_of(n, "visible", true))
            {
                w.set_visible(false);
            }
        }

        // --- control-specific properties --------------------------------

        void apply_control_props(Widget &w, const ui_node &n)
        {
            const std::string &t = n.type;
            if (t == "checkbox")
            {
                Checkbox &c = *as_checkbox(w);
                if (prop_of(n, "checked", false))
                {
                    c.set_checked(true);
                }
                return;
            }
            if (t == "radio")
            {
                RadioButton &r = *as_radio(w);
                r.set_group(static_cast<int>(prop_of(n, "group", 0LL)));
                if (prop_of(n, "checked", false))
                {
                    r.set_checked(true);
                }
                return;
            }
            if (t == "slider")
            {
                Slider &s = *as_slider(w);
                s.set_range(static_cast<int>(prop_of(n, "min", 0LL)),
                            static_cast<int>(prop_of(n, "max", 100LL)));
                s.set_step(static_cast<int>(prop_of(n, "step", 1LL)));
                return;
            }
            if (t == "list_box")
            {
                ListBox &l = *as_list(w);
                l.set_visible_rows(static_cast<size_t>(prop_of(n, "rows", 4LL)));
                if (!n.items.empty())
                {
                    l.set_items(n.items);
                }
                return;
            }
            if (t == "column" || t == "row")
            {
                FlexPanel &f = *as_flex(w);
                f.set_direction(t == "row" ? FlexPanel::flex_direction::row
                                           : FlexPanel::flex_direction::column);
                f.set_spacing(static_cast<int>(prop_of(n, "spacing", 0LL)));
                f.set_padding(static_cast<int>(prop_of(n, "padding", 0LL)));
                f.set_wrap(prop_of(n, "wrap", false));
            }
        }

        // --- materialization --------------------------------------------

        // materializes `n` into `container`; `container_is_flex` picks
        // the add_child signature
        void materialize(Widget &container, const bool container_is_flex, const ui_node &n)
        {
            bool is_flex = false;
            auto w = make_widget(n, &is_flex);
            if (w == nullptr)
            {
                return;
            }
            apply_common(*w, n);
            apply_control_props(*w, n);

            Widget *added = w.get();
            if (container_is_flex)
            {
                as_flex(container)->add_child(std::move(w), n.flex_grow);
            }
            else
            {
                as_panel(container)->add_child(std::move(w));
            }

            for (const ui_node &c : n.children)
            {
                if (!is_container_tag(c.type))
                {
                    LW << "ui_builder: '" << c.type
                       << "' is not a container tag; its children are dropped";
                }
                materialize(*added, is_flex, c);
            }
        }
    }  // namespace

    Widget &build(Widget &host, const ui_node &root)
    {
        // the host is the real container: the root tag (panel/row/column)
        // is documentation; the root's children materialize into it
        if (host.is_flex_container())
        {
            FlexPanel &f = *as_flex(host);
            f.set_spacing(static_cast<int>(prop_of(root, "spacing", 0LL)));
            f.set_padding(static_cast<int>(prop_of(root, "padding", 0LL)));
            f.set_wrap(prop_of(root, "wrap", false));
        }
        else
        {
            Panel &p = *as_panel(host);
            p.set_spacing(static_cast<int>(prop_of(root, "spacing", 0LL)));
            p.set_padding(static_cast<int>(prop_of(root, "padding", 0LL)));
        }
        for (const ui_node &c : root.children)
        {
            materialize(host, host.is_flex_container(), c);
        }
        return host;
    }
}  // namespace zb::ui