// Terminal InputSource implementation (see shell/term_input.hpp).
#include "shell/term_input.hpp"

#include <cstdio>
#include <cstdlib>

namespace zb::shell::term_input
{
    namespace
    {
        constexpr std::size_t kCsiMax = 64;

        void push_key(std::vector<zb::input::input_event> &out,
                      const zb::input::key_code key)
        {
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::key_down;
            ev.key = static_cast<int>(key);
            out.push_back(ev);
        }

        // the invalid-UTF-8 report (T-2): one U+FFFD per broken or
        // overlong sequence / per isolated high byte
        void push_replacement_char(std::vector<zb::input::input_event> &out)
        {
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::key_down;
            ev.ch = 0xFFFD;
            out.push_back(ev);
        }

        // a modifier chord: the control-byte translation (TJ-C3) — the
        // §3.8 clipboard bindings ride key + mods with ch = 0, exactly
        // like the desktop shells' chords
        void push_chord(std::vector<zb::input::input_event> &out,
                        const zb::input::key_code key, const int mods)
        {
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::key_down;
            ev.key = static_cast<int>(key);
            ev.mods = mods;
            out.push_back(ev);
        }
    }

    void parser::reset()
    {
        state_ = state::ground;
        csi_.clear();
        utf8_len_ = 0;
        utf8_need_ = 0;
        utf8_cp_ = 0;
    }

    void parser::set_cell_size(const int width_px, const int height_px)
    {
        if (width_px > 0)
        {
            cell_w_ = width_px;
        }
        if (height_px > 0)
        {
            cell_h_ = height_px;
        }
    }

    void parser::cell_to_buffer(const int cell_x, const int cell_y,
                                int &px, int &py) const
    {
        px = (cell_x - 1) * cell_w_ + cell_w_ / 2;
        py = (cell_y - 1) * cell_h_ + cell_h_ / 2;
    }

    // SGR mouse: params "<b;x;y" (the '<' already stripped). b low bits:
    // 0 left / 1 middle / 2 right / 3 none; +32 motion; +64 wheel (65 =
    // down). 'M' = press or motion, 'm' = release. Returns false when
    // the event is not one this shell maps (middle button, horizontal
    // wheel) or lands outside the buffer.
    bool parser::mouse_event(const std::string &params, const bool press,
                             std::vector<zb::input::input_event> &out) const
    {
        int b = 0;
        int x = 0;
        int y = 0;
        if (std::sscanf(params.c_str(), "%d;%d;%d", &b, &x, &y) != 3)
        {
            return false;
        }

        zb::input::input_event ev{};
        ev.touch_id = 0;
        cell_to_buffer(x, y, ev.x, ev.y);
        if (ev.x < 0 || ev.x >= buffer_w_ || ev.y < 0 || ev.y >= buffer_h_)
        {
            return false;  // the letterbox rule: outside the image, not app input
        }

        if (b & 64)
        {
            if (b == 64 || b == 65)
            {
                ev.type = zb::input::input_type::mouse_wheel;
                ev.delta = (b == 64) ? 1 : -1;
                out.push_back(ev);
                return true;
            }
            return false;  // horizontal wheel: unmapped
        }
        const int button = b & 3;
        if (b & 32)
        {
            if (button == 3)
            {
                ev.type = zb::input::input_type::mouse_move;
                out.push_back(ev);
                return true;
            }
            // drag with a held button also reports as motion
            ev.type = zb::input::input_type::mouse_move;
            out.push_back(ev);
            return true;
        }
        if (button == 3)
        {
            return false;
        }
        if (button == 1)
        {
            return false;  // middle button: unmapped in the demo target
        }
        ev.button = (button == 0) ? zb::input::mouse_button_t::left
                                  : zb::input::mouse_button_t::right;
        ev.type = press ? (button == 0 ? zb::input::input_type::mouse_left_down
                                       : zb::input::input_type::mouse_right_down)
                        : (button == 0 ? zb::input::input_type::mouse_left_up
                                       : zb::input::input_type::mouse_right_up);
        out.push_back(ev);
        return true;
    }

    void parser::csi_final(const char final_byte,
                           std::vector<zb::input::input_event> &out)
    {
        if (!csi_.empty() && csi_[0] == '<')
        {
            if (final_byte == 'M')
            {
                mouse_event(csi_.substr(1), true, out);
            }
            else if (final_byte == 'm')
            {
                mouse_event(csi_.substr(1), false, out);
            }
            return;
        }
        if (final_byte == 't')
        {
            // CSI 6 ; height ; width t — the cell-size reply
            int seq = 0;
            int h_px = 0;
            int w_px = 0;
            if (std::sscanf(csi_.c_str(), "%d;%d;%d", &seq, &h_px, &w_px) == 3 &&
                seq == 6)
            {
                set_cell_size(w_px, h_px);
            }
            return;
        }
        // plain CSI arrows (home/end etc. dropped in the demo target)
        switch (final_byte)
        {
        case 'A':
            push_key(out, zb::input::key_code::up);
            break;
        case 'B':
            push_key(out, zb::input::key_code::down);
            break;
        case 'C':
            push_key(out, zb::input::key_code::right);
            break;
        case 'D':
            push_key(out, zb::input::key_code::left);
            break;
        default:
            break;
        }
    }

    void parser::ground(const unsigned char b,
                        std::vector<zb::input::input_event> &out)
    {
        if (b == '\x1b')
        {
            state_ = state::esc;
            return;
        }
        switch (b)
        {
        case '\r':
            push_key(out, zb::input::key_code::enter);
            return;
        case '\t':
            push_key(out, zb::input::key_code::tab);
            return;
        case 0x7f:
        case 0x08:
            push_key(out, zb::input::key_code::backspace);
            return;
        case 0x04:  // Ctrl-D: the terminal's "get me out" (the EOF
                    // convention) — the escape hatch keeps this one key
            push_key(out, zb::input::key_code::escape);
            return;
        case ' ':
            // §4.2 pipeline convention: space is a navigation key
            // (activation), never a character (B1) -- the same routing as
            // the desktop shells, so space activates the focused widget
            push_key(out, zb::input::key_code::space);
            return;
        default:
            break;
        }
        // the Ctrl chords (TJ-C3, code-contract §3.8): the clipboard
        // bindings are unreachable from terminal hosts without this.
        // 0x00 = Ctrl+Space; 0x01-0x1A ride their letter with mod_ctrl.
        // Unreachable by terminal protocol design: Ctrl+H (0x08 =
        // backspace), Ctrl+I/M (0x09/0x0D = tab/enter), Ctrl+[ (0x1b =
        // the escape-parser lead); Ctrl+C (0x03) is the copy chord —
        // the "get me out" affordance moved to Ctrl-D above.
        if (b == 0x00)
        {
            push_chord(out, zb::input::key_code::space,
                       zb::input::mod_ctrl);
            return;
        }
        if (b <= 0x1A && b != 0x08 && b != 0x09 && b != 0x0D)
        {
            push_chord(out, static_cast<zb::input::key_code>('a' + (b - 1)),
                       zb::input::mod_ctrl);
            return;
        }
        if (b >= 0x20 && b <= 0x7e)
        {
            // U-1 through the shared letter/digit classification (the
            // same helper the win/x11/mac translators and the wasm/python
            // hosts apply): letters carry their lowercase key code, digits
            // verbatim, punctuation ch-only. The terminal protocol itself
            // has no key-up -- key-state is unavailable on this target
            // (ARCHITECTURE §4.2).
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::key_down;
            zb::input::decorate_printable_key(ev, b);
            out.push_back(ev);
            return;
        }
        if (b < 0x80)
        {
            return;  // other control bytes are dropped
        }
        // T-2 (code-contract §2 "Text input scope"): C2..F4 lead bytes
        // start a multibyte sequence buffered across feed() calls. Every
        // other high byte is one U+FFFD — an isolated continuation
        // (0x80..0xBF), an overlong-only lead (0xC0/0xC1), or an invalid
        // lead (0xF5..0xFF) — and the parser stays in ground, so the
        // stream is accounted for byte by byte and never dropped.
        if (b >= 0xC2 && b <= 0xF4)
        {
            utf8_need_ = (b < 0xE0) ? 2 : (b < 0xF0) ? 3
                                                     : 4;
            utf8_len_ = 1;
            static constexpr unsigned char kLeadMask[5] = {0, 0, 0x1F, 0x0F, 0x07};
            utf8_cp_ = b & kLeadMask[utf8_need_];
            state_ = state::utf8;
            return;
        }
        zb::input::input_event ev{};
        ev.type = zb::input::input_type::key_down;
        ev.ch = 0xFFFD;
        out.push_back(ev);
    }

    void parser::utf8_step(const unsigned char b,
                           std::vector<zb::input::input_event> &out)
    {
        if (b < 0x80 || b > 0xBF)
        {
            // a non-continuation byte ends the pending sequence: one
            // U+FFFD for it, then the byte re-enters ground parsing —
            // the resync point (a lead byte starts fresh, ASCII emits)
            state_ = state::ground;
            push_replacement_char(out);
            ground(b, out);
            return;
        }
        utf8_cp_ = (utf8_cp_ << 6) | (b & 0x3F);
        ++utf8_len_;
        if (utf8_len_ < utf8_need_)
        {
            return;  // still incomplete: buffered across feed() calls
        }
        state_ = state::ground;
        // overlong encodings, surrogate encodings, and out-of-range
        // values are invalid UTF-8: one U+FFFD, exactly like a broken
        // sequence (the framework text decoder applies the same rules)
        static constexpr char32_t kMinCp[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (utf8_cp_ < kMinCp[utf8_need_] || utf8_cp_ > 0x10FFFF ||
            (utf8_cp_ >= 0xD800 && utf8_cp_ <= 0xDFFF))
        {
            push_replacement_char(out);
            return;
        }
        // one completed code point per key_down; key stays 0 — U-1
        // decorates the ASCII half only, non-ASCII code points ride ch
        zb::input::input_event ev{};
        ev.type = zb::input::input_type::key_down;
        ev.ch = static_cast<int>(utf8_cp_);
        out.push_back(ev);
    }

    void parser::feed(const char *data, const std::size_t n,
                      std::vector<zb::input::input_event> &out)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const unsigned char ch = static_cast<unsigned char>(data[i]);
            switch (state_)
            {
            case state::ground:
                ground(ch, out);
                break;
            case state::utf8:
                utf8_step(ch, out);
                break;
            case state::esc:
                if (ch == '[')
                {
                    state_ = state::csi;
                    csi_.clear();
                }
                else if (ch == 'O')
                {
                    state_ = state::ss3;
                }
                else if (ch >= 0x20 && ch <= 0x7e)
                {
                    // the meta convention (xterm's ESC-prefix): ESC +
                    // a printable byte is the alt chord (TJ-C3); '['
                    // and 'O' keep their parser roles above
                    zb::input::input_event ev{};
                    ev.type = zb::input::input_type::key_down;
                    ev.mods = zb::input::mod_alt;
                    ev.ch = ch;
                    zb::input::decorate_printable_key(ev, ch);
                    out.push_back(ev);
                    state_ = state::ground;
                }
                else
                {
                    state_ = state::ground;  // lone escape: dropped
                }
                break;
            case state::csi:
                if (ch >= 0x40 && ch <= 0x7e)
                {
                    // the final byte is ASCII by the range check, so the
                    // cast cannot change its value
                    csi_final(static_cast<char>(ch), out);
                    state_ = state::ground;
                }
                else if (csi_.size() >= kCsiMax)
                {
                    csi_.clear();
                    state_ = state::csi_discard;
                }
                else
                {
                    // params/intermediates: stored as raw bytes (the
                    // string form keeps the byte pattern; only ASCII is
                    // ever read back)
                    csi_ += static_cast<char>(ch);
                }
                break;
            case state::csi_discard:
                if (ch >= 0x40 && ch <= 0x7e)
                {
                    state_ = state::ground;
                }
                break;
            case state::ss3:
                switch (ch)
                {
                case 'A':
                    push_key(out, zb::input::key_code::up);
                    break;
                case 'B':
                    push_key(out, zb::input::key_code::down);
                    break;
                case 'C':
                    push_key(out, zb::input::key_code::right);
                    break;
                case 'D':
                    push_key(out, zb::input::key_code::left);
                    break;
                default:
                    break;
                }
                state_ = state::ground;
                break;
            }
        }
    }
}
