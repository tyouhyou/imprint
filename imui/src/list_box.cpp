#include "list_box.hpp"

#include "logging.hpp"
#include "text/text_image.hpp"

namespace zb::ui
{
    namespace
    {
        // static string model callback (see ListBox::set_items)
        std::string static_items_text(const void *arg, const size_t i)
        {
            const auto &items = *static_cast<const std::vector<std::string> *>(arg);
            return i < items.size() ? items[i] : std::string{};
        }
    }  // namespace

    void ListBox::set_row_height(const int h)
    {
        if (h < 1)
        {
            LW << "list: row height must be >= 1; clamping";
            row_height = 1;
        }
        else
        {
            row_height = h;
        }
        invalidate_row_cache();
        // the widget height is derived from the row height: keep it
        // consistent (set_size also invalidates the layout)
        auto s = get_size();
        s.height = static_cast<int>(visible) * row_height;
        set_size(s.width, s.height);
        mark_dirty();
        clamp_top();
    }

    void ListBox::set_visible_rows(const size_t rows)
    {
        if (rows == 0)
        {
            LW << "list: zero visible rows; clamping to 1";
            visible = 1;
        }
        else
        {
            visible = rows;
        }
        invalidate_row_cache();
        auto s = get_size();
        s.height = static_cast<int>(visible) * row_height;
        set_size(s.width, s.height);
        mark_dirty();
        clamp_top();
    }

    void ListBox::set_item_count(const size_t n)
    {
        if (count == n)
        {
            return;
        }
        invalidate_row_cache();
        mark_dirty();
        count = n;
        clamp_top();
        if (value >= count && value != invalid)
        {
            value = invalid;
        }
    }

    void ListBox::set_items(std::vector<std::string> items)
    {
        items_ = std::make_shared<std::vector<std::string>>(std::move(items));
        set_item_count(items_->size());
        set_item_text(static_items_text, items_.get());
    }

    void ListBox::set_value(const size_t v)
    {
        if (v != invalid && v >= count)
        {
            return;
        }
        if (value == v)
        {
            return;
        }
        invalidate_row_cache();
        mark_dirty();
        value = v;
        if (value != invalid)
        {
            reveal(value);
        }
    }

    size_t ListBox::max_top() const
    {
        return count > visible ? count - visible : 0;
    }

    void ListBox::clamp_top()
    {
        if (top > max_top())
        {
            top = max_top();
        }
    }

    void ListBox::reveal(const size_t sel)
    {
        if (sel < top)
        {
            top = sel;
        }
        else if (sel >= top + visible)
        {
            top = sel + 1 - visible;
        }
        clamp_top();
    }

    bool ListBox::step_selection(const int dir)
    {
        if (count == 0)
        {
            return false;
        }
        size_t next;
        if (value == invalid)
        {
            // nothing selected: pick the row at the window top
            next = top;
        }
        else
        {
            if ((dir > 0 && value + 1 >= count) || (dir < 0 && value == 0))
            {
                return false;
            }
            next = value + static_cast<size_t>(dir);
        }
        if (next == value)
        {
            return false;
        }
        invalidate_row_cache();
        mark_dirty();
        value = next;
        reveal(value);
        changed(value);
        return true;
    }

    bool ListBox::scroll_rows(const int dir)
    {
        if (count <= visible)
        {
            return false;
        }
        const int64_t t = static_cast<int64_t>(top) -
                          static_cast<int64_t>(dir) * scroll_step;
        int64_t next = t < 0 ? 0 : t;
        const int64_t mx = static_cast<int64_t>(max_top());
        if (next > mx)
        {
            next = mx;
        }
        if (static_cast<size_t>(next) == top)
        {
            return false;
        }
        invalidate_row_cache();
        mark_dirty();
        top = static_cast<size_t>(next);
        return true;
    }

    bool ListBox::on_input(const zb::input::input_event &ev)
    {
        const auto pos = get_absolute_position();
        switch (ev.type)
        {
        case zb::input::input_type::mouse_left_down:
        case zb::input::input_type::touch_down:
        {
            const int x = ev.x - pos.x;
            const int y = ev.y - pos.y;
            int tx, ty, th, thumb_y, thumb_h;
            scrollbar_rect(&tx, &ty, &th, &thumb_y, &thumb_h);
            if (x >= tx && y >= thumb_y && y < thumb_y + thumb_h)
            {
                dragging = true;
                drag_grab = y - thumb_y;
                return true;
            }
            const size_t row = row_at(x, y);
            if (row == invalid || row >= count)
            {
                return false;
            }
            if (row == value)
            {
                return false;
            }
            invalidate_row_cache();
            mark_dirty();
            value = row;
            changed(value);
            return true;
        }
        case zb::input::input_type::mouse_move:
        case zb::input::input_type::touch_move:
        {
            if (!dragging)
            {
                return false;
            }
            const int y = ev.y - pos.y;
            int tx, ty, th, thumb_y, thumb_h;
            scrollbar_rect(&tx, &ty, &th, &thumb_y, &thumb_h);
            const int travel = th - thumb_h;
            if (travel <= 0)
            {
                return false;
            }
            const int64_t scaled =
                static_cast<int64_t>(y - drag_grab) *
                static_cast<int64_t>(max_top()) / travel;
            int64_t next = scaled < 0 ? 0 : scaled;
            const int64_t mx = static_cast<int64_t>(max_top());
            if (next > mx)
            {
                next = mx;
            }
            if (static_cast<size_t>(next) == top)
            {
                return false;
            }
            invalidate_row_cache();
            mark_dirty();
            top = static_cast<size_t>(next);
            return true;
        }
        case zb::input::input_type::mouse_left_up:
        case zb::input::input_type::touch_up:
        {
            if (dragging)
            {
                dragging = false;
                return true;
            }
            return false;
        }
        case zb::input::input_type::mouse_wheel:
            // wheel up (delta > 0) shows earlier rows: top decreases
            return scroll_rows(ev.delta > 0 ? 1 : -1);
        case zb::input::input_type::key_down:
            if (ev.key == static_cast<int>(zb::input::key_code::up))
            {
                return step_selection(-1);
            }
            if (ev.key == static_cast<int>(zb::input::key_code::down))
            {
                return step_selection(1);
            }
            return false;
        default:
            return false;
        }
    }

    void ListBox::scrollbar_rect(int *x0, int *y0, int *height,
                                 int *thumb_y, int *thumb_h) const
    {
        const auto s = get_size();
        *x0 = 0;
        *y0 = 0;
        *height = 0;
        *thumb_y = 0;
        *thumb_h = 0;
        if (count <= visible || s.width <= gutter)
        {
            return;
        }
        const int track_h = s.height - 2;
        const int travel = count - visible;
        const int th = track_h * static_cast<int>(visible) / static_cast<int>(count);
        const int thumb = th < 8 ? 8 : th;
        *x0 = s.width - gutter;
        *y0 = 1;
        *height = track_h;
        *thumb_y = 1 + (travel > 0
                            ? static_cast<int>(top) * (track_h - thumb) / travel
                            : 0);
        *thumb_h = thumb;
    }

    size_t ListBox::row_at(const int x, const int y) const
    {
        const auto s = get_size();
        if (x < 0 || x >= s.width || y < 0 || y >= s.height)
        {
            return invalid;
        }
        // the scrollbar gutter is not the row area: a click on the
        // track (above or below the thumb) must not select the row that
        // happens to be rendered under the bar
        if (count > visible && s.width > gutter && x >= s.width - gutter)
        {
            return invalid;
        }
        const size_t r = top + static_cast<size_t>(y / row_height);
        return r;
    }

    const ListBox::row_cache_entry *ListBox::find_row_cache(
        const size_t row, const bool sel, const int w, const int h,
        const core::Color &fg, const core::Color &bg) const
    {
        for (const auto &e : row_cache_)
        {
            if (e.row == row && e.sel == sel && e.w == w && e.h == h &&
                e.fg.pixel == fg.pixel && e.bg.pixel == bg.pixel)
            {
                return &e;
            }
        }
        return nullptr;
    }

    void ListBox::insert_row_cache(row_cache_entry &&e) const
    {
        // byte accounting: 16bpp builds carry half-size pixels
        const size_t bytes = static_cast<size_t>(e.w) * static_cast<size_t>(e.h) *
                             sizeof(core::Color);
        if (row_cache_bytes_ + bytes > row_cache_budget)
        {
            // over budget: drop everything, the next paint rebuilds; the
            // cache is a fast path, a worst-case blip is fine
            row_cache_.clear();
            row_cache_bytes_ = 0;
        }
        row_cache_bytes_ += bytes;
        row_cache_.push_back(std::move(e));
    }

    void ListBox::draw_at(core::Graphics &area) const
    {
        const auto &th = theme();
        const auto s = get_size();
        area.fill_rect(0, 0, s.width - 1, s.height - 1, th.field_bg);

        const bool bar = count > visible && s.width > gutter;
        const int text_w = bar ? s.width - gutter : s.width;

        // rows: the visible window of the model
        const size_t r0 = std::min(top, count);
        const size_t r1 = std::min(count, top + visible);
        for (size_t r = r0; r < r1; ++r)
        {
            const int y0 = static_cast<int>(r - top) * row_height;
            const bool sel = r == value;
            const core::Color bg = sel ? th.selection : th.field_bg;
            const core::Color fg = sel ? th.text_inverted : th.text;
            if (sel)
            {
                area.fill_rect(0, y0, text_w - 1, y0 + row_height - 1, bg);
            }
            if (text_fn != nullptr)
            {
                core::image_t row;
                if (const auto *hit = find_row_cache(r, sel, text_w, row_height, fg, bg))
                {
                    row = {hit->img->data(), hit->w, hit->h, 0};
                }
                else
                {
                    std::string text = text_fn(text_arg, r);
                    auto img = make_text_image(text.c_str(), text_w, row_height, fg, bg);
                    const auto sz = img->size();
                    row = {img->data(), sz.width, sz.height, 0};
                    insert_row_cache(row_cache_entry{r, sel, text_w, row_height, fg, bg,
                                                     std::move(img)});
                }
                area.draw_image(row, 0, y0);
            }
        }

        // scrollbar
        if (bar)
        {
            int tx, ty, th_height, thumb_y, thumb_h;
            scrollbar_rect(&tx, &ty, &th_height, &thumb_y, &thumb_h);
            area.fill_rect(tx, ty, s.width - 1, ty + th_height - 1, th.scroll_track);
            area.fill_rect(tx, thumb_y, s.width - 1, thumb_y + thumb_h - 1,
                           th.scroll_thumb);
        }

        if (is_focused())
        {
            area.draw_rect(0, 0, s.width - 1, s.height - 1, th.focus_mark);
        }
    }
}  // namespace zb::ui