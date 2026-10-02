#pragma once

#include "presentation.hpp"

namespace zb::shell
{
    /*
     * I-2b device overlay: bezel/chrome around the presented buffer,
     * shaped by target screen constraints (dual NDS 256x192 screens with
     * a hinge gap, 320x240 framebuffers, ...). Pure integer layout math
     * beside the I-2a seam: the desktop shells paint the computed bars
     * with their native fill instead of blanket black letterbox
     * (adopted; see code-contract §3), and pointer events landing on
     * chrome are swallowed (letterbox rule; the hinge composition under
     * hinge_bar applies when a device profile covers buffer rows).
     *
     * Single-buffer rule: the overlay frames ONE app buffer. A hinge bar
     * covers buffer rows of the presented buffer itself (a stacked
     * 256x384 NDS-style buffer shows rows [192, 192+gap) behind the
     * hinge); two live screens need two buffers, which is out of scope.
     */

    // NDS screen constraints (device truth, buffer pixels)
    constexpr int nds_screen_w = 256;
    constexpr int nds_screen_h = 192;

    // chrome bars around a presented rect within a window (client px).
    // top/bottom span the full width; left/right fill the middle band.
    struct chrome_bars
    {
        present_rect top;
        present_rect bottom;
        present_rect left;
        present_rect right;
    };

    /*
     * Bars around p inside (win_w x win_h). p is expected inside the
     * window (presentation_fit guarantees it). A degenerate (empty) p
     * means nothing is presented: the whole window is chrome, reported
     * as one top bar. All bars are clamped non-negative.
     */
    inline chrome_bars chrome_around(const presentation &p, const int win_w,
                                     const int win_h)
    {
        chrome_bars b;
        if (p.w <= 0 || p.h <= 0)
        {
            b.top = {0, 0, win_w > 0 ? win_w : 0, win_h > 0 ? win_h : 0};
            return b;
        }
        if (p.y > 0)
        {
            b.top = {0, 0, win_w, p.y};
        }
        const int below = win_h - (p.y + p.h);
        if (below > 0)
        {
            b.bottom = {0, p.y + p.h, win_w, below};
        }
        if (p.x > 0)
        {
            b.left = {0, p.y, p.x, p.h};
        }
        const int beside = win_w - (p.x + p.w);
        if (beside > 0)
        {
            b.right = {p.x + p.w, p.y, beside, p.h};
        }
        return b;
    }

    /*
     * Hinge bar: buffer rows [row0, row0+rows) shown as chrome (the NDS
     * hinge covers part of the presented buffer). Reuses the
     * presentation_region ceil mapping, so the bar covers exactly the
     * rows the stretch would show there; empty when rows <= 0.
     * Input composition (shell-side): a pointer event is app input iff
     * p.to_buffer(...) succeeds AND !contains_rect(hinge, wx, wy).
     */
    inline present_rect hinge_bar(const presentation &p, const int row0,
                                  const int rows)
    {
        if (rows <= 0)
        {
            return present_rect{};
        }
        const present_rect r{0, row0, p.buf_w, rows};
        return presentation_region(p, r);
    }

    /*
     * Half-open point-in-rect (matches to_buffer's dest convention);
     * empty rects contain nothing.
     */
    inline bool contains_rect(const present_rect &r, const int x, const int y)
    {
        return r.w > 0 && r.h > 0 && x >= r.x && y >= r.y && x < r.x + r.w &&
               y < r.y + r.h;
    }
}  // namespace zb::shell
