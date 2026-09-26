// Terminal InputSource implementation (see shell/term_input.hpp).
#include "shell/term_input.hpp"

#include <cstdio>
#include <cstdlib>

namespace zb::shell::term_input
{
    namespace
    {
        void push_key(std::vector<zb::input::input_event> &out,
                      const zb::input::key_code key)
        {
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::key_down;
            ev.key = static_cast<int>(key);
            out.push_back(ev);
        }
    }

    void parser::reset()
    {
        state_ = state::ground;
        csi_.clear();
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
            case 'A': push_key(out, zb::input::key_code::up); break;
            case 'B': push_key(out, zb::input::key_code::down); break;
            case 'C': push_key(out, zb::input::key_code::right); break;
            case 'D': push_key(out, zb::input::key_code::left); break;
            default: break;
        }
    }

    void parser::ground(const char ch,
                        std::vector<zb::input::input_event> &out)
    {
        if (ch == '\x1b')
        {
            state_ = state::esc;
            return;
        }
        switch (ch)
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
            case 0x03:  // Ctrl-C: the terminal's "get me out" maps to escape
            case 0x04:
                push_key(out, zb::input::key_code::escape);
                return;
            default:
                break;
        }
        if (ch >= 0x20 && ch <= 0x7e)
        {
            zb::input::input_event ev{};
            ev.type = zb::input::input_type::key_down;
            ev.ch = static_cast<int>(ch);
            out.push_back(ev);
        }
        // other control bytes are dropped
    }

    void parser::feed(const char *data, const std::size_t n,
                      std::vector<zb::input::input_event> &out)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const char ch = data[i];
            switch (state_)
            {
                case state::ground:
                    ground(ch, out);
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
                    else
                    {
                        state_ = state::ground;  // lone escape: dropped
                    }
                    break;
                case state::csi:
                    if (ch >= 0x40 && ch <= 0x7e)
                    {
                        csi_final(ch, out);
                        state_ = state::ground;
                    }
                    else
                    {
                        csi_ += ch;  // params/intermediates
                    }
                    break;
                case state::ss3:
                    switch (ch)
                    {
                        case 'A': push_key(out, zb::input::key_code::up); break;
                        case 'B': push_key(out, zb::input::key_code::down); break;
                        case 'C': push_key(out, zb::input::key_code::right); break;
                        case 'D': push_key(out, zb::input::key_code::left); break;
                        default: break;
                    }
                    state_ = state::ground;
                    break;
            }
        }
    }
}
