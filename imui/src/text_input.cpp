#include "text_input.hpp"

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
    }

    void TextInput::fire_changed()
    {
        changed(utf16_to_utf8(get_text()));
    }

    void TextInput::insert(const char16_t ch)
    {
        clamp_caret();
        std::u16string t = get_text();
        t.insert(caret, 1, ch);
        ++caret;
        mark_dirty();
        set_text(t);
        fire_changed();
    }

    bool TextInput::erase_before_caret()
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
        fire_changed();
        return true;
    }

    bool TextInput::erase_at_caret()
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
        fire_changed();
        return true;
    }

    bool TextInput::move_caret(const int dir)
    {
        clamp_caret();
        const size_t n = get_text().size();
        if (dir > 0 && caret < n)
        {
            mark_dirty();
            ++caret;
            return true;
        }
        if (dir < 0 && caret > 0)
        {
            mark_dirty();
            --caret;
            return true;
        }
        return false;
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
        if (caret != before)
        {
            mark_dirty();
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
            place_caret_at(ev.x - pos.x);
            return true;
        }
        case zb::input::input_type::key_down:
        {
            // printable character (space arrives as a key, see the key
            // mapping in the shells)
            if (ev.ch != 0)
            {
                if (ev.ch >= 0x20 && ev.ch <= 0x7e)
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

        // frame; the focus ring marks the active editor
        const core::Color caret = caret_color.value_or(theme().accent);
        const core::Color edge = is_focused() ? caret
                                              : border_color.value_or(theme().border);
        area.draw_rect(0, 0, s.width - 1, s.height - 1, edge);

        // text baseline: gap + vertical centering (like draw_text)
        const char16_t *const data = get_text().data();
        const int len = static_cast<int>(get_text().size());
        const int x0 = text_gap;
        const int y0 = (s.height - line_h) / 2 + ascent;
        if (len > 0)
        {
            draw_text_at(area, data, len, x0, y0);
        }

        // the caret: a static block column under the caret index
        if (is_focused())
        {
            const int cx = x0 + advance_of(data, static_cast<int>(c));
            area.fill_rect(cx, (s.height - line_h) / 2, cx + 1, (s.height + line_h) / 2 - 1, caret);
        }
    }
}  // namespace zb::ui