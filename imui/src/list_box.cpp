#include "list_box.hpp"

#include <algorithm>

#include "logging.hpp"
#include "scale.hpp"
#include "text/bitmap_provider.hpp"
#include "text/text_image.hpp"
#include "text/utf8.hpp"

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
        // consistent (set_size also invalidates the layout). The width
        // axis keeps its explicitness (L-3): a declarative list without
        // a width must stay measured instead of freezing the pre-layout
        // 0 that set_size would otherwise mark explicit.
        const bool w_auto = !is_width_explicit();
        auto s = get_size();
        // a height provider owns the geometry (§3.7): the fixed
        // row_height no longer derives the widget height
        s.height = height_fn != nullptr
                     ? detail::clamp_int(visible_height(visible))
                     : static_cast<int>(visible) * row_height;
        set_size(s.width, s.height);
        if (w_auto)
        {
            set_width_auto(s.width);
        }
        mark_dirty();
        clamp_top();
    }

    void ListBox::set_visible_rows(const size_t rows)
    {
        // guard the height math: visible * row_height must fit int
        constexpr size_t k_max_rows = 100000;
        if (rows == 0)
        {
            LW << "list: zero visible rows; clamping to 1";
            visible = 1;
        }
        else if (rows > k_max_rows)
        {
            LW << "list: " << rows << " visible rows exceeds cap; clamping";
            visible = k_max_rows;
        }
        else
        {
            visible = rows;
        }
        invalidate_row_cache();
        // L-3: keep the width axis explicitness (see set_row_height):
        // rows re-derives the height; a width that was never explicit
        // stays measured instead of freezing the pre-layout 0.
        const bool w_auto = !is_width_explicit();
        auto s = get_size();
        // with a height provider the widget is sized to the sum of the
        // provider's heights for items [0, visible) of the current model
        s.height = height_fn != nullptr
                     ? detail::clamp_int(visible_height(visible))
                     : static_cast<int>(visible) * row_height;
        set_size(s.width, s.height);
        if (w_auto)
        {
            set_width_auto(s.width);
        }
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
        // sums exist only for a height provider: the fixed-row height
        // needs no prefix sums, and materializing them for a huge virtual
        // model would allocate per-count memory the fixed mode never
        // reads (every fixed-mode path derives from count and row_height)
        if (height_fn != nullptr)
        {
            rebuild_sums();
            // the model changed under a provider: re-derive the widget
            // height (the set_visible_rows rule) from the fresh sums,
            // mirroring set_item_height; the fixed-row height does not
            // depend on the count
            auto s = get_size();
            s.height = detail::clamp_int(visible_height(visible));
            set_size(s.width, s.height);
        }
        clamp_top();
        if (value >= count && value != invalid)
        {
            value = invalid;
        }
    }

    void ListBox::set_item_height(const ItemHeight fn, const void *arg)
    {
        height_fn = fn;
        height_arg = arg;
        invalidate_row_cache();
        rebuild_sums();
        // the model owns the widget height now (the set_visible_rows
        // rule): re-derive so a provider installed after the rows
        // setter still sizes the widget from its own heights; unsetting
        // falls back to the fixed row math. The width axis keeps its
        // explicitness (L-3, see set_row_height).
        const bool w_auto = !is_width_explicit();
        auto s = get_size();
        s.height = fn != nullptr
                     ? detail::clamp_int(visible_height(visible))
                     : static_cast<int>(visible) * row_height;
        set_size(s.width, s.height);
        if (w_auto)
        {
            set_width_auto(s.width);
        }
        if (fn != nullptr)
        {
            // the pixel offset takes over from the row index: carry the
            // window over so a same-shape provider swap stays in place
            top_px = static_cast<int64_t>(top) * row_height;
        }
        else
        {
            top = static_cast<size_t>(std::min<int64_t>(
                top_px / row_height, static_cast<int64_t>(max_top())));
        }
        mark_dirty();
        clamp_top();
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

    core::imsize_t ListBox::measure() const
    {
        if (height_fn != nullptr)
        {
            return {100, detail::clamp_int(visible_height(visible))};
        }
        return {100, static_cast<int>(visible) * row_height};
    }

    size_t ListBox::get_top() const
    {
        return height_fn != nullptr ? row_at_offset() : top;
    }

    int ListBox::height_at(const size_t i) const
    {
        if (height_fn == nullptr)
        {
            return row_height;
        }
        const int h = height_fn(height_arg, i);
        return h > 0 ? h : 1;  // §3.7: a zero-height row is neither
                               // hittable nor visible
    }

    void ListBox::rebuild_sums()
    {
        sums_.assign(count + 1, 0);
        for (size_t i = 0; i < count; ++i)
        {
            sums_[i + 1] = sums_[i] + height_at(i);
        }
    }

    int64_t ListBox::visible_height(const size_t rows) const
    {
        if (sums_.empty())
        {
            return 0;
        }
        return sums_[rows < count ? rows : count];
    }

    int64_t ListBox::max_top_px() const
    {
        const int64_t total = visible_height(count);
        const int64_t view = get_size().height;
        return total > view ? total - view : 0;
    }

    size_t ListBox::row_at_offset() const
    {
        // the smallest row whose span crosses the window top:
        // sums_[r] <= top_px < sums_[r+1] (== count past the content)
        size_t lo = 0;
        size_t hi = count;
        while (lo < hi)
        {
            const size_t mid = lo + (hi - lo) / 2;
            if (sums_[mid + 1] > top_px)
            {
                hi = mid;
            }
            else
            {
                lo = mid + 1;
            }
        }
        return lo;
    }

    bool ListBox::overflows() const
    {
        // the bar shows when the content hides below the viewport: the
        // row-count rule in fixed mode, the content-vs-viewport rule
        // with a height provider (the geometry is the provider's)
        return height_fn != nullptr ? visible_height(count) > get_size().height
                                    : count > visible;
    }

    size_t ListBox::max_top() const
    {
        return count > visible ? count - visible : 0;
    }

    void ListBox::clamp_top()
    {
        if (height_fn != nullptr)
        {
            const int64_t mx = max_top_px();
            top_px = std::min<int64_t>(std::max<int64_t>(top_px, 0), mx);
            return;
        }
        if (top > max_top())
        {
            top = max_top();
        }
    }

    void ListBox::reveal(const size_t sel)
    {
        if (height_fn != nullptr)
        {
            // keep the selected row's whole span inside the window
            const int64_t r_top = sums_[sel];
            const int64_t r_bottom = sums_[sel + 1];
            if (r_top < top_px)
            {
                top_px = r_top;
            }
            else if (r_bottom > top_px + get_size().height)
            {
                top_px = r_bottom - get_size().height;
            }
            clamp_top();
            return;
        }
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
            next = height_fn != nullptr ? row_at_offset() : top;
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
        if (height_fn != nullptr)
        {
            // §3.7 wheel rule: one notch = 3 x the topmost visible
            // row's height (the fixed 3-rows rule generalized)
            const size_t r = row_at_offset();
            if (r >= count)
            {
                return false;
            }
            const int64_t step =
                static_cast<int64_t>(scroll_step) * height_at(r);
            const int64_t t = top_px - static_cast<int64_t>(dir) * step;
            int64_t next = t < 0 ? 0 : t;
            const int64_t mx = max_top_px();
            if (next > mx)
            {
                next = mx;
            }
            if (next == top_px)
            {
                return false;
            }
            invalidate_row_cache();
            mark_dirty();
            top_px = next;
            return true;
        }
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
            if (height_fn != nullptr)
            {
                const int64_t scaled =
                    static_cast<int64_t>(y - drag_grab) * max_top_px() /
                    travel;
                int64_t next = scaled < 0 ? 0 : scaled;
                const int64_t mx = max_top_px();
                if (next > mx)
                {
                    next = mx;
                }
                if (next == top_px)
                {
                    return false;
                }
                invalidate_row_cache();
                mark_dirty();
                top_px = next;
                return true;
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
            if (ev.delta == 0)
            {
                return false;  // a normalized zero delta is not a step
            }
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
        if (!overflows() || s.width <= gutter)
        {
            return;
        }
        const int track_h = s.height - 2;
        if (height_fn != nullptr)
        {
            // §3.7: thumb share and offset derive from the pixel totals
            // (the fixed rows math generalized; X-9 64-bit helper)
            const int64_t total = visible_height(count);
            const int th = detail::ratio(track_h, s.height, total);
            const int thumb = th < 8 ? 8 : th;
            *x0 = s.width - gutter;
            *y0 = 1;
            *height = track_h;
            *thumb_y =
                1 + detail::ratio(detail::clamp_int(top_px), track_h - thumb,
                                  total - s.height);
            *thumb_h = thumb;
            return;
        }
        // fixed rows: count has no sums allocation capping it anymore, so
        // the travel and thumb shares compute in 64-bit and clamp into
        // int (the provider branch's detail::ratio discipline); the share
        // stays under track_h, so clamping only bites a count no real
        // model carries
        const int64_t travel =
            static_cast<int64_t>(count) - static_cast<int64_t>(visible);
        const int th = detail::ratio(track_h, static_cast<int>(visible),
                                     static_cast<int64_t>(count));
        const int thumb = th < 8 ? 8 : th;
        *x0 = s.width - gutter;
        *y0 = 1;
        *height = track_h;
        *thumb_y = 1 + (travel > 0
                            ? detail::clamp_int(static_cast<int64_t>(top) *
                                                (track_h - thumb) / travel)
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
        if (overflows() && s.width > gutter && x >= s.width - gutter)
        {
            return invalid;
        }
        if (height_fn != nullptr)
        {
            // the row containing the absolute pixel (binary search over
            // the prefix sums; == count past the content, which the
            // caller filters like the fixed path's unclamped row)
            const int64_t p = top_px + y;
            size_t lo = 0;
            size_t hi = count;
            while (lo < hi)
            {
                const size_t mid = lo + (hi - lo) / 2;
                if (sums_[mid + 1] <= p)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            return lo;
        }
        const size_t r = top + static_cast<size_t>(y / row_height);
        return r;
    }

    const ListBox::row_cache_entry *ListBox::find_row_cache(
        const size_t row, const bool sel, const int w, const int h,
        const core::Color &fg, const core::Color &bg) const
    {
        // channel equality through the accessors (contract A-19): a raw
        // pixel-word compare would miss on 16bpp's unused X bit
        const auto same_color = [](const core::Color &a, const core::Color &b)
        {
            return a.r() == b.r() && a.g() == b.g() && a.b() == b.b() &&
                   a.a() == b.a();
        };
        for (const auto &e : row_cache_)
        {
            if (e.row == row && e.sel == sel && e.w == w && e.h == h &&
                same_color(e.fg, fg) && same_color(e.bg, bg))
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

    void ListBox::draw_row(core::Graphics &area, const size_t r, const int y0,
                           const int rh, const int text_w,
                           const Theme &th) const
    {
        const bool sel = r == value;
        const core::Color bg = sel ? th.selection : th.field_bg;
        const core::Color fg = sel ? th.text_inverted : th.text;
        if (sel)
        {
            area.fill_rect(0, y0, text_w - 1, y0 + rh - 1, bg);
        }
        if (text_fn == nullptr)
        {
            return;
        }
        // S-1: wireframe rows bypass the image cache -- a cached
        // face would blit opaque over the bones. Glyphs go
        // straight onto the screen (text skeleton, no face, no
        // cache activity), so mode flips need no invalidation.
        if (area.get_render_mode() == core::Graphics::render_mode::wireframe)
        {
            const std::string text = text_fn(text_arg, r);
            std::u32string cps;
            for (const char *p = text.c_str(); *p != '\0';)
            {
                cps.push_back(decode_utf8_next(p));
            }
            const BitmapProvider provider;
            const auto m =
                provider.measure(cps.data(), static_cast<int>(cps.size()));
            const int tx = (text_w - m.width) / 2;
            const int ty = y0 + (rh - m.height) / 2 + m.ascent;
            provider.write(area, cps.data(), static_cast<int>(cps.size()),
                           tx, ty, fg);
            return;
        }
        core::image_t row;
        if (const auto *hit = find_row_cache(r, sel, text_w, rh, fg, bg))
        {
            row = {hit->img->data(), hit->w, hit->h, 0};
        }
        else
        {
            ++rasterizations_;
            std::string text = text_fn(text_arg, r);
            // FI-12 (P1-10): a widget that declared a runtime font size
            // (set_font_size against the process family) renders its rows
            // through that provider, left-aligned like every chat surface
            // (CJK bodies). Without the declaration the 5x7 centered
            // legacy path stays byte-identical.
            const GlyphProvider *provider = nullptr;
            if (font_size() > 0)
            {
                provider = primary_provider();
            }
            auto img = make_text_image(text.c_str(), text_w, rh, fg, bg,
                                       provider, provider != nullptr);
            const auto sz = img->size();
            row = {img->data(), sz.width, sz.height, 0};
            insert_row_cache(
                row_cache_entry{r, sel, text_w, rh, fg, bg, std::move(img)});
        }
        area.draw_image(row, 0, y0);
    }

    void ListBox::draw_at(core::Graphics &area) const
    {
        const auto &th = theme();
        const auto s = get_size();
        area.fill_rect(0, 0, s.width - 1, s.height - 1, th.field_bg);

        const bool bar = overflows() && s.width > gutter;
        const int text_w = bar ? s.width - gutter : s.width;

        if (height_fn != nullptr)
        {
            // FI-06: the visible window is the rows whose spans
            // intersect the viewport (a row straddling the window top
            // renders partly clipped — the unchanged render rule)
            const int64_t view_bottom = top_px + s.height;
            for (size_t r = row_at_offset(); r < count && sums_[r] < view_bottom; ++r)
            {
                draw_row(area, r, detail::clamp_int(sums_[r] - top_px),
                         height_at(r), text_w, th);
            }
        }
        else
        {
            // rows: the visible window of the model
            const size_t r0 = std::min(top, count);
            const size_t r1 = std::min(count, top + visible);
            for (size_t r = r0; r < r1; ++r)
            {
                draw_row(area, r, static_cast<int>(r - top) * row_height,
                         row_height, text_w, th);
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