#include "scroll_panel.hpp"

#include <algorithm>

namespace zb::ui
{
    int ScrollPanel::max_scroll() const
    {
        const auto s = get_size();
        int content_bottom = 0;
        for (const auto &item : get_items())
        {
            const auto p = item.child->get_position();
            const auto sz = item.child->get_size();
            content_bottom = std::max(content_bottom, p.y + sz.height);
        }
        return std::max(0, content_bottom - s.height);
    }

    void ScrollPanel::set_scroll_offset(const int v)
    {
        const int clamped = std::max(0, std::min(v, max_scroll()));
        if (clamped == top_)
        {
            return;
        }
        top_ = clamped;
        mark_dirty();
    }

    void ScrollPanel::layout()
    {
        FlexPanel::layout();
        // the content extent (or the viewport) may have shrunk under a
        // scrolled offset; set_scroll_offset clamps and marks dirty
        set_scroll_offset(top_);
    }

    bool ScrollPanel::scroll_by(const int delta)
    {
        const int next = std::max(0, std::min(top_ + delta, max_scroll()));
        if (next == top_)
        {
            return false;  // already at the end: the wheel falls through
        }
        top_ = next;
        mark_dirty();
        return true;
    }

    void ScrollPanel::scrollbar_rect(int *x0, int *y0, int *height,
                                     int *thumb_y, int *thumb_h) const
    {
        const auto s = get_size();
        *x0 = 0;
        *y0 = 0;
        *height = 0;
        *thumb_y = 0;
        *thumb_h = 0;
        const int max = max_scroll();
        if (max <= 0 || s.width <= gutter || s.height <= 2)
        {
            return;
        }
        const int track_h = s.height - 2;
        // proportional thumb (ListBox rule): viewport share of the
        // content, floored at the gutter width so it stays grabbable
        const int content_h = s.height + max;
        const int th = track_h * s.height / content_h;
        const int thumb = th < gutter ? gutter : th;
        *x0 = s.width - gutter;
        *y0 = 1;
        *height = track_h;
        *thumb_y = 1 + top_ * (track_h - thumb) / max;
        *thumb_h = thumb;
    }

    void ScrollPanel::draw_at(core::Graphics &area) const
    {
        // children draw shifted up by the offset inside a nested clip:
        // the requested area extends past the viewport bottom, and the
        // enclosing draw area (this panel's own box, set by Widget::draw)
        // cuts it back — the content scrolls while the viewport edge
        // stays hard
        const auto s = get_size();
        auto clip = area.clip_safe(0, -top_, s.width, s.height + top_);
        if (clip)
        {
            FlexPanel::draw_at(area);
        }

        // the scrollbar rides on top at the right edge
        const int max = max_scroll();
        if (max <= 0)
        {
            return;
        }
        const auto mask = theme().border;
        int x0, y0, h, thumb_y, thumb_h;
        scrollbar_rect(&x0, &y0, &h, &thumb_y, &thumb_h);
        area.draw_rect(x0, 0, s.width - 1, s.height - 1, mask);
        area.fill_rect(x0 + 1, thumb_y, s.width - 2, thumb_y + thumb_h - 1,
                       theme().accent);
    }

    Widget *ScrollPanel::pick(const int x, const int y)
    {
        // the thumb sits on top of the content and claims the press
        int x0, y0, h, thumb_y, thumb_h;
        scrollbar_rect(&x0, &y0, &h, &thumb_y, &thumb_h);
        if (thumb_h > 0 && x >= x0 && y >= thumb_y && y < thumb_y + thumb_h)
        {
            return this;
        }
        // children live in the unscrolled frame: undo the offset before
        // hit-testing (Panel::pick with the scroll translation)
        for (auto it = get_items().rbegin(); it != get_items().rend(); ++it)
        {
            Widget &child = *it->child;
            const auto p = child.get_position();
            if (child.hit(x - p.x, (y + top_) - p.y))
            {
                if (auto *inner = child.pick(x - p.x, (y + top_) - p.y))
                {
                    return inner;
                }
                return &child;
            }
        }
        return nullptr;
    }

    bool ScrollPanel::on_input(const zb::input::input_event &ev)
    {
        const auto pos = get_absolute_position();
        switch (ev.type)
        {
        case zb::input::input_type::mouse_left_down:
        case zb::input::input_type::touch_down:
        {
            const int x = ev.x - pos.x;
            const int y = ev.y - pos.y;
            int x0, y0, h, thumb_y, thumb_h;
            scrollbar_rect(&x0, &y0, &h, &thumb_y, &thumb_h);
            if (thumb_h > 0 && x >= x0 && y >= thumb_y && y < thumb_y + thumb_h)
            {
                dragging_ = true;
                drag_grab_ = y - thumb_y;
                return true;
            }
            return false;  // the content takes its own presses
        }
        case zb::input::input_type::mouse_move:
        case zb::input::input_type::touch_move:
        {
            if (!dragging_)
            {
                return false;
            }
            const int y = ev.y - pos.y;
            int x0, y0, h, thumb_y, thumb_h;
            scrollbar_rect(&x0, &y0, &h, &thumb_y, &thumb_h);
            const int travel = h - thumb_h;
            if (travel <= 0)
            {
                return false;
            }
            const int64_t scaled =
                static_cast<int64_t>(y - drag_grab_) *
                static_cast<int64_t>(max_scroll()) / travel;
            set_scroll_offset(static_cast<int>(scaled));
            return true;
        }
        case zb::input::input_type::mouse_left_up:
        case zb::input::input_type::touch_up:
        {
            if (dragging_)
            {
                dragging_ = false;
                return true;
            }
            return false;
        }
        case zb::input::input_type::mouse_wheel:
            // wheel up (delta > 0) scrolls toward the top
            return scroll_by(ev.delta > 0 ? -wheel_step : wheel_step);
        default:
            return false;
        }
    }
}
