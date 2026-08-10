#include "widget.hpp"

#include "text/utf8.hpp"

namespace zb::ui
{
    void Widget::set_text(const char *text)
    {
        text_.clear();
        if (nullptr == text)
        {
            return;
        }
        text_ = utf8_to_utf16(text);
    }

    void Widget::draw(core::Graphics &g) const
    {
        if (!visible || size.width <= 0 || size.height <= 0)
        {
            return;
        }

        // clip_safe restricts g's draw area to this widget and restores it
        // on scope exit; no allocation per widget per frame
        auto area = g.clip_safe(position.x, position.y, size.width, size.height);
        if (!area)
        {
            return;
        }

        draw_background(g);
        draw_at(g);
    }

    void Widget::draw_background(core::Graphics &area) const
    {
        if (background.has_value())
        {
            area.fill(*background);
        }
        if (background_image.has_value())
        {
            area.draw_image(*background_image, 0, 0);
        }
    }

    const GlyphProvider *Widget::primary_provider() const
    {
        return primary_provider_ ? primary_provider_.get() : nullptr;
    }

    void Widget::draw_text(core::Graphics &area) const
    {
        if (text_.empty())
        {
            return;
        }

        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();

        // pick the provider for a code unit; nullptr means "not covered"
        const auto pick = [&](const char16_t ch) -> const GlyphProvider *
        {
            if (primary != nullptr && primary->covers(ch))
            {
                return primary;
            }
            if (fallback->covers(ch))
            {
                return fallback;
            }
            return nullptr;
        };

        const auto s = get_size();
        const char16_t *const data = text_.data();
        const int len = static_cast<int>(text_.size());

        // total advance: split into covered runs; uncovered units add 0
        int total = 0;
        for (int i = 0; i < len;)
        {
            const GlyphProvider *cur = pick(data[i]);
            int j = i;
            while (j < len && pick(data[j]) == cur)
            {
                ++j;
            }
            if (cur != nullptr)
            {
                total += cur->measure(data + i, j - i).width;
            }
            i = j;
        }

        // line metrics come from the primary provider, or the fallback;
        // line_metrics does not scan the string (no per-glyph loads)
        const text_metrics m = primary != nullptr
                                   ? primary->line_metrics()
                                   : fallback->line_metrics();

        int x = 0;
        switch (halign)
        {
        case h_align::left:
            x = 0;
            break;
        case h_align::center:
            x = (s.width - total) / 2;
            break;
        case h_align::right:
            x = s.width - total;
            break;
        }

        int y = 0;  // baseline of the first (only) line
        switch (valign)
        {
        case v_align::top:
            y = m.ascent;
            break;
        case v_align::center:
            y = (s.height - m.height) / 2 + m.ascent;
            break;
        case v_align::bottom:
            y = s.height - m.height + m.ascent;
            break;
        }

        // draw run by run; uncovered units keep the pen position
        int pen = 0;
        for (int i = 0; i < len;)
        {
            const GlyphProvider *cur = pick(data[i]);
            int j = i;
            while (j < len && pick(data[j]) == cur)
            {
                ++j;
            }
            if (cur != nullptr)
            {
                cur->write(area, data + i, j - i, x + pen, y, text_color);
                pen += cur->measure(data + i, j - i).width;
            }
            i = j;
        }
    }

    bool Widget::hit(const int x, const int y) const
    {
        return visible && x >= 0 && y >= 0 && x < size.width && y < size.height;
    }

    bool Widget::is_descendant_of(const Widget *ancestor) const
    {
        for (const Widget *w = this; w != nullptr; w = w->parent)
        {
            if (w == ancestor)
            {
                return true;
            }
        }
        return false;
    }
}
