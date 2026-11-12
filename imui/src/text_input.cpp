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
        // a programmatic set_text can leave an index mid-sequence —
        // snap to the code-point boundary before any edit or selection
        // math reads it (code-contract §2.1)
        caret = snap_boundary(get_text(), caret);
        sel_anchor = snap_boundary(get_text(), sel_anchor);
    }

    void TextInput::fire_changed()
    {
        changed(get_text());  // the stored bytes are the UTF-8 payload
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
        return get_text().substr(std::min(a, c), std::max(a, c) - std::min(a, c));
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
        std::string t = get_text();
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

    void TextInput::insert_text(const std::string &units)
    {
        if (read_only_)
        {
            return;  // read-only: user edits drop here (code-contract §2)
        }
        erase_selection(false);
        clamp_caret();
        if (units.empty())
        {
            return;
        }
        std::string t = get_text();
        t.insert(caret, units);
        caret += units.size();
        sel_anchor = caret;  // the edit collapses the selection
        mark_dirty();
        set_text(t);
        fire_changed();
    }

    std::string TextInput::paste_filter(const std::string &in) const
    {
        std::string out;
        for (const char *p = in.c_str(), *const end = p + in.size(); p < end;)
        {
            // the T-2 acceptance; no line break (single line). Code
            // points walk whole sequences; an invalid byte decodes to
            // U+FFFD and rides the >= U+00A0 branch like any code point
            const char32_t cp = decode_utf8_next(p, end);
            if ((cp >= 0x20 && cp <= 0x7E) || cp >= 0xA0)
            {
                append_utf8(out, cp);
            }
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
        insert_text(paste_filter(text));
    }

    void TextInput::insert(const char32_t cp)
    {
        if (read_only_)
        {
            return;  // read-only: user edits drop here (code-contract §2)
        }
        erase_selection(false);  // typing replaces the selection
        clamp_caret();
        std::string seq;
        append_utf8(seq, cp);
        std::string t = get_text();
        t.insert(caret, seq);
        caret += seq.size();
        sel_anchor = caret;  // the edit collapses the selection
        mark_dirty();
        set_text(t);
        fire_changed();
    }

    bool TextInput::erase_before_caret()
    {
        if (read_only_)
        {
            return false;  // read-only: user edits drop here (code-contract §2)
        }
        if (erase_selection())
        {
            return true;
        }
        clamp_caret();
        if (caret == 0 || get_text().empty())
        {
            return false;
        }
        std::string t = get_text();
        // one code point back: the byte span to erase (multi-byte CJK
        // deletes whole — code-contract §2.1)
        const char *p = t.data() + caret;
        (void)decode_utf8_prev(p, t.data());
        const size_t back = static_cast<size_t>(t.data() + caret - p);
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
        if (read_only_)
        {
            return false;  // read-only: user edits drop here (code-contract §2)
        }
        if (erase_selection())
        {
            return true;
        }
        clamp_caret();
        if (caret >= get_text().size())
        {
            return false;
        }
        std::string t = get_text();
        // one code point forward: the byte span to erase
        const char *p = t.data() + caret;
        (void)decode_utf8_next(p, t.data() + t.size());
        const size_t fwd = static_cast<size_t>(p - (t.data() + caret));
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
        const std::string &t = get_text();
        const size_t n = t.size();
        size_t next = caret;
        if (dir > 0 && caret < n)
        {
            const char *p = t.data() + caret;
            (void)decode_utf8_next(p, t.data() + n);  // one code point forward
            next = static_cast<size_t>(p - t.data());
        }
        else if (dir < 0 && caret > 0)
        {
            const char *p = t.data() + caret;
            (void)decode_utf8_prev(p, t.data());  // one code point back
            next = static_cast<size_t>(p - t.data());
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
        // caret under the click: walk the code points until the running
        // advance passes the click x (text starts right after the gap);
        // the running pen matches advance_of exactly (per covered code
        // point, letter-spacing included, uncovered zero)
        clamp_caret();
        const std::string &t = get_text();
        const char *const data = t.data();
        const char *const dend = data + t.size();
        const int xx = x - text_gap;
        if (xx <= 0)
        {
            caret = 0;
            mark_dirty();
            return;
        }
        const size_t before = caret;
        const int letter = letter_spacing();
        int pen = 0;
        const char *p = data;
        const char *bound = data;  // the caret lands on a walked boundary
        while (p < dend)
        {
            const char32_t cp = decode_utf8_next(p, dend);
            // the pick_provider rule: covering style run first, then the
            // primary/fallback chain (data is text_.data() here)
            const GlyphProvider *const prov =
                pick_provider(data, static_cast<int>(bound - data));
            const int w = prov != nullptr ? prov->measure(&cp, 1).width + letter : 0;
            if (pen + w <= xx)
            {
                pen += w;
                bound = p;
                continue;
            }
            // nearest boundary wins when the click sits inside glyph:
            // past the glyph's midpoint the caret goes after it
            const int half = pen + w / 2;
            if (xx >= half)
            {
                bound = p;
            }
            break;
        }
        caret = static_cast<size_t>(bound - data);
        if (caret != before)
        {
            mark_dirty();
        }
    }

    void TextInput::on_focus_change(const bool now_focused)
    {
        // the undo history lives for one focus session (code-contract
        // §2): cleared on gain (a pre-focus set_text is never undoable
        // across) and on loss
        history_.clear();
        if (!now_focused)
        {
            // focus loss collapses the selection to the caret (FI-02)
            clamp_caret();
            sel_anchor = caret;
            selecting_ = false;
        }
    }

    editor_history::step TextInput::capture() const
    {
        return {get_text(), caret, sel_anchor};
    }

    void TextInput::record_edit(const editor_history::kind k,
                                const history_step &before)
    {
        history_.record(k, before, capture());
    }

    void TextInput::apply_state(const history_step &s)
    {
        set_text(s.text);
        caret = s.caret;
        sel_anchor = s.sel_anchor;
        clamp_caret();
        fire_changed();
    }

    void TextInput::undo()
    {
        if (read_only_)
        {
            return;  // read-only gates undo too (code-contract §2)
        }
        history_step s;
        if (history_.undo(capture(), s))
        {
            apply_state(s);
        }
        // an empty stack is a silent no-op (code-contract §2)
    }

    void TextInput::redo()
    {
        if (read_only_)
        {
            return;  // read-only gates redo too (code-contract §2)
        }
        history_step s;
        if (history_.redo(capture(), s))
        {
            apply_state(s);
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
                {
                    const history_step before = capture();
                    paste_at_caret();
                    record_edit(editor_history::kind::paste, before);
                    return true;
                }
                case 'z':  // Ctrl+Z: undo (Ctrl+Shift+Z: redo)
                    if ((ev.mods & zb::input::mod_shift) != 0)
                    {
                        redo();
                    }
                    else
                    {
                        undo();
                    }
                    return true;
                case 'y':  // Ctrl+Y: redo
                    redo();
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
                    const history_step before = capture();
                    paste_at_caret();  // Shift+Insert: paste
                    record_edit(editor_history::kind::paste, before);
                    return true;
                }
                return false;
            }
            const bool extend = (ev.mods & zb::input::mod_shift) != 0;
            // character input (space arrives as a key, see the key
            // mapping in the shells). T-2 acceptance (code-contract §2
            // "Text input scope"): printable ASCII plus any code point
            // >= U+00A0 — one code point, one editing atom. The terminal
            // translator assembles multi-byte UTF-8 into one full ch, so
            // a non-BMP code point needs no special path (it stores as
            // its UTF-8 byte sequence)
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
                if ((ev.ch >= 0x20 && ev.ch <= 0x7E) ||
                    (ev.ch >= 0xA0 && ev.ch <= 0x10FFFF &&
                     (ev.ch < 0xD800 || ev.ch > 0xDFFF)))
                {
                    const history_step before = capture();
                    insert(static_cast<char32_t>(ev.ch));
                    record_edit(editor_history::kind::insert, before);
                    return true;
                }
                return false;
            }
            switch (static_cast<input::key_code>(ev.key))
            {
            case input::key_code::space:
            {
                const history_step before = capture();
                insert(U' ');
                record_edit(editor_history::kind::insert, before);
                return true;
            }
            case input::key_code::backspace:
            {
                const history_step before = capture();
                const bool consumed = erase_before_caret();
                record_edit(editor_history::kind::backspace, before);
                return consumed;
            }
            case input::key_code::del:
            {
                const history_step before = capture();
                const bool consumed = erase_at_caret();
                record_edit(editor_history::kind::del, before);
                return consumed;
            }
            case input::key_code::left:
                return move_caret(-1, extend);
            case input::key_code::right:
                return move_caret(1, extend);
            case input::key_code::enter:
                submitted(get_text());
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
        const char *const data = get_text().data();
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