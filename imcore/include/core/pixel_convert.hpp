#pragma once

#include <cstddef>
#include <cstdint>

#include "color.hpp"

namespace zb::ui::core
{
    /*
     * Presentation seam (A-1): the panel formats the presentation edge
     * can convert the internal buffer into. The kernel renders exactly
     * one internal format per build; a new panel format adds a value
     * here plus a convert_row branch -- never a kernel COLOR_DEPTH
     * matrix entry (docs/ARCHITECTURE.md 4.4).
     */
    enum class panel_format
    {
        native,  // the build's internal layout: a byte copy
        bgr565,  // 16-bit RGB565 words, little-endian byte order
    };

    /* bytes per pixel of the panel format; 0 for an unknown value */
    size_t panel_pixel_bytes(const panel_format f);

    /*
     * Converts one row of `count` internal colors into panel bytes.
     * Returns the number of bytes written; 0 (and nothing written) when
     * the destination is shorter than count * panel_pixel_bytes(f), the
     * format is unknown, or the inputs are invalid. Silent rejection:
     * the caller owns the warning (docs/code-contract.md 3).
     */
    size_t convert_row(const panel_format f, const Color *src, const int count,
                       uint8_t *dst, const size_t cap);
}
