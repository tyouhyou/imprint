#include "group_box.hpp"
#include <algorithm>

namespace zb::ui
{
    GroupBox::GroupBox()
    {
        set_padding(8);
    }

    void GroupBox::set_title(const std::string &value)
    {
        if (title == value)
        {
            return;
        }
        title = value;
        set_text(value.c_str());
    }

    void GroupBox::draw_at(core::Graphics &area) const
    {
        FlexPanel::draw_at(area);

        const auto dimensions = get_size();
        const int text_width = text_advance();
        const int text_h = text_height();
        const int border_y = text_h > 0 ? text_h / 2 : 4;
        const bool has_title_gap =
            !get_text().empty() && dimensions.width > 8 && text_width > 0 &&
            text_h > 0;
        const int gap_right =
            has_title_gap ? std::min(dimensions.width - 2, 8 + text_width) : 0;

        if (dimensions.width >= 2 && dimensions.height >= (border_y + 2))
        {
            if (has_title_gap)
            {
                // Left segment of top border
                area.draw_line(0, border_y, 7, border_y, theme().border);
                // Right segment of top border (after title text gap)
                if (gap_right < dimensions.width - 2)
                {
                    area.draw_line(gap_right, border_y, dimensions.width - 1, border_y,
                                   theme().border);
                }
                // Side and bottom borders
                area.draw_line(0, border_y, 0, dimensions.height - 1,
                               theme().border);
                area.draw_line(0, dimensions.height - 1, dimensions.width - 1,
                               dimensions.height - 1, theme().border);
                area.draw_line(dimensions.width - 1, border_y, dimensions.width - 1,
                               dimensions.height - 1, theme().border);
            }
            else
            {
                area.draw_rect(0, border_y, dimensions.width - 1,
                               dimensions.height - 1, theme().border);
            }
        }

        if (!get_text().empty())
        {
            draw_text_at(area, get_text().data(),
                         static_cast<int>(get_text().size()), 8,
                         text_ascent());
        }
    }
}  // namespace zb::ui
