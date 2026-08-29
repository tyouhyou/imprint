#pragma once

#include <algorithm>

namespace zb::shell
{
    /*
     * Presentation seam (A-2): the region-decision half of presenting.
     * Every desktop shell paints its own way (SetDIBitsToDevice /
     * XPutImage / CGImage / FB mmap), but they all answer the same
     * question first -- "which region of the app buffer do I blit
     * after this frame?" -- and the painted callbacks of buffered
     * presenters coalesce before the platform presents (WM_PAINT is the
     * lowest-priority message). The region rule and the coalescer live
     * here once; the shells only keep their blit calls.
     */

    /* a region in surface pixels; w <= 0 means "nothing to present" */
    struct present_rect
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    /*
     * Shared "what do I blit" decision: when the app reported the region
     * its last paint() drew, present exactly that; when it drew nothing
     * (empty dirty region), present nothing; otherwise (system-driven
     * repaint, first frame, no dirty tracking) present the whole buffer.
     */
    inline present_rect region_to_present(const bool dirty_valid, const int x, const int y,
                                          const int w, const int h,
                                          const int buf_w, const int buf_h)
    {
        if (!dirty_valid)
        {
            return present_rect{0, 0, buf_w, buf_h};
        }
        if (w <= 0 || h <= 0)
        {
            return present_rect{};
        }
        return present_rect{x, y, w, h};
    }

    /*
     * Accumulates the regions of painted callbacks until the platform
     * presents them. Buffered presenters whose present step runs on the
     * platform's own schedule (win: WM_PAINT, mac: drawRect) may see
     * several painted callbacks before one present; the union of every
     * invalidated region must survive or all but the last are lost.
     * An "empty" painted callback (a frame that drew nothing) changes
     * nothing here -- it must not drop the regions pending before it.
     */
    class dirty_coalescer
    {
    public:
        void add(const int x, const int y, const int w, const int h)
        {
            if (w <= 0 || h <= 0)
            {
                return;
            }
            if (!valid_)
            {
                l_ = x;
                t_ = y;
                r_ = x + w;
                b_ = y + h;
                valid_ = true;
                return;
            }
            l_ = (std::min)(l_, x);
            t_ = (std::min)(t_, y);
            r_ = (std::max)(r_, x + w);
            b_ = (std::max)(b_, y + h);
        }

        void clear() { valid_ = false; }

        [[nodiscard]] bool valid() const { return valid_; }

        /* w <= 0 when nothing is pending */
        [[nodiscard]] present_rect get() const
        {
            return valid_ ? present_rect{l_, t_, r_ - l_, b_ - t_} : present_rect{};
        }

    private:
        bool valid_ = false;
        int l_ = 0;
        int t_ = 0;
        int r_ = 0;
        int b_ = 0;
    };
}
