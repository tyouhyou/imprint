#include "text_input.hpp"

#include <algorithm>

#include "clipboard.hpp"
#include "theme.hpp"
#include "text/utf8.hpp"

namespace zb::ui
{
    core::imsize_t TextInput::measure() const
    {
        return {100, text_height() + 2};
    }

    void TextInput::clamp_caret()
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

    void TextInput::fire_changed()
    {
        changed(utf16_to_utf8(get_text()));
    }

    std::string TextInput::selected_text() const
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

    bool TextInput::erase_selection(const bool fire)
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
        if (fire)
        {
            fire_changed();
        }
        return true;
    }

    void TextInput::insert_text(const std::u16string &units)
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
        fire_changed();
    }

    std::u16string TextInput::paste_filter(const std::u16string &in) const
    {
        std::u16string out;
        for (size_t i = 0; i < in.size();)
        {
            // the T-2 acceptance; no line break (single line). A whole
            // surrogate pair rides (A-25); a lone surrogate unit drops
            if (i + 1 < in.size() &&
                is_surrogate_pair(in[i], in[i + 1]))
            {
                out.push_back(in[i]);
                out.push_back(in[i + 1]);
                i += 2;
                continue;
            }
            const int u = static_cast<int>(in[i]);
            if (((u >= 0x20 && u <= 0x7E) || u >= 0xA0) &&
                (u < 0xD800 || u > 0xDFFF))
            {
                out.push_back(in[i]);
            }
            ++i;
        }
        return out;
    }

    void TextInput::copy_selection()
    {
        if (ClipboardSink *sink = clipboard_sink())
        {
            const std::string s = selected_text();
            sink->set_text(s.data(), s.size());
        }
        // a null sink silently drops (code-contract §3.8)
    }

    void TextInput::paste_at_caret()
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

    void TextInput::insert(const char16_t ch)
    {
        erase_selection(false);  // typing replaces the selection
        clamp_caret();
        std::u16string t = get_text();
        t.insert(caret, 1, ch);
        ++caret;
        sel_anchor = caret;  // the edit collapses the selection
        mark_dirty();
        set_text(t);
        fire_changed();
    }

    bool TextInput::erase_before_caret()
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
        fire_changed();
        return true;
    }

    bool TextInput::erase_at_caret()
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
        fire_changed();
        return true;
    }

    bool TextInput::move_caret(const int dir, const bool extend_selection)
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
        return true;
    }

    void TextInput::place_caret_at(const int x)
    {
        // caret under the click: walk the run until the prefix advance
        // passes the click x (text starts right after the gap)
        clamp_caret();
        const char16_t *const data = get_text().data();
        const int len = static_cast<int>(get_text().size());
        const int xx = x - text_gap;
        if (xx <= 0)
        {
            caret = 0;
            mark_dirty();
            return;
        }
        const size_t before = caret;
        caret = 0;
        for (int i = 1; i <= len; ++i)
        {
            if (advance_of(data, i) <= xx)
            {
                caret = static_cast<size_t>(i);
            }
            else
            {
                // nearest boundary wins when the click sits inside glyph
                // i: past the glyph's midpoint the caret goes after it
                // (the old formula used the NEXT glyph's span, which is
                // always beyond the click, and could never place the
                // caret after the clicked glyph)
                const int half = (advance_of(data, i - 1) + advance_of(data, i)) / 2;
                if (xx >= half)
                {
                    caret = static_cast<size_t>(i);
                }
                else
                {
                    caret = static_cast<size_t>(i) - 1;
                }
                break;
            }
        }
        caret = snap_pair_end(get_text(), caret);
        if (caret != before)
        {
            mark_dirty();
        }
    }

    void TextInput::on_focus_change(const bool now_focused)
    {
        if (!now_focused)
        {
            // focus loss collapses the selection to the caret (FI-02)
            clamp_caret();
            sel_anchor = caret;
            selecting_ = false;
        }
    }

    bool TextInput::on_input(const zb::input::input_event &ev)
    {
        switch (ev.type)
        {
        case zb::input::input_type::mouse_left_down:
        case zb::input::input_type::touch_down:
        {
            const auto pos = get_absolute_position();
            selecting_ = true;
            place_caret_at(ev.x - pos.x);
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
            place_caret_at(ev.x - pos.x);
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
            // character input (space arrives as a key, see the key
            // mapping in the shells). T-2 acceptance (code-contract §2
            // "Text input scope"): printable ASCII plus any code point
            // >= U+00A0 inserts as a BMP code unit; a code point >
            // U+FFFF stores as its surrogate pair (A-25); lone surrogate
            // units and C1 controls drop
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
                if (ev.ch >= 0x20 && ev.ch <= 0x7E)
                {
                    insert(static_cast<char16_t>(ev.ch));
                    return true;
                }
                // a lone surrogate unit drops (A-25: no unpaired half
                // enters the buffer — the same rule paste_filter applies
                // to pasted text)
                if (ev.ch >= 0xA0 && ev.ch <= 0xFFFF &&
                    (ev.ch < 0xD800 || ev.ch > 0xDFFF))
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
            case input::key_code::enter:
                submitted(utf16_to_utf8(get_text()));
                return true;
            default:
                return false;
            }
        }
        default:
            return false;
        }
    }

    void TextInput::draw_at(core::Graphics &area) const
    {
        const auto s = get_size();
        const int line_h = text_height();
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

        // frame; the focus ring marks the active editor
        const core::Color caret_col = caret_color.value_or(theme().accent);
        const core::Color edge = is_focused() ? caret_col
                                              : border_color.value_or(theme().border);
        area.draw_rect(0, 0, s.width - 1, s.height - 1, edge);

        // text baseline: gap + vertical centering (like draw_text)
        const char16_t *const data = get_text().data();
        const int len = static_cast<int>(get_text().size());
        const int x0 = text_gap;
        const int y0 = (s.height - line_h) / 2 + ascent;
        // the selected range draws with the inverted face (theme
        // selection + text_inverted); collapsed, the plain single draw
        const int sel_lo = static_cast<int>(std::min(a, c));
        const int sel_hi = static_cast<int>(std::max(a, c));
        if (len > 0)
        {
            const int lo = std::max(sel_lo, 0);
            const int hi = std::min(sel_hi, len);
            if (lo < hi)
            {
                const int x_a = x0 + advance_of(data, lo);
                const int x_b = x0 + advance_of(data, hi);
                area.fill_rect(x_a, (s.height - line_h) / 2, x_b - 1,
                               (s.height + line_h) / 2 - 1,
                               theme().selection);
                if (lo > 0)
                {
                    draw_text_at(area, data, lo, x0, y0);
                }
                draw_text_at(area, data + lo, hi - lo, x_a, y0,
                             theme().text_inverted);
                if (hi < len)
                {
                    draw_text_at(area, data + hi, len - hi, x_b, y0);
                }
            }
            else
            {
                draw_text_at(area, data, len, x0, y0);
            }
        }

        // the caret: a static block column under the caret index
        if (is_focused())
        {
            const int cx = x0 + advance_of(data, static_cast<int>(c));
            area.fill_rect(cx, (s.height - line_h) / 2, cx + 1, (s.height + line_h) / 2 - 1, caret_col);
        }
    }
}  // namespace zb::ui