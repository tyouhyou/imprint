#include "flex_panel.hpp"

#include <algorithm>

namespace zb::ui
{
    namespace
    {
        // main axis = x for rows, y for columns
        bool is_row(const FlexPanel::flex_direction d)
        {
            return d == FlexPanel::flex_direction::row;
        }

        int main_size(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.get_size().width : w.get_size().height;
        }

        int cross_size(const Widget &w, const FlexPanel::flex_direction d)
        {
            return is_row(d) ? w.get_size().height : w.get_size().width;
        }

        void set_main_size(Widget &w, const FlexPanel::flex_direction d, const int v)
        {
            if (is_row(d))
            {
                w.set_size(v, w.get_size().height);
            }
            else
            {
                w.set_size(w.get_size().width, v);
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

    void FlexPanel::layout()
    {
        const auto &s = get_size();
        const int avail_main = (is_row(direction) ? s.width : s.height) - 2 * padding;

        // split the items into lines: a line breaks when the fixed demands
        // (plus spacing) exceed the available main-axis space
        std::vector<std::vector<size_t>> lines;
        {
            std::vector<size_t> cur;
            int need = 0;
            for (size_t i = 0; i < items.size(); ++i)
            {
                const int item_need = items[i].flex_grow > 0 ? 0 : main_size(*items[i].child, direction);
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
            return;
        }

        int cross_pos = padding;
        for (const auto &line : lines)
        {
            // give the flex items their share of the leftover space; integer
            // division drops the remainder, so the last flex item takes the
            // leftover pixels and the shares sum to exactly free
            int fixed = 0;
            int total_weight = 0;
            for (const size_t i : line)
            {
                if (items[i].flex_grow > 0)
                {
                    total_weight += items[i].flex_grow;
                }
                else
                {
                    fixed += main_size(*items[i].child, direction);
                }
            }
            fixed += static_cast<int>(line.size() - 1) * spacing;
            const int free = avail_main - fixed;

            int flex_count = 0;
            int flex_seen = 0;
            int distributed_size = 0;
            for (const size_t i : line)
            {
                if (items[i].flex_grow > 0)
                {
                    ++flex_count;
                }
            }
            for (const size_t i : line)
            {
                if (items[i].flex_grow > 0 && total_weight > 0)
                {
                    ++flex_seen;
                    const int grow = (flex_seen == flex_count)
                                         ? std::max(0, free) - distributed_size
                                         : std::max(0, free) * items[i].flex_grow / total_weight;
                    set_main_size(*items[i].child, direction, std::max(0, grow));
                    distributed_size += grow;
                }
            }

            // place the line items along the main axis
            int pen = padding;
            int line_cross = 0;
            for (const size_t i : line)
            {
                Widget &child = *items[i].child;
                set_main_position(child, direction, pen, cross_pos);
                line_cross = std::max(line_cross, cross_size(child, direction));
                pen += main_size(child, direction) + spacing;
                child.layout();
            }
            cross_pos += line_cross + spacing;
        }
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
