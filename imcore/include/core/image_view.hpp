#pragma once

#include "color.hpp"

namespace zb::ui::core
{
    /*
     * A non-owning view of a pixel buffer.
     * row_stride is the number of pixels in one row; 0 means width.
     */
    struct image_t
    {
        const Color *pixels = nullptr;
        int width = 0;
        int height = 0;
        int row_stride = 0;  // 0 -> width
    };
}
