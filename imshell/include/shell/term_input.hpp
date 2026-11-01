// Terminal InputSource (P4 demo target): a pure streaming parser — raw
// terminal bytes (raw mode) in, input_events out. Same shape as
// win_input / x11_input (A-2 doctrine: the translator is pure and
// dummy-driven unit-testable, the shell keeps only its fd glue).
//
// Understands: SGR mouse mode (CSI < b ; x ; y M/m — press, release,
// motion, wheel), CSI 16 t cell-size replies (the pointer map needs the
// cell geometry; fallback 10x20 until one arrives), CSI/SS3 arrow keys,
// single-byte keys, and UTF-8 multibyte sequences (T-2: one key_down
// with ch = code point, key = 0, per sequence — the sequence buffers
// across feed() calls exactly like the escape parser; a broken or
// overlong sequence reports one U+FFFD and the parser resynchronizes on
// the byte that broke it, so the stream is never dropped). Coordinates
// are mapped cell -> buffer pixel and
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
        // sequences and UTF-8 multibyte sequences may span feed()
        // calls (streaming)
        void feed(const char *data, std::size_t n,
                  std::vector<zb::input::input_event> &out);

        // the CSI 6 ; h ; w t reply's pixel geometry (from ESC[16t);
        // the pointer map falls back to 10x20 cells until it arrives
        void set_cell_size(int width_px, int height_px);

        void reset();

    private:
        void ground(unsigned char ch,
                    std::vector<zb::input::input_event> &out);
        void utf8_step(unsigned char b,
                       std::vector<zb::input::input_event> &out);
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
            utf8,
            esc,
            csi,
            csi_discard,
            ss3
        } state_ = state::ground;
        std::string csi_;
        // T-2: the multibyte sequence being assembled (utf8_len_ <
        // utf8_need_ persists across feed() calls; ground holds no
        // partial sequence)
        char32_t utf8_cp_ = 0;
        int utf8_need_ = 0;
        int utf8_len_ = 0;
    };
}

#endif
