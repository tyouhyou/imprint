#include "shell/presentation.hpp"

#include "core/color.hpp"

namespace zb::shell
{
    void resample_presentation(const presentation &p, const present_rect &dest,
                               const void *src, void *dst)
    {
        if (p.w <= 0 || p.h <= 0 || dest.w <= 0 || dest.h <= 0)
        {
            return;
        }
        // straight Color copies: no channel work, the words are blitted
        // exactly as the app wrote them (the presentation edge, A-1)
        const auto *s = static_cast<const zb::ui::core::Color *>(src);
        auto *d = static_cast<zb::ui::core::Color *>(dst);
        for (int dy = dest.y; dy < dest.y + dest.h; ++dy)
        {
            const int sy = (dy - p.y) * p.buf_h / p.h;
            const auto *srow = s + static_cast<size_t>(sy) * static_cast<size_t>(p.buf_w);
            auto *drow = d + static_cast<size_t>(dy) * static_cast<size_t>(p.w) + dest.x;
            for (int dx = 0; dx < dest.w; ++dx)
            {
                const int sx = (dest.x + dx - p.x) * p.buf_w / p.w;
                drow[dx] = srow[sx];
            }
        }
    }
}  // namespace zb::shell
