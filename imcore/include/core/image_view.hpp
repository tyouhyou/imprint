#pragma once

#include "color.hpp"

namespace zb::ui::core
{
    /*
     * A non-owning view of a pixel buffer.
     * row_stride is the number of pixels in one row; 0 means width.
     * A set stride must be >= width (rows cannot overlap); a view that
     * violates it or has null pixels is rejected by draw_image (nothing
     * is drawn).
     */
    struct image_t
    {
        const Color *pixels = nullptr;
        int width = 0;
        int height = 0;
        int row_stride = 0;  // 0 -> width
    };
}
