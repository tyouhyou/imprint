#include "ui_builder.hpp"

#include "button.hpp"
#include "checkbox.hpp"
#include "flex_panel.hpp"
#include "gauge_dial.hpp"
#include "knob.hpp"
#include "label.hpp"
#include "list_box.hpp"
#include "logging.hpp"
#include "panel.hpp"
#include "progress_bar.hpp"
#include "radio_button.hpp"
#include "slider.hpp"
#include "svg_canvas.hpp"
#include "text/utf8.hpp"
#include "text_input.hpp"
#include "toggle_switch.hpp"
#include "trend_line.hpp"
#include "widget.hpp"
#include <string>
namespace zb::ui
{
    // the shared color resolver (declared in html.hpp, defined below at
    // this scope so the header stays light for the ui_embed tool)
    bool parse_color(const std::string &s, core::Color &out);

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

        // whether the node declares the property at all (a present 0 is
        // not the same as absent -- explicit geometry, batch K / N8)
        bool has_prop(const ui_node &n, const char *name)
        {
            for (const auto &p : n.props)
            {
                if (p.first == name)
                {
                    return true;
                }
            }
            return false;
        }

        // a gradient stop color: parse_color, except the literal
        // `transparent` is an explicit alpha-0 stop (parse_color
        // reports it false by design — no paintable color — which is
        // right for solid backgrounds but wrong inside a stop list)
        bool parse_stop_color(const std::string &s, core::Color &c)
        {
            if (parse_color(s, c))
            {
                return true;
            }
            std::string t;
            for (const char ch : s)
            {
                if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
                {
                    t.push_back(
                        static_cast<char>(ch >= 'A' && ch <= 'Z'
                                              ? ch - 'A' + 'a'
                                              : ch));
                }
            }
            if (t == "transparent")
            {
                c = core::Color{};
                return true;
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
                if constexpr (std::is_same_v<T, double>)
                {
                    if (const auto *d = std::get_if<double>(&v))
                    {
                        return *d;
                    }
                    if (const auto *i = std::get_if<long long>(&v))
                    {
                        return static_cast<double>(*i);
                    }
                    return fallback;
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

        // element opacity (P-2d): fixed-point 0..1000, default opaque
        long long elem_opacity(const ui_node &n)
        {
            const long long v = prop_of(n, "elem_opacity", 1000LL);
            return v < 0 ? 0 : (v > 1000 ? 1000 : v);
        }
        // fold opacity into a paint color (rounded up: any nonzero
        // stays nonzero — the set_a(v > 0) binary rule, so a dimmed
        // LED keeps its bit on 16bpp)
        core::Color fold_opacity(core::Color c, const long long op)
        {
            if (op < 1000)
            {
                const int v = static_cast<int>(c.a()) *
                              static_cast<int>(op);
                c.set_a(static_cast<uint8_t>(v <= 0 ? 0 : (v + 999) / 1000));
            }
            return c;
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
            if (t == "progress_bar")
            {
                return std::make_unique<ProgressBar>();
            }
            if (t == "toggle")
            {
                return std::make_unique<ToggleSwitch>();
            }
            if (t == "gauge")
            {
                return std::make_unique<GaugeDial>();
            }
            if (t == "knob")
            {
                return std::make_unique<Knob>();
            }
            if (t == "trend")
            {
                return std::make_unique<TrendLine>();
            }
            if (t == "svg")
            {
                return std::make_unique<SvgCanvas>();
            }
            if (t == "list_box")
            {
                return std::make_unique<ListBox>();
            }
            if (t == "text_input")
            {
                return std::make_unique<TextInput>();
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
        ProgressBar *as_progress_bar(Widget &w)
        {
            return static_cast<ProgressBar *>(&w);
        }
        ToggleSwitch *as_toggle(Widget &w)
        {
            return static_cast<ToggleSwitch *>(&w);
        }
        GaugeDial *as_gauge(Widget &w)
        {
            return static_cast<GaugeDial *>(&w);
        }
        Knob *as_knob(Widget &w)
        {
            return static_cast<Knob *>(&w);
        }
        TrendLine *as_trend(Widget &w)
        {
            return static_cast<TrendLine *>(&w);
        }
        SvgCanvas *as_svg(Widget &w)
        {
            return static_cast<SvgCanvas *>(&w);
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

        // the percent form of a geometry prop: "N%" -> N clamped into
        // 1..100 (0 = no percent declaration); any other value -> 0
        // (tolerated like every mistyped prop)
        int as_percent(const prop_value &v)
        {
            const auto *s = std::get_if<std::string>(&v);
            if (s == nullptr || s->size() < 2 || s->back() != '%')
            {
                return 0;
            }
            long long n = 0;
            for (std::size_t i = 0; i + 1 < s->size(); ++i)
            {
                const char c = (*s)[i];
                if (c < '0' || c > '9')
                {
                    return 0;
                }
                const int d = c - '0';
                // the same pre-multiply guard as ui_file.cpp parse_int:
                // without it an overlong digit run (n already past
                // (100-d)/10) overflows n*10+d before the clamp below
                if (n > (100 - d) / 10)
                {
                    return 100;  // an overlong run still clamps to 100
                }
                n = n * 10 + d;
            }
            return static_cast<int>(n);
        }

        // color value parsing lives at zb::ui scope below (parse_color,
        // shared by the background/color props and the HTML page box).

        // P-3 offset form: "N%" percent, "Npx"/bare pixels (signed);
        // false = malformed/unset. Range fits the int16 spec (the
        // Widget setter clamps percent/pixel ranges).
        bool parse_abs_offset(const std::string &s, int &v, bool &pct)
        {
            std::size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            {
                ++i;
            }
            std::size_t j = s.size();
            while (j > i && (s[j - 1] == ' ' || s[j - 1] == '\t'))
            {
                --j;
            }
            if (i >= j)
            {
                return false;
            }
            std::string t = s.substr(i, j - i);
            pct = false;
            if (!t.empty() && t.back() == '%')
            {
                pct = true;
                t.pop_back();
            }
            else if (t.size() > 2 && t[t.size() - 2] == 'p' &&
                     t[t.size() - 1] == 'x')
            {
                t.erase(t.size() - 2);
            }
            if (t.empty())
            {
                return false;
            }
            std::size_t k = 0;
            bool neg = false;
            if (t[0] == '+' || t[0] == '-')
            {
                neg = t[0] == '-';
                k = 1;
            }
            if (k >= t.size())
            {
                return false;
            }
            int n = 0;
            for (; k < t.size(); ++k)
            {
                if (t[k] < '0' || t[k] > '9')
                {
                    return false;
                }
                n = n * 10 + (t[k] - '0');
                if (n > 32767)
                {
                    return false;
                }
            }
            v = neg ? -n : n;
            return true;
        }

        // --- common properties (every widget) ---------------------------

        // box dress shared by apply_common and build(): gradient,
        // border, radius (the solid background and text color stay at
        // the call sites; this keeps the two paths from drifting)
        void apply_box_dress(Widget &w, const ui_node &n)
        {
            // element opacity folds into every paint color here (P-2d)
            const long long op = elem_opacity(n);
            // H-10 generated boxes: before_*/after_* props assemble a
            // paint-only box on the widget (no layout, no hit). Warns
            // once per failure class; unresolvable boxes stay detached.
            for (int pk = 0; pk < 2; ++pk)
            {
                const std::string pre = pk == 0 ? "before_" : "after_";
                const std::string wkey = pre + "w";
                const std::string hkey = pre + "h";
                const bool want =
                    has_prop(n, wkey.c_str()) ||
                    has_prop(n, hkey.c_str()) ||
                    has_prop(n, (pre + "l").c_str()) ||
                    has_prop(n, (pre + "t").c_str()) ||
                    has_prop(n, (pre + "r").c_str()) ||
                    has_prop(n, (pre + "b").c_str());
                if (!want)
                {
                    continue;
                }
                Widget::pseudo_spec ps;
                const char *const sk[4] = {"l", "t", "r", "b"};
                for (int si = 0; si < 4; ++si)
                {
                    const std::string key = pre + sk[si];
                    if (!has_prop(n, key.c_str()))
                    {
                        continue;
                    }
                    int v = 0;
                    bool pct = false;
                    if (!parse_abs_offset(
                            prop_of(n, key.c_str(), std::string{}), v, pct))
                    {
                        continue;
                    }
                    const int c = v < -32767 ? -32767
                                             : (v > 32767 ? 32767 : v);
                    ps.off[si] = static_cast<int16_t>(c);
                    ps.off_mask |= static_cast<uint8_t>(1U << si);
                    if (pct)
                    {
                        ps.off_pct |= static_cast<uint8_t>(1U << si);
                    }
                }
                const long long pw = prop_of(n, wkey.c_str(), -1LL);
                const long long ph = prop_of(n, hkey.c_str(), -1LL);
                if (pw >= 0 && pw <= 32767)
                {
                    ps.has_w = 1;
                    ps.w = static_cast<int16_t>(pw);
                }
                if (ph >= 0 && ph <= 32767)
                {
                    ps.has_h = 1;
                    ps.h = static_cast<int16_t>(ph);
                }
                const char *const mk[4] = {"ml", "mt", "mr", "mb"};
                // margin order on the node is l/t/r/b like the offsets
                const int mo[4] = {0, 1, 2, 3};
                for (int mi = 0; mi < 4; ++mi)
                {
                    const long long mv =
                        prop_of(n, (pre + mk[mi]).c_str(), 0LL);
                    ps.margin[mo[mi]] =
                        static_cast<int16_t>(mv < 0 ? 0
                                                   : (mv > 32767 ? 32767 : mv));
                }
                core::Color solid;
                const bool has_solid = parse_color(
                    prop_of(n, (pre + "background").c_str(), std::string{}),
                    solid);
                core::Color lfrom;
                core::Color lmid;
                core::Color lto;
                const bool has_lin3 =
                    parse_stop_color(
                        prop_of(n, (pre + "bg_lin_from").c_str(),
                                std::string{}),
                        lfrom) &&
                    parse_stop_color(
                        prop_of(n, (pre + "bg_lin3_mid").c_str(),
                                std::string{}),
                        lmid) &&
                    parse_stop_color(
                        prop_of(n, (pre + "bg_lin_to").c_str(), std::string{}),
                        lto);
                if (has_solid)
                {
                    ps.has_bg = 1;
                    ps.bg = fold_opacity(solid, op);
                }
                else if (has_lin3)
                {
                    ps.has_bg = 1;
                    ps.grad_kind = 5;
                    ps.bg = fold_opacity(lfrom, op);
                    ps.mid = fold_opacity(lmid, op);
                    ps.to = fold_opacity(lto, op);
                    ps.grad_mid_p = static_cast<uint8_t>(
                        prop_of(n, (pre + "bg_lin3_p").c_str(), 50LL) < 0
                            ? 0
                            : (prop_of(n, (pre + "bg_lin3_p").c_str(), 50LL) >
                                       100
                                   ? 100
                                   : prop_of(n, (pre + "bg_lin3_p").c_str(),
                                             50LL)));
                    ps.grad_h =
                        prop_of(n, (pre + "bg_lin_h").c_str(), true) ? 1 : 0;
                }
                else
                {
                    static bool warned_paint = false;
                    if (!warned_paint)
                    {
                        warned_paint = true;
                        LW << "ui_builder: pseudo box without supported "
                              "paint stays detached (H-10)";
                    }
                    continue;
                }
                if (prop_of(n, (pre + "radius_half").c_str(), false))
                {
                    ps.radius_kind = 2;
                }
                else if (has_prop(n, (pre + "radius_px").c_str()))
                {
                    const long long rp =
                        prop_of(n, (pre + "radius_px").c_str(), 0LL);
                    if (rp >= 0)
                    {
                        ps.radius_kind = 1;
                        ps.radius_px = static_cast<uint16_t>(
                            rp > 65535 ? 65535 : rp);
                    }
                }
                const int ang = static_cast<int>(
                    prop_of(n, (pre + "rot_ang").c_str(), 0LL));
                if (ang != 0)
                {
                    if (ps.grad_kind != 0)
                    {
                        static bool warned_rot = false;
                        if (!warned_rot)
                        {
                            warned_rot = true;
                            LW << "ui_builder: rotation on a non-plain "
                                  "pseudo box paints unrotated (H-10)";
                        }
                    }
                    else
                    {
                        const bool oxp =
                            prop_of(n, (pre + "rot_ox_pct").c_str(), true);
                        const bool oyp =
                            prop_of(n, (pre + "rot_oy_pct").c_str(), true);
                        ps.rot_ang = static_cast<int16_t>(
                            ang < -32768 ? -32768
                                         : (ang > 32767 ? 32767 : ang));
                        const long long oxv = prop_of(
                            n, (pre + "rot_ox").c_str(), oxp ? 50LL : 0LL);
                        const long long oyv = prop_of(
                            n, (pre + "rot_oy").c_str(), oyp ? 50LL : 0LL);
                        ps.rot_ox = static_cast<int16_t>(
                            oxv < -32768 ? -32768
                                         : (oxv > 32767 ? 32767 : oxv));
                        ps.rot_oy = static_cast<int16_t>(
                            oyv < -32768 ? -32768
                                         : (oyv > 32767 ? 32767 : oyv));
                        ps.rot_ox_pct = oxp ? 1 : 0;
                        ps.rot_oy_pct = oyp ? 1 : 0;
                    }
                }
                const bool sx = ps.has_w != 0 ||
                                ((ps.off_mask & 1U) != 0U &&
                                 (ps.off_mask & 4U) != 0U);
                const bool sy = ps.has_h != 0 ||
                                ((ps.off_mask & 2U) != 0U &&
                                 (ps.off_mask & 8U) != 0U);
                if (!sx || !sy)
                {
                    static bool warned_size = false;
                    if (!warned_size)
                    {
                        warned_size = true;
                        LW << "ui_builder: pseudo box without resolvable "
                              "size stays detached (H-10)";
                    }
                    continue;
                }
                w.set_pseudo(pk, ps);
            }
            // P-1 paint dressing: radial wins over linear when both are
            // set (contract); a mistyped half leaves the color unset
            if (has_prop(n, "bg_rad_from") && has_prop(n, "bg_rad_to"))
            {
                core::Color from;
                core::Color to;
                if (parse_color(prop_of(n, "bg_rad_from", std::string{}), from) &&
                    parse_color(prop_of(n, "bg_rad_to", std::string{}), to))
                {
                    w.set_background_radial(
                        static_cast<int>(prop_of(n, "bg_rad_cx", 50LL)),
                        static_cast<int>(prop_of(n, "bg_rad_cy", 50LL)),
                        fold_opacity(from, op),
                        static_cast<int>(prop_of(n, "bg_rad_from_p", 0LL)),
                        fold_opacity(to, op),
                        static_cast<int>(prop_of(n, "bg_rad_to_p", 100LL)));
                }
            }
            else if (has_prop(n, "bg_lin_from") && has_prop(n, "bg_lin_to"))
            {
                core::Color from;
                core::Color to;
                if (parse_color(prop_of(n, "bg_lin_from", std::string{}), from) &&
                    parse_color(prop_of(n, "bg_lin_to", std::string{}), to))
                {
                    w.set_background_linear(fold_opacity(from, op),
                                            fold_opacity(to, op),
                                            prop_of(n, "bg_lin_h", true));
                }
            }
            // P-2b conic: 2..4 stops ride bg_con_pN/cN (any missing
            // half drops the form, like the radial mistype rule)
            if (has_prop(n, "bg_con_from"))
            {
                int degs[4] = {0, 0, 0, 0};
                core::Color cols[4]{};
                int nstops = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const std::string ps =
                        "bg_con_p" + std::to_string(i);
                    const std::string cs =
                        "bg_con_c" + std::to_string(i);
                    if (!has_prop(n, ps.c_str()) ||
                        !has_prop(n, cs.c_str()))
                    {
                        break;
                    }
                    if (!parse_stop_color(
                            prop_of(n, cs.c_str(), std::string{}), cols[i]))
                    {
                        nstops = 0;
                        break;
                    }
                    degs[i] =
                        static_cast<int>(prop_of(n, ps.c_str(), 0LL));
                    ++nstops;
                }
                if (nstops >= 2)
                {
                    core::Color folded[4]{};
                    for (int i = 0; i < nstops; ++i)
                    {
                        folded[i] = fold_opacity(cols[i], op);
                    }
                    w.set_background_conic(
                        static_cast<int>(prop_of(n, "bg_con_from", 0LL)),
                        degs, folded, nstops);
                }
            }
            // P-2c three-stop linear: the mid section rides the sidecar
            // (kind 5); a mistyped mid keeps the ends-only base above
            if (has_prop(n, "bg_lin3_mid"))
            {
                core::Color from;
                core::Color mid;
                core::Color to;
                if (parse_stop_color(prop_of(n, "bg_lin_from", std::string{}),
                                     from) &&
                    parse_stop_color(prop_of(n, "bg_lin3_mid", std::string{}),
                                     mid) &&
                    parse_stop_color(prop_of(n, "bg_lin_to", std::string{}),
                                     to))
                {
                    w.set_background_linear3(
                        fold_opacity(from, op), fold_opacity(mid, op),
                        static_cast<int>(prop_of(n, "bg_lin3_p", 50LL)),
                        fold_opacity(to, op),
                        prop_of(n, "bg_lin_h", true));
                }
            }
            // N-stop linear: 4..8 percent stops ride bg_linN_pN/cN;
            // any missing half drops the form, the ends-only base
            // above survives
            if (has_prop(n, "bg_linN_n"))
            {
                const int nstops =
                    static_cast<int>(prop_of(n, "bg_linN_n", 0LL));
                if (nstops >= 4 && nstops <= 8)
                {
                    int pos[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                    core::Color cols[8]{};
                    int got = 0;
                    for (int i = 0; i < nstops; ++i)
                    {
                        const std::string ps =
                            "bg_linN_p" + std::to_string(i);
                        const std::string cs =
                            "bg_linN_c" + std::to_string(i);
                        if (!has_prop(n, ps.c_str()) ||
                            !has_prop(n, cs.c_str()))
                        {
                            break;
                        }
                        if (!parse_stop_color(
                                prop_of(n, cs.c_str(), std::string{}),
                                cols[got]))
                        {
                            got = 0;
                            break;
                        }
                        pos[got] = static_cast<int>(
                            prop_of(n, ps.c_str(), 0LL));
                        ++got;
                    }
                    if (got == nstops)
                    {
                        core::Color folded[8]{};
                        for (int i = 0; i < nstops; ++i)
                        {
                            folded[i] = fold_opacity(cols[i], op);
                        }
                        w.set_background_linearN(
                            pos, folded, nstops,
                            prop_of(n, "bg_lin_h", true));
                    }
                }
            }
            // P-2c repeating overlay: 2..6 stops ride bg_rep_pN/cN; any
            // missing half drops the overlay, the base above survives
            if (has_prop(n, "bg_rep_n"))
            {
                const int nstops =
                    static_cast<int>(prop_of(n, "bg_rep_n", 0LL));
                if (nstops >= 2 && nstops <= 6)
                {
                    int pos[6] = {0, 0, 0, 0, 0, 0};
                    core::Color cols[6]{};
                    int got = 0;
                    for (int i = 0; i < nstops; ++i)
                    {
                        const std::string ps =
                            "bg_rep_p" + std::to_string(i);
                        const std::string cs =
                            "bg_rep_c" + std::to_string(i);
                        if (!has_prop(n, ps.c_str()) ||
                            !has_prop(n, cs.c_str()))
                        {
                            break;
                        }
                        if (!parse_stop_color(
                                prop_of(n, cs.c_str(), std::string{}),
                                cols[i]))
                        {
                            got = 0;
                            break;
                        }
                        pos[i] =
                            static_cast<int>(prop_of(n, ps.c_str(), 0LL));
                        ++got;
                    }
                    if (got >= 2)
                    {
                        core::Color folded[6]{};
                        for (int i = 0; i < got; ++i)
                        {
                            folded[i] = fold_opacity(cols[i], op);
                        }
                        w.set_background_repeating(
                            prop_of(n, "bg_rep_h", false),
                            static_cast<int>(
                                prop_of(n, "bg_rep_period", 0LL)),
                            pos, folded, got);
                    }
                }
            }
            // P-2d top border: same grammar as border, own band
            if (has_prop(n, "border_t_w") && has_prop(n, "border_t_c"))
            {
                core::Color bc;
                if (parse_color(prop_of(n, "border_t_c", std::string{}), bc))
                {
                    w.set_top_border(
                        static_cast<int>(prop_of(n, "border_t_w", 0LL)),
                        fold_opacity(bc, op));
                }
            }
            // P-2e box shadows: up to two per kind ride sh_oN_/sh_iN_;
            // a mistyped color drops that shadow alone
            const char *const kinds[2] = {"sh_o", "sh_i"};
            for (int kd = 0; kd < 2; ++kd)
            {
                for (int i = 0; i < 2; ++i)
                {
                    const std::string base =
                        std::string(kinds[kd]) + std::to_string(i);
                    const std::string oxk = base + "_ox";
                    const std::string cyk = base + "_color";
                    if (!has_prop(n, oxk.c_str()) ||
                        !has_prop(n, cyk.c_str()))
                    {
                        break;
                    }
                    core::Color sc;
                    if (!parse_color(prop_of(n, cyk.c_str(), std::string{}),
                                     sc))
                    {
                        continue;
                    }
                    const int ox = static_cast<int>(
                        prop_of(n, oxk.c_str(), 0LL));
                    const int oy = static_cast<int>(
                        prop_of(n, (base + "_oy").c_str(), 0LL));
                    const int blur = static_cast<int>(
                        prop_of(n, (base + "_blur").c_str(), 0LL));
                    const int spread = static_cast<int>(
                        prop_of(n, (base + "_spread").c_str(), 0LL));
                    if (kd == 0)
                    {
                        w.add_shadow_outer(ox, oy, blur, spread,
                                           fold_opacity(sc, op));
                    }
                    else
                    {
                        w.add_shadow_inset(ox, oy, blur, spread,
                                           fold_opacity(sc, op));
                    }
                }
            }
            if (has_prop(n, "border_w") && has_prop(n, "border_color"))
            {
                core::Color bc;
                if (parse_color(prop_of(n, "border_color", std::string{}), bc))
                {
                    w.set_border(
                        static_cast<int>(prop_of(n, "border_w", 0LL)),
                        fold_opacity(bc, op));
                }
            }
            if (prop_of(n, "radius_half", false))
            {
                w.set_corner_radius_half();
            }
            else if (has_prop(n, "radius_px"))
            {
                w.set_corner_radius(
                    static_cast<int>(prop_of(n, "radius_px", 0LL)));
            }
        }

        void apply_common(Widget &w, const ui_node &n)
        {
            if (!n.id.empty())
            {
                w.set_id(n.id);
            }
            // presence-gated: a declared 0 is an explicit value, not an
            // omission (batch K / N8). A percent form ("N%", batch L-4)
            // is not a pixel declaration: it skips the pixel gate
            // (set_size would mark the axis explicit with a fallback 0
            // and poison resolvers that read the flag, e.g. abs sizing)
            // and applies as a bare declaration after.
            const int w_pct = as_percent(prop_of(n, "width", std::string{}));
            const int h_pct = as_percent(prop_of(n, "height", std::string{}));
            const bool has_wpx = has_prop(n, "width") && w_pct <= 0;
            const bool has_hpx = has_prop(n, "height") && h_pct <= 0;
            if (has_wpx || has_hpx)
            {
                if (has_wpx == has_hpx)
                {
                    // both declared (or neither leniently): two-axis set
                    w.set_size(static_cast<int>(prop_of(n, "width", 0LL)),
                               static_cast<int>(prop_of(n, "height", 0LL)));
                }
                else
                {
                    // a one-axis declaration keeps the widget's own other
                    // axis (its measure, e.g. a button's text height) --
                    // set_size marks both explicit, so clear the missing
                    // axis's flag right after
                    const int wp = static_cast<int>(prop_of(n, "width", 0LL));
                    const int hp = static_cast<int>(prop_of(n, "height", 0LL));
                    if (has_wpx)
                    {
                        w.set_size(wp, w.get_size().height);
                        w.set_height_auto(w.get_size().height);
                    }
                    else
                    {
                        w.set_size(w.get_size().width, hp);
                        w.set_width_auto(w.get_size().width);
                    }
                }
            }
            if (w_pct > 0)
            {
                w.set_width_percent(w_pct);
            }
            if (h_pct > 0)
            {
                w.set_height_percent(h_pct);
            }
            // aspect-ratio declaration (H-5): independent of the pixel
            // gate above (it needs no width/height prop of its own --
            // the cross axis may come from anywhere settled)
            if (prop_of(n, "aspect_w", 0LL) > 0 && prop_of(n, "aspect_h", 0LL) > 0)
            {
                w.set_aspect_ratio(static_cast<int>(prop_of(n, "aspect_w", 0LL)),
                                   static_cast<int>(prop_of(n, "aspect_h", 0LL)));
            }
            // in-flow margins (H-3): presence-gated so bare widgets stay
            // allocation-free (set_margin skips the all-zero case too)
            if (has_prop(n, "margin_t") || has_prop(n, "margin_r") ||
                has_prop(n, "margin_b") || has_prop(n, "margin_l"))
            {
                w.set_margin(static_cast<int>(prop_of(n, "margin_t", 0LL)),
                             static_cast<int>(prop_of(n, "margin_r", 0LL)),
                             static_cast<int>(prop_of(n, "margin_b", 0LL)),
                             static_cast<int>(prop_of(n, "margin_l", 0LL)));
            }
            if (has_prop(n, "pos_x") || has_prop(n, "pos_y"))
            {
                w.set_position(static_cast<int>(prop_of(n, "pos_x", 0LL)),
                               static_cast<int>(prop_of(n, "pos_y", 0LL)));
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
            core::Color c;
            if (parse_color(prop_of(n, "background", std::string{}), c))
            {
                w.set_background_color(fold_opacity(c, elem_opacity(n)));
            }
            apply_box_dress(w, n);
            // P-3 positioning: relative anchors, absolute leaves the flow
            // (offsets/translate resolve at layout against the anchor)
            const std::string ppos = prop_of(n, "position", std::string{});
            if (ppos == "absolute" || ppos == "relative")
            {
                if (ppos == "absolute")
                {
                    w.set_absolute();
                }
                else
                {
                    w.set_relative();
                }
                const char *const keys[6] = {"abs_l", "abs_t", "abs_r",
                                             "abs_b", "translate_x",
                                             "translate_y"};
                for (int k = 0; k < 6; ++k)
                {
                    if (!has_prop(n, keys[k]))
                    {
                        continue;
                    }
                    int v = 0;
                    bool pct = false;
                    if (!parse_abs_offset(
                            prop_of(n, keys[k], std::string{}), v, pct))
                    {
                        continue;
                    }
                    if (k < 4)
                    {
                        w.set_abs_offset(k, v, pct);
                    }
                    else
                    {
                        w.set_translate(k - 4, v, pct);
                    }
                }
            }
            if (parse_color(prop_of(n, "color", std::string{}), c))
            {
                w.set_text_color(fold_opacity(c, elem_opacity(n)));
            }
            // P-2a text dressing: tracking, double-strike bold, one
            // solid offset shadow (shadow color through parse_color,
            // so malformed drops the whole shadow)
            if (has_prop(n, "letter_px"))
            {
                w.set_letter_spacing(
                    static_cast<int>(prop_of(n, "letter_px", 0LL)));
            }
            if (prop_of(n, "bold", false))
            {
                w.set_bold(true);
            }
            if (has_prop(n, "shadow_color"))
            {
                core::Color sc;
                if (parse_color(prop_of(n, "shadow_color", std::string{}),
                                sc))
                {
                    w.set_text_shadow(
                        fold_opacity(sc, elem_opacity(n)),
                        static_cast<int>(prop_of(n, "shadow_dx", 0LL)),
                        static_cast<int>(prop_of(n, "shadow_dy", 0LL)));
                }
            }
            // per-widget font size (code-contract §2.4): presence-gated
            // bare pixel size; tolerance (never throws): out-of-range or
            // missing-family warns once and keeps the current provider.
            // Without IMCORE_HAS_TTF_RUNTIME has_font_family() is false,
            // so the declaration is silently ignored there (documented
            // degradation — no per-build branch at the call site).
            if (has_prop(n, "font_size"))
            {
                const long long px = prop_of(n, "font_size", 0LL);
                if (px > 0)
                {
                    if (px >= 1 && px <= 128 && has_font_family())
                    {
                        w.set_font_size(static_cast<int>(px));
                    }
                    else
                    {
                        static bool warned = false;
                        if (!warned)
                        {
                            warned = true;
                            LW << "ui_builder: font_size out of range 1..128 "
                                  "or no font family installed; kept the "
                                  "current provider";
                        }
                    }
                }
            }
        if(has_prop(n, "halign"))
        {
            const std::string halign = prop_of(n, "halign", std::string{});

            if(halign == "left")
            {
                w.set_h_align(Widget::h_align::left);
            }
            else if(halign == "center")
            {
                w.set_h_align(Widget::h_align::center);
            }
            else if(halign == "right")
            {
                w.set_h_align(Widget::h_align::right);
            }
        }
        if(has_prop(n, "valign"))
        {
            const std::string valign = prop_of(n, "valign", std::string{});

            if(valign == "top")
            {
                w.set_v_align(Widget::v_align::top);
            }
            else if(valign == "center")
            {
                w.set_v_align(Widget::v_align::center);
            }
            else if(valign == "bottom"){
                w.set_v_align(Widget::v_align::bottom);
            }
        }
        }

        // --- control-specific properties --------------------------------

        // shared FlexPanel configuration: direction, spacing, padding,
        // wrap, and the H-7a/H-7b integer justify/align codes (out of
        // range falls back to start, never UB). Used both when a
        // column/row node materializes its own widget and when a
        // column/row document root configures the build host (H-7 A+B:
        // a kept flex body grounds the host with its direction and
        // viewport-centering placement, not just spacing/padding).
        // Host transfer runs presence-gated: only props the root
        // declares reconfigure the host, so pre-configured host state
        // survives (the same presence rule as the geometry props and
        // the child margins). Direction still transfers — the row/column
        // tag itself declares it.
        void apply_flex_config(FlexPanel &f, const ui_node &n,
                               const bool presence_gated = false)
        {
            f.set_direction(n.type == "row" ? FlexPanel::flex_direction::row
                                            : FlexPanel::flex_direction::column);
            const auto gate = [presence_gated, &n](const char *key)
            {
                return !presence_gated || has_prop(n, key);
            };
            if (gate("spacing"))
            {
                f.set_spacing(static_cast<int>(prop_of(n, "spacing", 0LL)));
            }
            if (gate("padding"))
            {
                f.set_padding(static_cast<int>(prop_of(n, "padding", 0LL)));
            }
            // per-side padding (html shorthand/longhand fold): sides the
            // node declares win over the uniform base; undeclared sides
            // keep it. Only rewrites when at least one side is declared,
            // so the uniform setter stays the sole path for .ui/page roots.
            {
                static const char *const side_keys[4] = {
                    "padding_t", "padding_r", "padding_b", "padding_l"};
                const int base = static_cast<int>(prop_of(n, "padding", 0LL));
                int sides[4] = {base, base, base, base};
                bool any = false;
                for (int si = 0; si < 4; ++si)
                {
                    if (has_prop(n, side_keys[si]))
                    {
                        sides[si] = static_cast<int>(
                            prop_of(n, side_keys[si], 0LL));
                        any = true;
                    }
                }
                if (any)
                {
                    f.set_padding_sides(sides[0], sides[1], sides[2],
                                        sides[3]);
                }
            }
            if (gate("wrap"))
            {
                f.set_wrap(prop_of(n, "wrap", false));
            }
            if (gate("justify"))
            {
                const long long jc = prop_of(n, "justify", 0LL);
                f.set_justify_content((jc >= 0 && jc <= 4)
                                          ? static_cast<FlexPanel::justify>(jc)
                                          : FlexPanel::justify::start);
            }
            if (gate("align"))
            {
                const long long ac = prop_of(n, "align", 0LL);
                f.set_align_items((ac >= 0 && ac <= 3)
                                      ? static_cast<FlexPanel::align>(ac)
                                      : FlexPanel::align::start);
            }
        }

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
            if (t == "progress_bar")
            {
                ProgressBar &p = *as_progress_bar(w);
                p.set_range(static_cast<int>(prop_of(n, "min", 0LL)),
                            static_cast<int>(prop_of(n, "max", 100LL)));
                p.set_value(static_cast<int>(prop_of(n, "value", 0LL)));
                return;
            }
            if (t == "toggle")
            {
                if (prop_of(n, "checked", false))
                {
                    as_toggle(w)->set_checked(true);
                }
                return;
            }
            if (t == "gauge")
            {
                GaugeDial &g = *as_gauge(w);
                g.set_range(static_cast<int>(prop_of(n, "min", 0LL)),
                            static_cast<int>(prop_of(n, "max", 100LL)));
                g.set_value(static_cast<int>(prop_of(n, "value", 0LL)));
                return;
            }
            if (t == "knob")
            {
                Knob &k = *as_knob(w);
                k.set_range(static_cast<int>(prop_of(n, "min", 0LL)),
                            static_cast<int>(prop_of(n, "max", 100LL)));
                k.set_step(static_cast<int>(prop_of(n, "step", 1LL)));
                k.set_value(static_cast<int>(prop_of(n, "value", 0LL)));
                return;
            }
            if (t == "svg")
            {
                // the vector-dial subset: viewBox props plus structured
                // children (svg_line/svg_text) consumed here, never
                // recursed (see materialize)
                SvgCanvas &v = *as_svg(w);
                v.set_view_box(static_cast<int>(prop_of(n, "vb_x", 0LL)),
                               static_cast<int>(prop_of(n, "vb_y", 0LL)),
                               static_cast<int>(prop_of(n, "vb_w", 0LL)),
                               static_cast<int>(prop_of(n, "vb_h", 0LL)));
                // the svg element's own opacity multiplies every child
                // (the per-level product already folded at parse time)
                const long long base_alpha = prop_of(n, "opacity", 255LL);
                for (const ui_node &c : n.children)
                {
                    if (c.type == "svg_line")
                    {
                        core::Color stroke;
                        if (!parse_color(prop_of(c, "stroke", std::string{}), stroke))
                        {
                            continue;  // SVG default: no stroke = invisible
                        }
                        const long long alpha =
                            prop_of(c, "stroke_alpha", 255LL) * base_alpha / 255;
                        stroke.set_a(static_cast<uint8_t>(alpha));
                        SvgCanvas::Line l;
                        l.x1 = static_cast<int>(prop_of(c, "x1", 0LL));
                        l.y1 = static_cast<int>(prop_of(c, "y1", 0LL));
                        l.x2 = static_cast<int>(prop_of(c, "x2", 0LL));
                        l.y2 = static_cast<int>(prop_of(c, "y2", 0LL));
                        l.color = stroke;
                        l.width = prop_of(c, "stroke_w", 1.0);
                        l.round_caps = prop_of(c, "stroke_round", false);
                        v.add_line(l);
                    }
                    else if (c.type == "svg_text")
                    {
                        const std::string s = prop_of(c, "text", std::string{});
                        if (s.empty())
                        {
                            continue;
                        }
                        SvgCanvas::Text t;
                        t.text = utf8_to_utf16(s.c_str());
                        t.x = static_cast<int>(prop_of(c, "x", 0LL));
                        t.y = static_cast<int>(prop_of(c, "y", 0LL));
                        core::Color fill;
                        if (parse_color(prop_of(c, "fill", std::string{}), fill))
                        {
                            const long long alpha =
                                prop_of(c, "fill_alpha", 255LL) * base_alpha / 255;
                            fill.set_a(static_cast<uint8_t>(alpha));
                            t.color = fill;
                            t.has_color = true;
                        }
                        t.anchor = static_cast<int>(prop_of(c, "anchor", 0LL));
                        t.font_size = prop_of(c, "text_fs", 0.0);
                        v.add_text(t);
                    }
                }
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
                apply_flex_config(*as_flex(w), n);
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
                // H-7b: per-item cross hint rides the node (0 = auto);
                // out-of-range codes inherit, never UB
                const int sa = n.align_self;
                as_flex(container)->add_child(
                    std::move(w), n.flex_grow,
                    (sa >= 0 && sa <= 4)
                        ? static_cast<FlexPanel::self_align>(sa)
                        : FlexPanel::self_align::auto_,
                    n.flex_shrink, n.flex_basis_px, n.flex_basis_pct);
            }
            else
            {
                as_panel(container)->add_child(std::move(w));
            }

            // a leaf tag cannot host children: recursing would static_cast
            // it to a container (no RTTI) and write through a bogus
            // pointer -- drop the children instead (contract: leaf
            // children are dropped with a warning)
            if (n.type == "svg")
            {
                return;  // svg_line/svg_text already consumed above
            }
            if (!n.children.empty() && !is_container_tag(n.type))
            {
                LW << "ui_builder: '" << n.type
                   << "' is not a container tag; its children are dropped";
            }
            else
            {
                for (const ui_node &c : n.children)
                {
                    materialize(*added, is_flex, c);
                }
            }
        }
    }  // namespace

    // --- shared color value parsing (B2 export) -----------------------
    // accepts "#rgb", "#rrggbb", the named subset, or "transparent";
    // malformed strings and "transparent" yield false (nothing set --
    // default background / theme text stays). Returns false also for
    // an empty string (absent prop). Names and "transparent" are ASCII
    // case-insensitive (B4, HTML semantics; hex digits already were).

    bool parse_color(const std::string &s, core::Color &out)
    {
        if (s.empty())
        {
            return false;
        }
        std::string name = s;
        for (char &c : name)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (name == "transparent")
        {
            return false;
        }
        if (s[0] == '#')
        {
            auto hex = [](const char c) -> int {
                if (c >= '0' && c <= '9')
                {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f')
                {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F')
                {
                    return c - 'A' + 10;
                }
                return -1;
            };
            const std::size_t len = s.size() - 1;
            if (len == 3)
            {
                const int r = hex(s[1]);
                const int g = hex(s[2]);
                const int b = hex(s[3]);
                if (r < 0 || g < 0 || b < 0)
                {
                    return false;
                }
                out = core::Color::from(r * 17, g * 17, b * 17);
                return true;
            }
            if (len == 6)
            {
                const int r0 = hex(s[1]);
                const int r1 = hex(s[2]);
                const int g0 = hex(s[3]);
                const int g1 = hex(s[4]);
                const int b0 = hex(s[5]);
                const int b1 = hex(s[6]);
                if (r0 < 0 || r1 < 0 || g0 < 0 || g1 < 0 ||
                    b0 < 0 || b1 < 0)
                {
                    return false;
                }
                out = core::Color::from(r0 * 16 + r1, g0 * 16 + g1,
                                        b0 * 16 + b1);
                return true;
            }
            return false;
        }
        if (name == "black")
        {
            out = core::colors::Black;
        }
        else if (name == "white")
        {
            out = core::colors::White;
        }
        else if (name == "red")
        {
            out = core::colors::Red;
        }
        else if (name == "green")
        {
            out = core::colors::Green;
        }
        else if (name == "blue")
        {
            out = core::colors::Blue;
        }
        else if (name == "yellow")
        {
            out = core::Color::from(255, 255, 0);
        }
        else if (name == "gray" || name == "grey")
        {
            out = core::Color::from(128, 128, 128);
        }
        else if (name == "cyan")
        {
            out = core::Color::from(0, 255, 255);
        }
        else if (name == "magenta")
        {
            out = core::Color::from(255, 0, 255);
        }
        else if (name.compare(0, 4, "rgb(") == 0 ||
                 name.compare(0, 5, "rgba(") == 0)
        {
            // rgb()/rgba() comma form (contract P-1): components are
            // 0..255 integers, alpha a 0..1 decimal (>1 clamps opaque
            // like browsers); anything else is malformed
            const bool has_alpha = name[3] == 'a';
            const std::size_t open = s.find('(');
            const std::size_t close = s.rfind(')');
            if (open == std::string::npos || close == std::string::npos ||
                close <= open + 1)
            {
                return false;
            }
            const std::string body = s.substr(open + 1, close - open - 1);
            std::vector<std::string> parts;
            std::string cur;
            for (const char c : body)
            {
                if (c == ',')
                {
                    parts.push_back(cur);
                    cur.clear();
                }
                else
                {
                    cur.push_back(c);
                }
            }
            parts.push_back(cur);
            if (parts.size() != (has_alpha ? 4U : 3U))
            {
                return false;
            }
            auto to_byte = [](const std::string &t, int &v) {
                std::size_t i = 0;
                while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
                {
                    ++i;
                }
                std::size_t j = t.size();
                while (j > i && (t[j - 1] == ' ' || t[j - 1] == '\t'))
                {
                    --j;
                }
                if (i >= j)
                {
                    return false;
                }
                int n = 0;
                for (std::size_t k = i; k < j; ++k)
                {
                    if (t[k] < '0' || t[k] > '9')
                    {
                        return false;
                    }
                    n = n * 10 + (t[k] - '0');
                    if (n > 255)
                    {
                        return false;
                    }
                }
                v = n;
                return true;
            };
            int comp[3] = {0, 0, 0};
            for (int k = 0; k < 3; ++k)
            {
                if (!to_byte(parts[static_cast<std::size_t>(k)], comp[k]))
                {
                    return false;
                }
            }
            out = core::Color::from(comp[0], comp[1], comp[2]);
            if (has_alpha)
            {
                const std::string &a = parts[3];
                std::size_t i = 0;
                while (i < a.size() && (a[i] == ' ' || a[i] == '\t'))
                {
                    ++i;
                }
                std::size_t j = a.size();
                while (j > i && (a[j - 1] == ' ' || a[j - 1] == '\t'))
                {
                    --j;
                }
                if (i >= j)
                {
                    return false;
                }
                int whole = 0;
                int frac = 0;
                int frac_div = 1;
                bool dot = false;
                bool bad = false;
                for (std::size_t k = i; k < j; ++k)
                {
                    if (a[k] == '.' && !dot)
                    {
                        dot = true;
                        continue;
                    }
                    if (a[k] < '0' || a[k] > '9')
                    {
                        bad = true;
                        break;
                    }
                    if (!dot)
                    {
                        whole = whole * 10 + (a[k] - '0');
                    }
                    else if (frac_div < 1000)
                    {
                        frac = frac * 10 + (a[k] - '0');
                        frac_div *= 10;
                    }
                }
                if (bad)
                {
                    return false;
                }
                int alpha = 255;
                if (whole <= 0)
                {
                    alpha = frac * 255 / frac_div;
                }
                else if (whole == 1 && frac == 0)
                {
                    alpha = 255;
                }
                // whole > 1 (or 1.xxx) clamps to 255
                out.set_a(static_cast<uint8_t>(alpha));
            }
        }
        else
        {
            return false;
        }
        return true;
    }

    Widget &build(Widget &host, const ui_node &root)
    {
        // the host is the real container: the root tag (panel/row/column)
        // is documentation; a row/column root configures the host as its
        // own widget would be configured (direction + H-7 placement ride
        // along, so a kept flex body grounds the host); any other root
        // keeps the historical spacing/padding-only transfer. All of it
        // is presence-gated (see apply_flex_config): an omitted prop
        // never resets pre-configured host state.
        if (host.is_flex_container() &&
            (root.type == "row" || root.type == "column"))
        {
            apply_flex_config(*as_flex(host), root, true);
        }
        else if (host.is_flex_container())
        {
            FlexPanel &f = *as_flex(host);
            if (has_prop(root, "spacing"))
            {
                f.set_spacing(static_cast<int>(prop_of(root, "spacing", 0LL)));
            }
            if (has_prop(root, "padding"))
            {
                f.set_padding(static_cast<int>(prop_of(root, "padding", 0LL)));
            }
            if (has_prop(root, "wrap"))
            {
                f.set_wrap(prop_of(root, "wrap", false));
            }
        }
        else
        {
            Panel &p = *as_panel(host);
            if (has_prop(root, "spacing"))
            {
                p.set_spacing(static_cast<int>(prop_of(root, "spacing", 0LL)));
            }
            if (has_prop(root, "padding"))
            {
                p.set_padding(static_cast<int>(prop_of(root, "padding", 0LL)));
            }
        }
        // the root's own box dress styles the host (contract: document
        // frame paints on the host; geometry never transfers)
        core::Color c;
        if (parse_color(prop_of(root, "background", std::string{}), c))
        {
            host.set_background_color(fold_opacity(c, elem_opacity(root)));
        }
        apply_box_dress(host, root);
        if (parse_color(prop_of(root, "color", std::string{}), c))
        {
            host.set_text_color(fold_opacity(c, elem_opacity(root)));
        }
        for (const ui_node &c : root.children)
        {
            materialize(host, host.is_flex_container(), c);
        }
        return host;
    }
}  // namespace zb::ui
