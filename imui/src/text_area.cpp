#include "text_area.hpp"

#include <algorithm>

#include "clipboard.hpp"
#include "scale.hpp"
#include "theme.hpp"
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
        if (sel_anchor > get_text().size())
        {
            sel_anchor = get_text().size();
        }
        // A-25: a programmatic set_text can leave an index mid-pair —
        // snap before any edit or selection math reads it
        caret = snap_pair_end(get_text(), caret);
        sel_anchor = snap_pair_end(get_text(), sel_anchor);
    }

    void TextArea::fire_changed()
    {
        changed(utf16_to_utf8(get_text()));
    }

    std::string TextArea::selected_text() const
    {
        size_t a = sel_anchor;
        size_t c = caret;
        const size_t n = get_text().size();
        if (a > n)
        {
            a = n;
        }
        if (c > n)
        {
            c = n;
        }
        if (a == c)
        {
            return {};
        }
        return utf16_to_utf8(
            get_text().substr(std::min(a, c), std::max(a, c) - std::min(a, c)));
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

    bool TextArea::erase_selection(const bool fire)
    {
        clamp_caret();
        const size_t lo = std::min(sel_anchor, caret);
        const size_t hi = std::max(sel_anchor, caret);
        if (lo == hi)
        {
            return false;
        }
        std::u16string t = get_text();
        t.erase(lo, hi - lo);
        caret = lo;
        sel_anchor = lo;
        mark_dirty();
        set_text(t);
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        if (fire)
        {
            fire_changed();
        }
        return true;
    }

    void TextArea::insert_text(const std::u16string &units)
    {
        erase_selection(false);
        clamp_caret();
        if (units.empty())
        {
            return;
        }
        std::u16string t = get_text();
        t.insert(caret, units);
        caret += units.size();
        sel_anchor = caret;  // the edit collapses the selection
        mark_dirty();
        set_text(t);
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        fire_changed();
    }

    std::u16string TextArea::paste_filter(const std::u16string &in) const
    {
        std::u16string out;
        for (size_t i = 0; i < in.size();)
        {
            // the T-2 acceptance plus '\n' (enter is content here). A
            // whole surrogate pair rides (A-25); a lone surrogate unit
            // drops
            if (i + 1 < in.size() &&
                is_surrogate_pair(in[i], in[i + 1]))
            {
                out.push_back(in[i]);
                out.push_back(in[i + 1]);
                i += 2;
                continue;
            }
            const int u = static_cast<int>(in[i]);
            if (((u >= 0x20 && u <= 0x7E) || u >= 0xA0 || u == u'\n') &&
                (u < 0xD800 || u > 0xDFFF))
            {
                out.push_back(in[i]);
            }
            ++i;
        }
        return out;
    }

    void TextArea::copy_selection()
    {
        if (ClipboardSink *sink = clipboard_sink())
        {
            const std::string s = selected_text();
            sink->set_text(s.data(), s.size());
        }
        // a null sink silently drops (code-contract §3.8)
    }

    void TextArea::paste_at_caret()
    {
        const ClipboardSink *sink = clipboard_sink();
        if (sink == nullptr)
        {
            return;  // no sink: paste is a no-op (code-contract §3.8)
        }
        const std::string text = sink->get_text();
        if (text.empty())
        {
            return;  // nothing to paste (a set-only sink lands here too)
        }
        insert_text(paste_filter(utf8_to_utf16(text.c_str())));
    }

    void TextArea::insert(const char16_t ch)
    {
        erase_selection(false);  // typing replaces the selection
        clamp_caret();
        std::u16string t = get_text();
        t.insert(caret, 1, ch);
        ++caret;
        sel_anchor = caret;  // the edit collapses the selection
        mark_dirty();
        set_text(t);
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        fire_changed();
    }

    bool TextArea::erase_before_caret()
    {
        if (erase_selection())
        {
            return true;
        }
        clamp_caret();
        if (caret == 0 || get_text().empty())
        {
            return false;
        }
        std::u16string t = get_text();
        // A-25: backspacing into a pair removes both units
        const size_t back = (caret >= 2 && is_surrogate_pair(t[caret - 2],
                                                             t[caret - 1]))
                              ? 2
                              : 1;
        t.erase(caret - back, back);
        caret -= back;
        sel_anchor = caret;  // the edit collapses the selection
        mark_dirty();
        set_text(t);
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        fire_changed();
        return true;
    }

    bool TextArea::erase_at_caret()
    {
        if (erase_selection())
        {
            return true;
        }
        clamp_caret();
        if (caret >= get_text().size())
        {
            return false;
        }
        std::u16string t = get_text();
        // A-25: deleting across a pair removes both units
        const size_t fwd = (caret + 1 < t.size() &&
                            is_surrogate_pair(t[caret], t[caret + 1]))
                             ? 2
                             : 1;
        t.erase(caret, fwd);
        sel_anchor = caret;  // the edit collapses the selection
        mark_dirty();
        set_text(t);
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        fire_changed();
        return true;
    }

    bool TextArea::move_caret(const int dir, const bool extend_selection)
    {
        clamp_caret();
        const size_t n = get_text().size();
        size_t next = caret;
        if (dir > 0 && caret < n)
        {
            next = snap_pair_end(get_text(), caret + 1);
        }
        else if (dir < 0 && caret > 0)
        {
            next = caret - 1;
            // A-25: never stop between a lead and its trail
            if (next > 0 && is_surrogate_lead(get_text()[next - 1]) &&
                is_surrogate_trail(get_text()[next]))
            {
                --next;
            }
        }
        else
        {
            return false;
        }
        if (!extend_selection)
        {
            // a non-shift caret move collapses the selection (FI-02)
            sel_anchor = next;
        }
        caret = next;
        mark_dirty();
        column_goal = caret_pixel_column();
        scroll_caret_into_view();
        return true;
    }

    bool TextArea::move_caret_vertical(const int dir,
                                       const bool extend_selection)
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
        const size_t next = snap_pair_end(
            get_text(),
            static_cast<size_t>(off) +
                column_to_caret(get_text().data() + off, len, column_goal));
        if (!extend_selection)
        {
            sel_anchor = next;
        }
        caret = next;
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
        caret = snap_pair_end(
            get_text(),
            static_cast<size_t>(off) +
                column_to_caret(get_text().data() + off, len, x - text_gap));
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
            selecting_ = true;
            place_caret_at(ev.x - pos.x, ev.y - pos.y);
            if ((ev.mods & zb::input::mod_shift) == 0)
            {
                // a plain press starts a collapsed selection at the
                // caret; shift+click extends from the standing anchor
                sel_anchor = caret;
            }
            return true;
        }
        case zb::input::input_type::mouse_move:
        case zb::input::input_type::touch_move:
        {
            if (!selecting_)
            {
                return false;
            }
            // the drag moves the caret only: the anchor stays (FI-02)
            const auto pos = get_absolute_position();
            place_caret_at(ev.x - pos.x, ev.y - pos.y);
            return true;
        }
        case zb::input::input_type::mouse_left_up:
        case zb::input::input_type::touch_up:
        {
            if (selecting_)
            {
                selecting_ = false;
                return true;
            }
            return false;
        }
        case zb::input::input_type::mouse_wheel:
        {
            // wheel up (delta > 0) scrolls toward the top, the caret
            // stays put (the ScrollPanel convention). H-4: a wheel event
            // that moves nothing bubbles to the outer scroller (the
            // ListBox/ScrollPanel rule). delta is C-ABI input, so the
            // step product computes in 64-bit and clamps — scroll_y
            // never wraps
            const int step = line_pitch() > 0 ? line_pitch() : 1;
            const int before = scroll_y;
            scroll_y = detail::clamp_int(static_cast<int64_t>(scroll_y) -
                                         static_cast<int64_t>(ev.delta) * step);
            clamp_scroll();
            return scroll_y != before;
        }
        case zb::input::input_type::key_down:
        {
            // FI-02 clipboard bindings (code-contract §3.3: control
            // keys ride `key` with mods, ch = 0)
            if ((ev.mods & zb::input::mod_ctrl) != 0)
            {
                switch (ev.key)
                {
                case 'a':  // Ctrl+A: select all
                    clamp_caret();
                    sel_anchor = 0;
                    caret = get_text().size();
                    mark_dirty();
                    column_goal = caret_pixel_column();
                    scroll_caret_into_view();
                    return true;
                case 'c':  // Ctrl+C: copy
                    copy_selection();
                    return true;
                case 'v':  // Ctrl+V: paste
                    paste_at_caret();
                    return true;
                default:
                    break;  // e.g. ctrl+arrow keeps the plain move
                }
            }
            if (ev.ch == 0 &&
                ev.key == static_cast<int>(input::key_code::insert))
            {
                if ((ev.mods & zb::input::mod_ctrl) != 0)
                {
                    copy_selection();  // Ctrl+Insert: copy
                    return true;
                }
                if ((ev.mods & zb::input::mod_shift) != 0)
                {
                    paste_at_caret();  // Shift+Insert: paste
                    return true;
                }
                return false;
            }
            const bool extend = (ev.mods & zb::input::mod_shift) != 0;
            // the T-2 acceptance (code-contract §2 "Text input scope"):
            // printable ASCII plus code points >= U+00A0 as BMP code
            // units; a code point > U+FFFF stores as its surrogate pair
            // (A-25); lone surrogate units and C1 controls drop
            if (ev.ch != 0)
            {
                // a chord never inserts text: alt/meta combos and any
                // ctrl chord that fell through the clipboard switch
                // above (shift stays legal — shift+ch is plain input)
                if ((ev.mods & (zb::input::mod_ctrl | zb::input::mod_alt |
                                zb::input::mod_meta)) != 0)
                {
                    return false;
                }
                // a lone surrogate unit drops (A-25: no unpaired half
                // enters the buffer — the same rule paste_filter applies
                // to pasted text)
                if ((ev.ch >= 0x20 && ev.ch <= 0x7E) ||
                    (ev.ch >= 0xA0 && ev.ch <= 0xFFFF &&
                     (ev.ch < 0xD800 || ev.ch > 0xDFFF)))
                {
                    insert(static_cast<char16_t>(ev.ch));
                    return true;
                }
                if (ev.ch >= 0x10000 && ev.ch <= 0x10FFFF)
                {
                    // A-25: a non-BMP code point stores as its surrogate
                    // pair (the terminal translator assembles 4-byte
                    // UTF-8 into one full ch)
                    std::u16string pair;
                    const char32_t v = static_cast<char32_t>(ev.ch) - 0x10000;
                    pair.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
                    pair.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
                    insert_text(pair);
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
                return move_caret(-1, extend);
            case input::key_code::right:
                return move_caret(1, extend);
            case input::key_code::up:
                return move_caret_vertical(-1, extend);
            case input::key_code::down:
                return move_caret_vertical(1, extend);
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

    void TextArea::on_focus_change(const bool now_focused)
    {
        if (!now_focused)
        {
            // focus loss collapses the selection to the caret (FI-02)
            clamp_caret();
            sel_anchor = caret;
            selecting_ = false;
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
        size_t a = sel_anchor;
        if (a > get_text().size())
        {
            a = get_text().size();
        }

        // frame; the focus ring marks the active editor (as TextInput)
        const core::Color caret_col = caret_color.value_or(theme().accent);
        const core::Color edge = is_focused() ? caret_col
                                              : border_color.value_or(theme().border);
        area.draw_rect(0, 0, s.width - 1, s.height - 1, edge);

        // the wrapped block hangs from the top; the scroll offset slides
        // it under the (clipped) viewport. The selected unit range draws
        // with the inverted face (theme selection + text_inverted);
        // collapsed, every span takes the plain single-draw path
        const auto &spans = wrap_spans(inner_width());
        const char16_t *const data = get_text().data();
        const int sel_lo = static_cast<int>(std::min(a, c));
        const int sel_hi = static_cast<int>(std::max(a, c));
        int y = ascent - scroll_y;
        for (const auto &span : spans)
        {
            if (span.second > 0)
            {
                const int s0 = span.first;
                const int s1 = span.first + span.second;
                const int lo = std::max(sel_lo, s0);
                const int hi = std::min(sel_hi, s1);
                if (lo < hi)
                {
                    const int x_a = text_gap + advance_of(data + s0, lo - s0);
                    const int x_b = text_gap + advance_of(data + s0, hi - s0);
                    const int top = y - ascent;
                    area.fill_rect(x_a, top, x_b - 1, top + pitch - 1,
                                   theme().selection);
                    if (lo > s0)
                    {
                        draw_text_at(area, data + s0, lo - s0, text_gap, y);
                    }
                    draw_text_at(area, data + lo, hi - lo, x_a, y,
                                 theme().text_inverted);
                    if (hi < s1)
                    {
                        draw_text_at(area, data + hi, s1 - hi, x_b, y);
                    }
                }
                else
                {
                    draw_text_at(area, data + s0, span.second, text_gap, y);
                }
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
