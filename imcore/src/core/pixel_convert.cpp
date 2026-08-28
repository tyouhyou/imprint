/*
 * Presentation seam (A-1): row converters from the internal buffer to
 * panel formats. Lives at the presentation edge only -- shells call it
 * per row when the panel layout differs from the build's internal one;
 * the rasterizer never does.
 */

#include "pixel_convert.hpp"

#include <cstring>

namespace zb::ui::core
{
    size_t panel_pixel_bytes(const panel_format f)
    {
        switch (f)
        {
        case panel_format::native:
            return sizeof(Color);
        case panel_format::bgr565:
            return 2;
        }
        return 0;
    }

    size_t convert_row(const panel_format f, const Color *src, const int count,
                       uint8_t *dst, const size_t cap)
    {
        if (src == nullptr || dst == nullptr || count <= 0)
        {
            return 0;
        }
        const size_t need = static_cast<size_t>(count) * panel_pixel_bytes(f);
        if (need == 0 || cap < need)
        {
            return 0;
        }
        switch (f)
        {
        case panel_format::native:
            std::memcpy(dst, src, need);
            return need;
        case panel_format::bgr565:
            for (int i = 0; i < count; ++i)
            {
                // red/blue are 5-bit on both sides of the seam, so >>3
                // recovers the panel channel exactly at every depth.
                // green is 6-bit in the panel word: at 32bpp the accessor
                // is the true 8-bit channel (>>2 truncation); at 16bpp
                // internal (abgr1555) it yields g5 << 3 (0..248, never
                // 255), so the 5-bit channel is recovered and replicated
                // to 6 bits -- full green stays full green (a plain >>2
                // would cap it at 62/63)
                const uint16_t r = static_cast<uint16_t>(src[i].rgb.r >> 3);
                uint16_t g;
                if constexpr (ImColor_Depth == 16)
                {
                    const uint16_t g5 = static_cast<uint16_t>(src[i].rgb.g >> 3);
                    g = static_cast<uint16_t>((g5 << 1) | (g5 >> 4));
                }
                else
                {
                    g = static_cast<uint16_t>(src[i].rgb.g >> 2);
                }
                const uint16_t b = static_cast<uint16_t>(src[i].rgb.b >> 3);
                const uint16_t px = static_cast<uint16_t>((r << 11) | (g << 5) | b);
                // little-endian 16-bit word: low byte first
                dst[i * 2] = static_cast<uint8_t>(px & 0xFF);
                dst[i * 2 + 1] = static_cast<uint8_t>(px >> 8);
            }
            return need;
        }
        return 0;
    }
}
