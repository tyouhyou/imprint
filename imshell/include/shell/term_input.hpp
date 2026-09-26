// Terminal InputSource (P4 demo target): a pure streaming parser — raw
// terminal bytes (raw mode) in, input_events out. Same shape as
// win_input / x11_input (A-2 doctrine: the translator is pure and
// dummy-driven unit-testable, the shell keeps only its fd glue).
//
// Understands: SGR mouse mode (CSI < b ; x ; y M/m — press, release,
// motion, wheel), CSI 16 t cell-size replies (the pointer map needs the
// cell geometry; fallback 10x20 until one arrives), CSI/SS3 arrow keys,
// and single-byte keys. Coordinates are mapped cell -> buffer pixel and
// events landing outside the buffer are dropped (the letterbox rule of
// the other shells).
#ifndef IM_SHELL_TERM_INPUT_HPP
#define IM_SHELL_TERM_INPUT_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "input.hpp"

namespace zb::shell::term_input
{
    class parser
    {
    public:
        parser(int buffer_width, int buffer_height)
            : buffer_w_(buffer_width), buffer_h_(buffer_height) {}

        // feeds raw bytes and appends translated events; escape
        // sequences may span feed() calls (streaming)
        void feed(const char *data, std::size_t n,
                  std::vector<zb::input::input_event> &out);

        // the CSI 6 ; h ; w t reply's pixel geometry (from ESC[16t);
        // the pointer map falls back to 10x20 cells until it arrives
        void set_cell_size(int width_px, int height_px);

        void reset();

    private:
        void ground(char ch, std::vector<zb::input::input_event> &out);
        void csi_final(char final_byte,
                       std::vector<zb::input::input_event> &out);
        bool mouse_event(const std::string &params, bool press,
                         std::vector<zb::input::input_event> &out) const;
        void cell_to_buffer(int cell_x, int cell_y, int &px, int &py) const;

        int buffer_w_;
        int buffer_h_;
        int cell_w_ = 10;
        int cell_h_ = 20;
        enum class state
        {
            ground,
            esc,
            csi,
            ss3
        } state_ = state::ground;
        std::string csi_;
    };
}

#endif
