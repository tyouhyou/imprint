#include "text_area.hpp"

#include "text/utf8.hpp"

namespace zb::ui
{
    TextArea::TextArea()
    {
        // T-4: wrap is always on — the editor is a wrapping paragraph
        set_text_wrap(true);
    }

    core::imsize_t TextArea::measure() const
    {
        return {100, line_pitch() * 3 + 2};
    }

    void TextArea::clamp_caret()
    {
        if (caret > get_text().size())
        {
            caret = get_text().size();
        }
    }

    void TextArea::fire_changed()
    {
        changed(utf16_to_utf8(get_text()));
    }

    int TextArea::line_pitch() const
    {
        return line_height() > 0 ? line_height() : text_height();
    }

    int TextArea::inner_width() const
    {
        const int w = get_size().width - 2 * text_gap;
        return w > 0 ? w : 0;
    }

    int TextArea::caret_line(const std::vector<std::pair<int, int>> &spans) const
    {
        for (std::size_t i = 0; i < spans.size(); ++i)
        {
            if (static_cast<int>(caret) <=
                spans[i].first + spans[i].second)
            {
                return static_cast<int>(i);
            }
        }
        return static_cast<int>(spans.size()) - 1;
    }

    size_t TextArea::column_to_caret(const char16_t *data, const int len,
                                     const int col) const
    {
        size_t k = 0;
        for (int i = 1; i <= len; ++i)
        {
            if (advance_of(data, i) <= col)
            {
                k = static_cast<size_t>(i);
            }
            else
            {
                // nearest boundary wins when the column sits inside glyph
                // i: past the glyph's midpoint the caret goes after it
                const int half =
                    (advance_of(data, i - 1) + advance_of(data, i)) / 2;
                if (col >= half)
                {
                    k = static_cast<size_t>(i);
                }
                break;
            }
        }
        return k;
    }

    int TextArea::caret_pixel_column() const
    {
        const auto &spans = wrap_spans(inner_width());
        if (spans.empty())
        {
            return 0;
        }
        const int li = caret_line(spans);
        const int off = spans[static_cast<std::size_t>(li)].first;
        int len_into_line = static_cast<int>(caret) - off;
        if (len_into_line < 0)
        {
            return 0;
        }
        const int line_len = spans[static_cast<std::size_t>(li)].second;
        if (len_into_line > line_len)
        {
            len_into_line = line_len;
        }
        return advance_of(get_text().data() + off, len_into_line);
    }

    void TextArea::scroll_caret_into_view()
    {
        const auto s = get_size();
        const int pitch = line_pitch();
        const auto &spans = wrap_spans(inner_width());
        if (spans.empty() || s.height <= 0)
        {
            return;
        }
        const int top = caret_line(spans) * pitch;
        if (top < scroll_y)
        {
            scroll_y = top;
        }
        const int bottom = top + pitch;
        if (bottom > scroll_y + s.height)
        {
            scroll_y = bottom - s.height;
        }
        if (scroll_y < 0)
        {
            scroll_y = 0;
        }
        mark_dirty();
    }

    void TextArea::clamp_scroll()
    {
        const auto s = get_size();
        const int block = wrapped_block_height(inner_width());
        const int max_scroll = block > s.height ? block - s.height : 0;
        if (scroll_y > max_scroll)
        {
            scroll_y = max_scroll;
        }
        if (scroll_y < 0)
        {
            scroll_y = 0;
        }
        mark_dirty();
    }

    void TextArea::insert(const char16_t ch)
    {
        clamp_caret();
        std::u16string t = get_text();
        t.insert(caret, 1, ch);
        ++caret;
        mark_dirty();
        set_text(t);
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        fire_changed();
    }

    bool TextArea::erase_before_caret()
    {
        clamp_caret();
        if (caret == 0 || get_text().empty())
        {
            return false;
        }
        std::u16string t = get_text();
        t.erase(caret - 1, 1);
        --caret;
        mark_dirty();
        set_text(t);
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        fire_changed();
        return true;
    }

    bool TextArea::erase_at_caret()
    {
        clamp_caret();
        if (caret >= get_text().size())
        {
            return false;
        }
        std::u16string t = get_text();
        t.erase(caret, 1);
        mark_dirty();
        set_text(t);
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        fire_changed();
        return true;
    }

    bool TextArea::move_caret(const int dir)
    {
        clamp_caret();
        const size_t n = get_text().size();
        if (dir > 0 && caret < n)
        {
            ++caret;
        }
        else if (dir < 0 && caret > 0)
        {
            --caret;
        }
        else
        {
            return false;
        }
        mark_dirty();
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        return true;
    }

    bool TextArea::move_caret_vertical(const int dir)
    {
        clamp_caret();
        const auto &spans = wrap_spans(inner_width());
        const int lines = static_cast<int>(spans.size());
        if (lines == 0)
        {
            return false;
        }
        const int target = caret_line(spans) + dir;
        if (target < 0 || target >= lines)
        {
            return false;
        }
        const int off = spans[static_cast<std::size_t>(target)].first;
        const int len = spans[static_cast<std::size_t>(target)].second;
        // the pixel column goal survives the move (a shorter target line
        // clamps to its end without re-anchoring the goal)
        caret = static_cast<size_t>(off) +
                column_to_caret(get_text().data() + off, len, column_goal);
        mark_dirty();
        scroll_caret_into_view();
        return true;
    }

    void TextArea::place_caret_at(const int x, const int y)
    {
        clamp_caret();
        const int pitch = line_pitch();
        const auto &spans = wrap_spans(inner_width());
        const int lines = static_cast<int>(spans.size());
        if (lines == 0 || pitch <= 0)
        {
            return;
        }
        int line = (y + scroll_y) / pitch;
        if (line < 0)
        {
            line = 0;
        }
        if (line >= lines)
        {
            line = lines - 1;
        }
        const int off = spans[static_cast<std::size_t>(line)].first;
        const int len = spans[static_cast<std::size_t>(line)].second;
        caret = static_cast<size_t>(off) +
                column_to_caret(get_text().data() + off, len,
                                x - text_gap);
        column_goal = x - text_gap > 0 ? x - text_gap : 0;
        mark_dirty();
        scroll_caret_into_view();
    }

    bool TextArea::on_input(const zb::input::input_event &ev)
    {
        switch (ev.type)
        {
        case zb::input::input_type::mouse_left_down:
        case zb::input::input_type::touch_down:
        {
            const auto pos = get_absolute_position();
            place_caret_at(ev.x - pos.x, ev.y - pos.y);
            return true;
        }
        case zb::input::input_type::mouse_wheel:
        {
            // wheel up (delta > 0) scrolls toward the top, the caret
            // stays put (the ScrollPanel convention)
            const int step = line_pitch() > 0 ? line_pitch() : 1;
            scroll_y -= ev.delta * step;
            clamp_scroll();
            return true;
        }
        case zb::input::input_type::key_down:
        {
            // the T-2 acceptance (code-contract §2 "Text input scope"):
            // printable ASCII plus code points >= U+00A0 as BMP code
            // units; C1 and non-BMP drop
            if (ev.ch != 0)
            {
                if ((ev.ch >= 0x20 && ev.ch <= 0x7E) ||
                    (ev.ch >= 0xA0 && ev.ch <= 0xFFFF))
                {
                    insert(static_cast<char16_t>(ev.ch));
                    return true;
                }
                return false;
            }
            switch (static_cast<input::key_code>(ev.key))
            {
            case input::key_code::space:
                insert(u' ');
                return true;
            case input::key_code::backspace:
                return erase_before_caret();
            case input::key_code::del:
                return erase_at_caret();
            case input::key_code::left:
                return move_caret(-1);
            case input::key_code::right:
                return move_caret(1);
            case input::key_code::up:
                return move_caret_vertical(-1);
            case input::key_code::down:
                return move_caret_vertical(1);
            case input::key_code::enter:
                // T-4: enter is content, not submit — there is no
                // submitted event on this editor
                insert(u'\n');
                return true;
            default:
                return false;
            }
        }
        default:
            return false;
        }
    }

    void TextArea::draw_at(core::Graphics &area) const
    {
        const auto s = get_size();
        const int pitch = line_pitch();
        const int ascent = text_ascent();
        size_t c = caret;
        if (c > get_text().size())
        {
            c = get_text().size();
        }

        // frame; the focus ring marks the active editor (as TextInput)
        const core::Color caret_col = caret_color.value_or(theme().accent);
        const core::Color edge = is_focused() ? caret_col
                                              : border_color.value_or(theme().border);
        area.draw_rect(0, 0, s.width - 1, s.height - 1, edge);

        // the wrapped block hangs from the top; the scroll offset slides
        // it under the (clipped) viewport
        const auto &spans = wrap_spans(inner_width());
        const char16_t *const data = get_text().data();
        int y = ascent - scroll_y;
        for (const auto &span : spans)
        {
            if (span.second > 0)
            {
                draw_text_at(area, data + span.first, span.second,
                             text_gap, y);
            }
            y += pitch;
        }

        // the caret: a static block column under the caret index
        if (is_focused() && !spans.empty())
        {
            const int li = caret_line(spans);
            const int off = spans[static_cast<std::size_t>(li)].first;
            const int cx = text_gap +
                           advance_of(data + off, static_cast<int>(c) - off);
            const int top = li * pitch - scroll_y;
            area.fill_rect(cx, top, cx + 1, top + pitch - 1, caret_col);
        }
    }
}  // namespace zb::ui
