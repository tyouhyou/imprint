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
        // dest arrives window-absolute (presentation_region's output);
        // clamp it into the presented rect, then index the scratch
        // presented-rect-locally -- the caller's scratch is the p.w x p.h
        // presented rect, so a letterboxed window must neither index the
        // scratch with a window row (overflow past p.w * p.h) nor sample
        // a source row above the presented rect (negative index)
        present_rect d = dest;
        if (d.x < p.x)
        {
            d.w -= p.x - d.x;
            d.x = p.x;
        }
        if (d.y < p.y)
        {
            d.h -= p.y - d.y;
            d.y = p.y;
        }
        if (d.x + d.w > p.x + p.w)
        {
            d.w = p.x + p.w - d.x;
        }
        if (d.y + d.h > p.y + p.h)
        {
            d.h = p.y + p.h - d.y;
        }
        if (d.w <= 0 || d.h <= 0)
        {
            return;
        }
        // straight Color copies: no channel work, the words are blitted
        // exactly as the app wrote them (the presentation edge, A-1)
        const auto *s = static_cast<const zb::ui::core::Color *>(src);
        auto *out = static_cast<zb::ui::core::Color *>(dst);
        for (int dy = d.y; dy < d.y + d.h; ++dy)
        {
            const int sy = static_cast<int>(
                static_cast<long long>(dy - p.y) * p.buf_h / p.h);
            const auto *srow = s + static_cast<size_t>(sy) * static_cast<size_t>(p.buf_w);
            auto *drow = out + static_cast<size_t>(dy - p.y) * static_cast<size_t>(p.w) +
                         static_cast<size_t>(d.x - p.x);
            for (int dx = 0; dx < d.w; ++dx)
            {
                const int sx = static_cast<int>(
                    static_cast<long long>(d.x + dx - p.x) * p.buf_w / p.w);
                drow[dx] = srow[sx];
            }
        }
    }
}  // namespace zb::shell
