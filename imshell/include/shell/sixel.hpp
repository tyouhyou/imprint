// Sixel presenter (P4 demo target): encodes one frame as a sixel DCS
// stream — the presentation-edge row converter of the terminal target
// (A-1 doctrine: a converter at the edge, never a new pixel format).
// Pure and deterministic: the same buffer always yields the same bytes,
// which the battery pins structurally (test_sixel). No dirty regions in
// v1: the caller writes the whole frame at the cursor home.
#ifndef IM_SHELL_SIXEL_HPP
#define IM_SHELL_SIXEL_HPP

#include <string>

namespace zb::shell::sixel
{
    // Encodes the frame (core::Color pixels, pitch = width) as a
    // complete sixel DCS sequence (ESC P q ... ESC \). Palette registers
    // are assigned per sixel band in first-seen order (cap 250; colors
    // beyond the cap map to the nearest registered color, deterministic
    // scan). Column runs of 4+ identical cells collapse to `!len`.
    std::string encode_frame(const void *buffer, int width, int height);

    // encode_frame written to out (a write + flush convenience for the
    // shell loop).
    void write_frame(const void *buffer, int width, int height);
}

#endif
