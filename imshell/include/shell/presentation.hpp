#pragma once

#include "input.hpp"

#include "presenter.hpp"

namespace zb::shell
{
    /*
     * I-2a presentation scaling (desktop shells): the app buffer stays
     * fixed-size forever -- the shell window is what resizes. The buffer
     * is presented fitted into the window client area, aspect preserved
     * and centered (black letterbox), with nearest-neighbor resampling
     * on every platform (win StretchDIBits + COLORONCOLOR, mac
     * kCGInterpolationNone, x11 the shared manual resample loop): the
     * same buffer at the same window size renders identically
     * everywhere. All math is integer, floor division -- the inverse
     * input map is the exact arithmetic inverse of the forward stretch,
     * so hit-testing stays exact at any scale. The scaling is
     * presentation-side only: the core never learns the window size and
     * no re-layout ever happens. NDS/FB shells present 1:1; wasm/python
     * hosts scale host-side (docs/code-contract.md 3, README "Window &
     * presentation").
     */

    /*
     * The presented mapping: the fixed buffer (buf_w x buf_h) and the
     * rect it occupies in the window client area (x, y, w, h).
     */
    struct presentation
    {
        int buf_w = 0;
        int buf_h = 0;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        /*
         * The inverse of the forward stretch: a window-client point
         * becomes a buffer pixel. Returns false when the point is
         * outside the presented rect -- the letterbox is not part of
         * the app, and the shell swallows pointer events landing there.
         * The formula is the same integer floor division the forward
         * stretch samples with (dest pixel d shows buffer pixel
         * d * buf_w / w), so a click always lands on the pixel shown
         * under the cursor.
         */
        bool to_buffer(const int wx, const int wy, int &out_bx, int &out_by) const
        {
            if (w <= 0 || h <= 0 || wx < x || wy < y || wx >= x + w || wy >= y + h)
            {
                return false;
            }
            out_bx = static_cast<int>(static_cast<long long>(wx - x) * buf_w / w);
            out_by = static_cast<int>(static_cast<long long>(wy - y) * buf_h / h);
            return true;
        }
    };

    /*
     * Fits (buf_w x buf_h) into (win_w x win_h), aspect preserved and
     * centered. Cross-multiplied orientation test (no division), floor
     * division for the fitted side, half of the leftover for the offset.
     * A degenerate (non-positive) size gives a zero rect.
     */
    inline presentation presentation_fit(const int win_w, const int win_h,
                                         const int buf_w, const int buf_h)
    {
        presentation p;
        p.buf_w = buf_w;
        p.buf_h = buf_h;
        if (win_w <= 0 || win_h <= 0 || buf_w <= 0 || buf_h <= 0)
        {
            return p;
        }
        // width-constrained (>= keeps the exact-fit case here): the full
        // window width is used, the height is fitted and centered
        if (static_cast<long long>(buf_w) * win_h >= static_cast<long long>(buf_h) * win_w)
        {
            p.w = win_w;
            p.h = static_cast<int>(static_cast<long long>(buf_h) * win_w / buf_w);
            p.x = 0;
            p.y = (win_h - p.h) / 2;
            return p;
        }
        p.h = win_h;
        p.w = static_cast<int>(static_cast<long long>(buf_w) * win_h / buf_h);
        p.x = (win_w - p.w) / 2;
        p.y = 0;
        return p;
    }

    /*
     * Maps a buffer-space dirty region into the dest-space rect that
     * shows it, so the region_to_present protocol composes with the
     * stretch. Edges are ceiled (a dest pixel shows buffer pixel
     * d * buf_w / w, so the region's dest pixels run from
     * ceil(x0 * w / buf_w) to ceil(x1 * w / buf_w) exclusive) and the
     * rect is clamped into the presented rect. An empty region stays
     * empty (w <= 0), like present_rect's convention.
     */
    inline present_rect presentation_region(const presentation &p, const present_rect &r)
    {
        if (p.w <= 0 || p.h <= 0 || r.w <= 0 || r.h <= 0)
        {
            return present_rect{};
        }
        const auto ceil_div = [](const long long a, const long long b) {
            return static_cast<int>((a + b - 1) / b);
        };
        present_rect d;
        d.x = p.x + ceil_div(static_cast<long long>(r.x) * p.w, p.buf_w);
        d.y = p.y + ceil_div(static_cast<long long>(r.y) * p.h, p.buf_h);
        int dx2 = p.x + ceil_div(static_cast<long long>(r.x + r.w) * p.w, p.buf_w);
        int dy2 = p.y + ceil_div(static_cast<long long>(r.y + r.h) * p.h, p.buf_h);
        if (d.x < p.x)
        {
            d.x = p.x;
        }
        if (d.y < p.y)
        {
            d.y = p.y;
        }
        if (dx2 > p.x + p.w)
        {
            dx2 = p.x + p.w;
        }
        if (dy2 > p.y + p.h)
        {
            dy2 = p.y + p.h;
        }
        d.w = dx2 - d.x;
        d.h = dy2 - d.y;
        return d;
    }

    /*
     * The shared nearest-neighbor resample loop (the x11 shell's
     * scaler): dest pixel (dx, dy) shows the source pixel it is
     * centered over, sampled with the same floor formula the inverse
     * input map uses -- dest rect pixel (dx, dy) reads source pixel
     * ((dx - p.x) * buf_w / p.w, (dy - p.y) * buf_h / p.h). Writes only
     * the dest rect (the dirty region's dest footprint); the scratch is
     * the caller's dest-sized buffer.
     */
    void resample_presentation(const presentation &p, const present_rect &dest,
                               const void *src, void *dst);

    /*
     * Pointer events carry a position the inverse map applies to;
     * keyboard and other events carry no position and always pass. The
     * shells swallow pointer events only when the position lands on the
     * letterbox -- a key press must never be dropped for where the
     * cursor happens to rest.
     */
    inline bool maps_pointer(const zb::input::input_type t)
    {
        switch (t)
        {
            case zb::input::input_type::mouse_left_down:
            case zb::input::input_type::mouse_left_up:
            case zb::input::input_type::mouse_left_click:
            case zb::input::input_type::mouse_right_down:
            case zb::input::input_type::mouse_right_up:
            case zb::input::input_type::mouse_right_click:
            case zb::input::input_type::mouse_wheel:
            case zb::input::input_type::mouse_move:
                return true;
            default:
                return false;
        }
    }
}
