#include "flex_panel.hpp"

#include <algorithm>
#include <utility>

namespace zb::ui
{
    namespace
    {
        // main axis = x for rows, y for columns
        bool is_row(const FlexPanel::flex_direction d)
        {
            return d == FlexPanel::flex_direction::row;
        }

        void set_main_size(Widget &w, const FlexPanel::flex_direction d, const int v)
        {
            // per-axis write: a grown main size must not clear an
            // explicit cross-axis size (set_size_auto clears both flags)
            if (is_row(d))
            {
                w.set_width_auto(v);
            }
            else
            {
                w.set_height_auto(v);
            }
        }

        // the layout demand of a child along the main axis: an explicit
        // set_size wins on that axis, otherwise the widget's measure()
        int main_demand(const Widget &w, const FlexPanel::flex_direction d)
        {
            const bool own = is_row(d) ? w.is_width_explicit() : w.is_height_explicit();
            const auto demand = own ? w.get_size() : w.measure();
            return is_row(d) ? demand.width : demand.height;
        }

        // the layout demand of a child along the cross axis
        int cross_demand(const Widget &w, const FlexPanel::flex_direction d)
        {
            const bool own = is_row(d) ? w.is_height_explicit() : w.is_width_explicit();
            const auto demand = own ? w.get_size() : w.measure();
            return is_row(d) ? demand.height : demand.width;
        }

        // the child's final main/cross size (after materialization)
        int main_now(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.get_size().width : w.get_size().height;
        }

        int cross_now(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.get_size().height : w.get_size().width;
        }

        // the percentage declared on the child's main/cross axis
        // (0 = the axis is not a percentage, batch L-4)
        int main_percent(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.width_percent() : w.height_percent();
        }

        int cross_percent(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.height_percent() : w.width_percent();
        }

        // the main-axis demand a child brings to line packing: a percent
        // child participates with its declared share of the content box,
        // otherwise its fixed demand (explicit axis or measure)
        int main_desired(const Widget &w, const FlexPanel::flex_direction d,
                         const int content_main)
        {
            const int pct = main_percent(w, d);
            return pct > 0 ? pct * content_main / 100 : main_demand(w, d);
        }

        void set_cross_size(Widget &w, const FlexPanel::flex_direction d, const int v)
        {
            if (is_row(d))
            {
                w.set_height_auto(v);
            }
            else
            {
                w.set_width_auto(v);
            }
        }

        void set_main_position(Widget &w, const FlexPanel::flex_direction d, const int main, const int cross)
        {
            if (is_row(d))
            {
                w.set_position(main, cross);
            }
            else
            {
                w.set_position(cross, main);
            }
        }
    }  // namespace

    std::unique_ptr<Widget> FlexPanel::remove_child(Widget *w)
    {
        for (auto it = items.begin(); it != items.end(); ++it)
        {
            if (it->child.get() == w)
            {
                std::unique_ptr<Widget> out = std::move(it->child);
                out->parent = nullptr;
                items.erase(it);
                mark_layout_dirty();
                return out;
            }
        }
        return nullptr;
    }

    void FlexPanel::clear_children()
    {
        for (const auto &item : items)
        {
            item.child->parent = nullptr;
        }
        items.clear();
        mark_layout_dirty();
    }

    core::imsize_t FlexPanel::measure() const
    {
        int main = 0;
        int cross = 0;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const Widget &child = *items[i].child;
            // flex items and percent children contribute nothing on their
            // open axis: their size only exists relative to a resolved
            // parent size, which a measure() has no access to
            const int m = (items[i].flex_grow > 0 || main_percent(child, direction) > 0)
                              ? 0
                              : main_demand(child, direction);
            main += m + (i == 0 ? 0 : spacing);
            cross = std::max(cross, cross_percent(child, direction) > 0
                                        ? 0
                                        : cross_demand(child, direction));
        }
        const int pad = 2 * padding;
        if (is_row(direction))
        {
            return {main + pad, cross + pad};
        }
        return {cross + pad, main + pad};
    }

    void FlexPanel::layout()
    {
        // the layout owns all child geometry writes: report the whole
        // container area (children cannot move outside its bounds)
        mark_dirty();
        const auto &s = get_size();
        const int avail_main = (is_row(direction) ? s.width : s.height) - 2 * padding;
        const int avail_cross = (is_row(direction) ? s.height : s.width) - 2 * padding;

        // cross-axis percent sizes resolve first, against the content-box
        // cross size: they have no sibling interaction and do not depend
        // on the line packing (batch L-4)
        for (const auto &item : items)
        {
            const int pct = cross_percent(*item.child, direction);
            if (pct > 0)
            {
                set_cross_size(*item.child, direction, std::max(0, pct * avail_cross / 100));
            }
        }

        // split the items into lines: a line breaks when the fixed demands
        // (plus spacing) exceed the available main-axis space; a percent
        // child participates with its desired share (the flex items absorb
        // leftover space, so they keep contributing 0)
        std::vector<std::vector<size_t>> lines;
        {
            std::vector<size_t> cur;
            int need = 0;
            for (size_t i = 0; i < items.size(); ++i)
            {
                const int pct = main_percent(*items[i].child, direction);
                const int item_need = (items[i].flex_grow > 0 && pct == 0)
                                          ? 0
                                          : main_desired(*items[i].child, direction, avail_main);
                if (wrap && !cur.empty() && need + spacing + item_need > avail_main)
                {
                    lines.push_back(std::move(cur));
                    cur.clear();
                    need = 0;
                }
                need += item_need + (cur.empty() ? 0 : spacing);
                cur.push_back(i);
            }
            if (!cur.empty())
            {
                lines.push_back(std::move(cur));
            }
        }
        if (lines.empty())
        {
            clear_layout_dirty();
            return;
        }

        // a line item claims main-axis space either as a flex grow
        // participant or as a fixed demand; a percent child is neither:
        // its size is resolved below and its grow weight is ignored
        const auto grows = [&](const size_t i) {
            return items[i].flex_grow > 0 && main_percent(*items[i].child, direction) == 0;
        };

        int cross_pos = padding;
        for (const auto &line : lines)
        {
            // resolve the percent children of the line first (batch L-4):
            // the fixed demands claim their space, each percent child
            // takes its declared share of the content box, and shares that
            // overflow the remainder are scaled into it proportionally to
            // their percentages -- the last percent child takes the
            // leftover pixel, so a scaled line sums exactly to the
            // remainder (the same exact-sum rule as the grow distribution)
            {
                int fixed = static_cast<int>(line.size() - 1) * spacing;
                int sum_desired = 0;
                for (const size_t i : line)
                {
                    const Widget &child = *items[i].child;
                    if (grows(i))
                    {
                        continue;  // absorbs leftover space, no fixed claim
                    }
                    const int want = main_desired(child, direction, avail_main);
                    if (main_percent(child, direction) > 0)
                    {
                        sum_desired += want;
                    }
                    else
                    {
                        fixed += want;
                    }
                }
                const int remaining = std::max(0, avail_main - fixed);
                if (sum_desired > remaining)
                {
                    int total_pct = 0;
                    int count = 0;
                    for (const size_t i : line)
                    {
                        const int pct = main_percent(*items[i].child, direction);
                        if (pct > 0)
                        {
                            total_pct += pct;
                            ++count;
                        }
                    }
                    int given = 0;
                    int seen = 0;
                    for (const size_t i : line)
                    {
                        const int pct = main_percent(*items[i].child, direction);
                        if (pct > 0)
                        {
                            ++seen;
                            const int share = (seen == count) ? remaining - given
                                                              : remaining * pct / total_pct;
                            set_main_size(*items[i].child, direction, std::max(0, share));
                            given += share;
                        }
                    }
                }
                else
                {
                    for (const size_t i : line)
                    {
                        Widget &child = *items[i].child;
                        if (main_percent(child, direction) > 0)
                        {
                            set_main_size(child, direction,
                                          main_desired(child, direction, avail_main));
                        }
                    }
                }
            }

            // give the flex items their share of the leftover space; integer
            // division drops the remainder, so the last flex item takes the
            // leftover pixels and the shares sum to exactly free
            int fixed = 0;
            int total_weight = 0;
            for (const size_t i : line)
            {
                if (grows(i))
                {
                    total_weight += items[i].flex_grow;
                }
                else
                {
                    const Widget &child = *items[i].child;
                    // a percent child's size was just resolved: its actual
                    // size is the fixed claim (not its desired share)
                    fixed += main_percent(child, direction) > 0
                                 ? main_now(child, direction)
                                 : main_demand(child, direction);
                }
            }
            fixed += static_cast<int>(line.size() - 1) * spacing;
            const int free = avail_main - fixed;

            int flex_count = 0;
            int flex_seen = 0;
            int distributed_size = 0;
            for (const size_t i : line)
            {
                if (grows(i))
                {
                    ++flex_count;
                }
            }
            for (const size_t i : line)
            {
                if (grows(i) && total_weight > 0)
                {
                    ++flex_seen;
                    const int grow = (flex_seen == flex_count)
                                         ? std::max(0, free) - distributed_size
                                         : std::max(0, free) * items[i].flex_grow / total_weight;
                    set_main_size(*items[i].child, direction, std::max(0, grow));
                    distributed_size += grow;
                }
            }

            // materialize the sizes of auto-sized children (per axis) so
            // hit-testing works; an axis with an explicit size keeps both
            // its value and its flag; grown main sizes (set above) are
            // already written; percent axes were resolved above
            for (const size_t i : line)
            {
                Widget &child = *items[i].child;
                const bool row = is_row(direction);
                const bool main_explicit = row ? child.is_width_explicit() : child.is_height_explicit();
                const bool cross_explicit = row ? child.is_height_explicit() : child.is_width_explicit();
                if (!main_explicit && main_percent(child, direction) == 0
                    && items[i].flex_grow == 0)
                {
                    const int demand = main_demand(child, direction);
                    if (row)
                    {
                        child.set_width_auto(demand);
                    }
                    else
                    {
                        child.set_height_auto(demand);
                    }
                }
                if (!cross_explicit && cross_percent(child, direction) == 0)
                {
                    const int cross = cross_demand(child, direction);
                    if (row)
                    {
                        child.set_height_auto(cross);
                    }
                    else
                    {
                        child.set_width_auto(cross);
                    }
                }
            }

            // place the line items along the main axis, spaced by their
            // final sizes
            int pen = padding;
            int line_cross = 0;
            for (const size_t i : line)
            {
                Widget &child = *items[i].child;
                line_cross = std::max(line_cross, cross_now(child, direction));
                pen += main_now(child, direction) + spacing;
            }
            pen = padding;
            for (const size_t i : line)
            {
                Widget &child = *items[i].child;
                set_main_position(child, direction, pen, cross_pos);
                pen += main_now(child, direction) + spacing;
                child.layout();
            }
            cross_pos += line_cross + spacing;
        }
        clear_layout_dirty();
    }

    void FlexPanel::draw_at(core::Graphics &area) const
    {
        for (const auto &item : items)
        {
            item.child->draw(area);
        }
    }

    Widget *FlexPanel::pick(const int x, const int y)
    {
        // children drawn later are on top, so search in reverse order
        for (auto it = items.rbegin(); it != items.rend(); ++it)
        {
            Widget &child = *it->child;
            const auto p = child.get_position();
            if (child.hit(x - p.x, y - p.y))
            {
                if (auto *inner = child.pick(x - p.x, y - p.y))
                {
                    return inner;
                }
                return &child;
            }
        }
        return nullptr;
    }
}  // namespace zb::ui
