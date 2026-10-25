#include "imraster/canvas.hpp"

#include <cstring>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace zb::raster
{
    /*
     * Channel-wise integer lerp on the upscale, 256ths. The upscale is the
     * one pass that touches every surface pixel, so it stays integer
     * arithmetic: a float multiply and a float convert per tap is measurable
     * at 2M pixels.
     *
     * The alpha byte is left alone: the world grid carries 0xffffffff and
     * the page putImageData's the buffer verbatim, so alpha has to survive
     * the resample untouched. It is carried by a MASK rather than unpacked
     * and repacked, and the three colour channels are done one at a time so
     * no arithmetic can touch another channel's bits.
     *
     * That last point is not style. The short form
     *
     *     (a + (((b - a) * w) >> 8)) & 0xffffff
     *
     * is four ops instead of twenty and it is WRONG: the multiply carries
     * bits out of each byte into the next, so every channel picks up its
     * neighbour's high bits. It agrees with this function on most inputs and
     * disagrees on 523744 of 2.1 million -- see
     * game/test/upscale_check.cpp, which sweeps the whole domain rather than
     * sampling it, because a sampled check walks straight past this.
     *
     * Measured on the pass as a whole: this is worth roughly 15% of the
     * blend work, which is itself only about a quarter of upscale() -- that
     * pass is memory-bound, and a version doing no arithmetic at all still
     * costs 7.1 of its 11.8 ms in wasm. See DESIGN F28.
     */
    uint32_t tent_blend(const uint32_t a, const uint32_t b, const int w)
    {
        if (w <= 0)
        {
            return a;
        }
        if (w >= 256)
        {
            return b;
        }
        const int ar = unpack_r(a), ag = unpack_g(a), ab = unpack_b(a);
        const int br = unpack_r(b), bg = unpack_g(b), bb = unpack_b(b);
        return (a & 0xff000000u) |
               static_cast<uint32_t>(ar + (((br - ar) * w) >> 8)) |
               (static_cast<uint32_t>(ag + (((bg - ag) * w) >> 8)) << 8) |
               (static_cast<uint32_t>(ab + (((bb - ab) * w) >> 8)) << 16);
    }

    /*
     * The blend, on operands that are already unpacked.
     *
     * This is tent_blend() with the six channel extractions lifted out of it,
     * and it exists because within one group of `rep` output rows the
     * rep - 2 blended rows all blend the SAME two source rows. Paying the
     * unpack once per group instead of once per output pixel is the whole of
     * the change: at rep 4 the vertical pass does 1.04 M blends and the
     * horizontal 0.26 M, and at 35 ops per blend the twelve ops that unpack
     * a and b are a third of all of it.
     *
     * `top` is a's alpha byte already masked out. It is passed in rather than
     * `a` itself because it is per-source-row, not per-output-pixel, and
     * recomputing it inside was three ops of the seven the repack costs.
     *
     * Byte-identical to tent_blend() for 0 < w < 256, which is the only range
     * the callers use: weight[j] = 256*j/rep with 1 <= j <= rep-2, so for
     * every rep from 2 to 8 it lands in (0, 256). That invariant is also why
     * tent_blend's two range branches can be hoisted out of these loops
     * entirely -- they are never taken. The blend COUNT is unchanged, so the
     * group-end copies are still the same two memcpys and pass B still reads
     * operands that were in L1 (F40).
     */
    [[gnu::always_inline]] inline uint32_t tent_px(const uint32_t top,
                                                  const int ar, const int ag,
                                                  const int ab, const int br,
                                                  const int bg, const int bb,
                                                  const int w)
    {
        return top | static_cast<uint32_t>(ar + (((br - ar) * w) >> 8)) |
               (static_cast<uint32_t>(ag + (((bg - ag) * w) >> 8)) << 8) |
               (static_cast<uint32_t>(ab + (((bb - ab) * w) >> 8)) << 16);
    }

    namespace
    {
        // the upscale's per-output weights live in a fixed array: the
        // replication factor is a settings value, not a compile-time
        // constant, but it is small and bounded
        constexpr int kMaxRep = 8;

        // Graphics normalizes rect arguments before filling; so do we, so a
        // caller that passes (x1 > x0) still covers the same block
        void normalize(int &a, int &b)
        {
            if (a > b)
            {
                std::swap(a, b);
            }
        }
    } // namespace

    void Canvas::reset_mesh_depth() noexcept
    {
        if (!mesh_depth_.empty())
        {
            std::fill(mesh_depth_.begin(), mesh_depth_.end(), 0.f);
        }
    }

    void Canvas::set_replicate(const int n) 
    {
        rep_ = n > 1 ? n : 1;
        grid_w_ = w_ / rep_;
        grid_h_ = h_ / rep_;
        // Only reallocate when the geometry actually moved: set_replicate(1)
        // brackets the 1:1 passes inside every frame, and zeroing a grid
        // that is about to be overwritten anyway is pure loss.
        const size_t need = static_cast<size_t>(grid_w_) *
                            static_cast<size_t>(grid_h_);
        if (grid_.size() != need)
        {
            grid_.assign(need, 0u);
        }
        // The sky mask tracks the ACTIVE surface, so at 1:1 it is surface
        // sized. Reallocating it here rather than only in resize() is what
        // keeps sky_w()/sky_h() honest when the render scale changes without
        // the window doing so -- the setting is live in the menu.
        const size_t sky_need = rep_ > 1 ? need
                                         : static_cast<size_t>(w_) *
                                               static_cast<size_t>(h_);
        if (sky_.size() != sky_need)
        {
            sky_.assign(sky_need, uint8_t{0});
        }
        if (mesh_depth_.size() != sky_need)
        {
            mesh_depth_.assign(sky_need, 0.f);
        }
        const size_t span_need = static_cast<size_t>(active_w());
        if (span_top_.size() != span_need)
        {
            span_top_.assign(span_need, int16_t{sky_sentinel_});
            span_bot_.assign(span_need, int16_t{-1});
        }
    }

    namespace
    {
        // saturating add, because a star that clipped to a flat white plate
        // would be a worse artefact than no star at all
        int sat_add(const int a, const int b) noexcept
        {
            const int v = a + b;
            return v < 0 ? 0 : (v > 255 ? 255 : v);
        }
    } // namespace

    void Canvas::add_pixel(const int x, const int y, const int r, const int g,
                           const int b, const int gain) noexcept
    {
        if (x < 0 || y < 0 || x >= w_ || y >= h_ || gain <= 0)
        {
            return;
        }
        uint32_t &p = px_[static_cast<size_t>(y) * static_cast<size_t>(w_) +
                          static_cast<size_t>(x)];
        const int k = gain > 256 ? 256 : gain;
        p = pack(sat_add(unpack_r(p), (r * k) >> 8),
                 sat_add(unpack_g(p), (g * k) >> 8),
                 sat_add(unpack_b(p), (b * k) >> 8));
    }

    void Canvas::mark_sky(const int y0, const int y1) noexcept
    {
        const int h = active_h();
        const int y_start = y0 < 0 ? 0 : (y0 < h ? y0 : h);
        const int y_end = y1 >= h ? h - 1 : (y1 < y_start ? y_start - 1 : y1);
        const size_t stride = static_cast<size_t>(active_w());
        for (int y = y_start; y <= y_end; ++y)
        {
            std::memset(sky_.data() + static_cast<size_t>(y) * stride, 1,
                        stride);
        }
    }


    void Canvas::resize(const int w, const int h)
    {
        w_ = w > 0 ? w : 0;
        h_ = h > 0 ? h : 0;
        grid_w_ = w_ / rep_;
        grid_h_ = h_ / rep_;
        px_.assign(static_cast<size_t>(w_) * static_cast<size_t>(h_), 0u);
        grid_.assign(static_cast<size_t>(grid_w_) *
                         static_cast<size_t>(grid_h_),
                     0u);
        const size_t sky_need = rep_ > 1 ? static_cast<size_t>(grid_w_) *
                                               static_cast<size_t>(grid_h_)
                                         : static_cast<size_t>(w_) *
                                               static_cast<size_t>(h_);
        sky_.assign(sky_need, uint8_t{0});
        mesh_depth_.assign(sky_need, 0.f);
        // The per-column spans too, and this is not a copy-paste of the line
        // above: allocating them ONLY in set_replicate left them empty for
        // any frame that rendered before the render scale had ever been set
        // (rep_ defaults to 1, so active_w() is w_ = 1920 there), and
        // reset_wall_spans() then wrote 1920 int16s into an empty vector.
        // The wasm build's heap sanitizer caught it as corruption at address
        // zero, which is a much better outcome than the visual symptom it
        // would have had.
        span_top_.assign(static_cast<size_t>(active_w()), int16_t{sky_sentinel_});
        span_bot_.assign(static_cast<size_t>(active_w()), int16_t{-1});
        // sized for the full width even though a pass only touches the
        // coarse grid: 7 KB, and it keeps the depth accessors branch-free
        // whatever scale is in force
        depth_.assign(static_cast<size_t>(w_), 0.f);
    }

    void Canvas::upscale()
    {
        if (rep_ <= 1 || grid_w_ < 1 || grid_h_ < 1)
        {
            return; // no grid: the world drew on the surface already
        }
        const int rep = rep_;
        const int gw = grid_w_;
        const int gh = grid_h_;
        const size_t tw = static_cast<size_t>(w_);

        /*
         * A tent (bilinear) resample, and the reason it is not written as two
         * passes over memory.
         *
         * A separable tent is two 1D filters, so the obvious shape is:
         *
         *   A  grid (gw x gh)  -> tmp (w x gh)    horizontal
         *   B  tmp  (w x gh)  -> surface (w x h)  vertical
         *
         * and that is what this used to be. It is also, at 1080p with rep 4,
         * a 2.07 MB scratch buffer (1920 x 270 x 4) written by pass A and
         * streamed straight back through by pass B -- 4.15 MB of DRAM traffic
         * per frame to hand 15 KB of working data from one pass to the other.
         *
         * The ordering argument that put the expensive half on the short
         * buffer ("A's cost is divided by rep relative to B's") was reasoning
         * about blend counts, and it was right about blend counts. It said
         * nothing about the 2 MB, because pass A's WRITES are not divided by
         * rep: every horizontal result is written once at full width and read
         * back once, no matter how many output pixels it becomes. And this is
         * the frame's most expensive pass (measured 6.1 ms) and F28 already
         * established it is memory-bound, not arithmetic-bound -- so the
         * traffic was the whole cost and the blend count was a rounding
         * error next to it.
         *
         * So the two passes are FUSED, and the intermediate stops existing:
         *
         *   for each grid row gy:
         *       hrow(gy) and hrow(gy+1) into two row buffers
         *       emit the rep output rows for that group straight into px_
         *
         * The row buffers are 2 x w x 4 = 15 KB, so they are read and written
         * entirely inside L1 and never reach DRAM. Traffic per frame drops
         * from 0.52 + 2.07 + 2.07 + 8.29 = 12.96 MB to 0.52 + 8.29 = 8.81 MB,
         * and pass B's inner loop -- 1.95 M blends, the bulk of the pass --
         * now reads operands that were in L1 rather than in a 2 MB stream.
         *
         * Two row buffers suffice because the group ends at a row that is already
         * in hand, so hrow(gy+1) is available when group gy+1 starts: the
         * buffers swap roles and only the new far row is computed. That is
         * gh horizontal rows in total, exactly as many as pass A used to do
         * -- the fusion costs no extra filtering work at all.
         *
         * Within one group of `rep` outputs there are `rep - 1` blended
         * outputs and ONE exact copy: the group's first output is the near
         * texel verbatim. The group's LAST output used to be the far texel
         * verbatim too, which made it identical to the next group's first --
         * 270 duplicated rows and 480 duplicated columns at 1080p/rep 4, and
         * a reconstruction that ramped 0, 1/4, 1/2, 1 and then started over.
         * That was F29, priced but unfixed; it is fixed here, so the filter
         * ramps 0, 1/4, 1/2, 3/4, 1 and every group boundary carries real
         * detail. The price is one extra blend per group per axis.
         *
         * The alternative -- a straight 256ths bilinear over every output
         * pixel in both passes -- was measured at 17 ms of a 24 ms frame
         * (see DESIGN F21). This is the same filter for half the blends,
         * because a full bilinear blends every output pixel including the
         * ones that land exactly on a source texel.
         *
         * The exact copy at the group START is a straight memcpy, which the
         * bulk-memory path eats. It is the only one left: F29's missing blend
         * was affordable to leave in place exactly while a SECOND memcpy sat
         * at the group end, and removing that memcpy is what pays for it.
         */
        int weight[kMaxRep];
        // j = 1 .. rep-1 inclusive, and that upper end is the F29 fix: the
        // group's LAST output is a real blend at 256*(rep-1)/rep, not a copy
        // of the far texel. See the block comment above for what that buys
        // and what it costs.
        for (int j = 1; j < rep; ++j)
        {
            weight[j] = (256 * j) / rep;
        }

        row_.resize(tw * 2);
        uint32_t *row_a = row_.data();
        uint32_t *row_b = row_a + tw;

        /*
         * One grid row, tent-blended horizontally into `dst` (w entries).
         *
         * The columns past the last whole group are handled HERE, which is
         * where the F24 fix lives. Pass A used to write w = gw * rep columns
         * and no more, so when the width is not a multiple of the replication
         * factor the last w % rep columns of tmp_ were never touched -- and
         * pass B copied tmp_ straight through, so those columns reached the
         * surface holding whatever was in the buffer before. There is a
         * remainder path for the ROWS at the end of this function and there
         * was none for the columns, which is the same bug on the other axis:
         * on the shipping 1920x1080 buffer every replication factor divides
         * both extents, so no frame could show it, and it surfaced the first
         * time game/test/upscale_check.cpp ran a size that does not divide.
         *
         * Filling the tail as part of the horizontal row means the vertical
         * group and the row-remainder loop below both inherit the fix by
         * construction, exactly as holding it in tmp_ did -- and now it costs
         * one short run inside a buffer that is already in L1.
         */
        const int tail = w_ - gw * rep;
        const auto hrow = [&](uint32_t *const dst, const int gy) {
            const uint32_t *const src =
                grid_.data() + static_cast<size_t>(gy) * static_cast<size_t>(gw);
            for (int gx = 0; gx < gw; ++gx)
            {
                uint32_t *const blk = dst + static_cast<size_t>(gx * rep);
                blk[0] = src[gx];
                if (gx + 1 >= gw)
                {
                    // no far neighbour: hold the edge texel
                    for (int j = 1; j < rep; ++j)
                    {
                        blk[j] = src[gx];
                    }
                    break;
                }
                const uint32_t a = src[gx];
                const uint32_t b = src[gx + 1];
                const int ar = unpack_r(a), ag = unpack_g(a), ab = unpack_b(a);
                const int br = unpack_r(b), bg = unpack_g(b), bb = unpack_b(b);
                const uint32_t top = a & 0xff000000u;
                // j = 1 .. rep-1: every output of the group except the
                // first is a blend, so no group's last output equals the
                // next group's first (F29).
                for (int j = 1; j < rep; ++j)
                {
                    blk[j] = tent_px(top, ar, ag, ab, br, bg, bb, weight[j]);
                }
            }
            if (tail > 0)
            {
                const uint32_t edge = dst[gw * rep - 1];
                for (int x = gw * rep; x < w_; ++x)
                {
                    dst[x] = edge;
                }
            }
        };

        hrow(row_a, 0);

        uint32_t *out = px_.data();
        for (int gy = 0; gy < gh; ++gy)
        {
            if (gy + 1 < gh)
            {
                hrow(row_b, gy + 1);
            }
            else
            {
                // The last grid row has no far neighbour, so it holds the
                // edge row -- exactly what the horizontal pass does for the
                // last column. Reading `row_a + tw` here walked one row past
                // the scratch (it holds grid_h rows), and that heap row
                // landed in the surface's last output row: one unpainted row
                // of whatever the allocator had behind it. It is why the
                // golden boot frame stopped reproducing across dependency
                // snapshots (DESIGN F24).
                row_b = row_a;
            }
            const uint32_t *const a = row_a;
            const uint32_t *const b = row_b;
            /*
             * The group's ONE exact copy: j = 0 is the near row verbatim.
             *
             * It used to be two -- j = rep-1 copied the far row, which is
             * the same value the next group writes as its own j = 0, and
             * that is exactly the duplication F29 is about. The far row is
             * still in hand (it is what the blended rows are made from),
             * it just no longer gets a row of its own.
             */
            if (gy * rep < h_)
            {
                std::copy_n(a, tw, out + static_cast<size_t>(gy * rep) * tw);
            }
            /*
             * The rep - 1 blended rows, written together.
             *
             * They are rep - 1 of every rep output rows -- at rep 4, three
             * quarters of the frame's pixels -- and all of them blend the
             * SAME two source rows, so the loop is turned inside out: one
             * sweep along x for the whole group, with the unpack of a and b
             * and the alpha mask hoisted out of the row loop. The per-pixel
             * arithmetic is the same in the same order; what the turn buys is
             * that the six channel extractions are paid once per group rather
             * than once per pixel. Worth 1.75 ms at rep 4 (F43).
             *
             * It was rep - 2 before the F29 fix, and the extra row is the
             * price of that fix: the group's last output stopped being a
             * memcpy of the far row and became a blend of the two. The
             * hoisted unpack is what makes the extra row cheaper than its
             * share of a standalone blend -- it reuses ar/ag/ab and br/bg/bb
             * and adds only the seven ops of a repack.
             *
             * Two things this is NOT allowed to do, both learned by measuring:
             *
             *  - Address the rows through a pointer TABLE indexed by a
             *    runtime j. It reads like the general way to write it and
             *    measured 1.2 ms SLOWER than not sharing the unpack at all:
             *    a store through a pointer the compiler cannot prove
             *    distinct from a and b makes it reload that address every
             *    pixel. The rep-4 case below uses two named locals and two
             *    weights already in registers, and it wins.
             *  - Make any other replication factor worse than it was. The
             *    general path is therefore the ORIGINAL per-row loop, not a
             *    fused one: rep 2 and 3 have a single blended row and nothing
             *    to share, and rep 5..8 would want their own named locals.
             *    Scaling the win to those is left for whoever tunes them, and
             *    it must be measured per factor rather than assumed.
             */
            const int jlo = 1;
            // rep, not rep - 1: the group's last row is blended too (F29)
            const int jhi = rep;
            // three rows now, so the "everything fits" test asks about the
            // LAST of them, and the last of them is a blend rather than a
            // copy of b -- which is why this condition moved at all.
            const bool all_fit = rep == 4 && (gy * rep + 3 < h_);
            if (all_fit)
            {
                uint32_t *const d1 = out + static_cast<size_t>(gy * rep + 1) * tw;
                uint32_t *const d2 = out + static_cast<size_t>(gy * rep + 2) * tw;
                uint32_t *const d3 = out + static_cast<size_t>(gy * rep + 3) * tw;
                const int w1 = weight[1];
                const int w2 = weight[2];
                const int w3 = weight[3];
                for (int x = 0; x < w_; ++x)
                {
                    const uint32_t av = a[x];
                    const uint32_t bv = b[x];
                    const int ar = unpack_r(av), ag = unpack_g(av);
                    const int ab = unpack_b(av);
                    const int br = unpack_r(bv), bg = unpack_g(bv);
                    const int bb = unpack_b(bv);
                    const uint32_t top = av & 0xff000000u;
                    d1[x] = tent_px(top, ar, ag, ab, br, bg, bb, w1);
                    d2[x] = tent_px(top, ar, ag, ab, br, bg, bb, w2);
                    d3[x] = tent_px(top, ar, ag, ab, br, bg, bb, w3);
                }
            }
            else
            {
                for (int j = jlo; j < jhi; ++j)
                {
                    const int y = gy * rep + j;
                    if (y >= h_)
                    {
                        break;
                    }
                    uint32_t *const dst = out + static_cast<size_t>(y) * tw;
                    const int wgt = weight[j];
                    for (int x = 0; x < w_; ++x)
                    {
                        dst[x] = tent_blend(a[x], b[x], wgt);
                    }
                }
            }
            if (gy + 1 < gh)
            {
                // hrow(gy+1) is the near row for the next group, so the two
                // buffers just change names; the far row is recomputed into
                // whichever one was the near row for this group.
                std::swap(row_a, row_b);
            }
        }
        // grid_h * rep can be short of h_ when the height is not a multiple:
        // hold the last OUTPUT row for the remainder.
        //
        // It used to copy row_a, which was hrow(gh-1) -- correct only while
        // the group's last output was a copy of the far row, because then
        // the last output row and the group's first row were the same
        // pixels. They are not the same any more (F29), so this holds the
        // last row the surface actually got, which is what "hold the edge"
        // has to mean once the filter is not copying its own output.
        if (gh * rep < h_)
        {
            const uint32_t *const edge =
                out + static_cast<size_t>(gh * rep - 1) * tw;
            for (int y = gh * rep; y < h_; ++y)
            {
                std::copy_n(edge, tw, out + static_cast<size_t>(y) * tw);
            }
        }
    }

    void Canvas::hspan(const int y, const int x0, const int x1, const uint32_t c)
    {
        // horizontal spans are always written 1:1 (the sky band and the
        // weapon pass); a world pass writes one grid pixel per call
        if (y < 0 || y >= active_h())
        {
            return;
        }
        int a = x0;
        int b = x1;
        normalize(a, b);
        if (a < 0)
        {
            a = 0;
        }
        if (b > active_w() - 1)
        {
            b = active_w() - 1;
        }
        if (a > b)
        {
            return;
        }
        uint32_t *row = active_row(y);
        std::fill_n(row + a, static_cast<size_t>(b - a + 1), c);
    }

    void Canvas::vspan(const int x, const int y0, const int y1, const uint32_t c)
    {
        if (x < 0 || x >= active_w())
        {
            return;
        }
        int a = y0;
        int b = y1;
        normalize(a, b);
        if (a < 0)
        {
            a = 0;
        }
        if (b > active_h() - 1)
        {
            b = active_h() - 1;
        }
        if (a > b)
        {
            return;
        }
        const size_t w = active_stride();
        uint32_t *p = active_data() + static_cast<size_t>(a) * w +
                      static_cast<size_t>(x);
        for (int y = a; y <= b; ++y, p += w)
        {
            *p = c;
        }
    }

    void Canvas::vspan_grad(const int x, const int y0, const int y1,
                            const uint32_t top, const uint32_t bot)
    {
        if (x < 0 || x >= active_w())
        {
            return;
        }
        int a = y0;
        int b = y1;
        normalize(a, b);
        if (a < 0)
        {
            a = 0;
        }
        if (b > active_h() - 1)
        {
            b = active_h() - 1;
        }
        if (a > b)
        {
            return;
        }
        const int steps = b - a;
        int r0 = unpack_r(top), g0 = unpack_g(top), b0 = unpack_b(top);
        const int r1 = unpack_r(bot), g1 = unpack_g(bot), b1 = unpack_b(bot);
        const int dr = steps > 0 ? (r1 - r0) / steps : 0;
        const int dg = steps > 0 ? (g1 - g0) / steps : 0;
        const int db = steps > 0 ? (b1 - b0) / steps : 0;
        const size_t w = active_stride();
        uint32_t *p = active_data() + static_cast<size_t>(a) * w +
                      static_cast<size_t>(x);
        for (int y = a; y <= b; ++y, p += w)
        {
            *p = pack(r0, g0, b0);
            r0 += dr;
            g0 += dg;
            b0 += db;
        }
    }

    void Canvas::vspan_blend(const int x, const int y0, const int y1,
                             const uint32_t c, const float alpha)
    {
        if (x < 0 || x >= active_w() || alpha <= 0.f)
        {
            return;
        }
        int a = y0;
        int b = y1;
        normalize(a, b);
        if (a < 0)
        {
            a = 0;
        }
        if (b > active_h() - 1)
        {
            b = active_h() - 1;
        }
        if (a > b)
        {
            return;
        }
        if (alpha >= 1.f)
        {
            vspan(x, a, b, c);
            return;
        }
        // fixed-point coverage: 0..256, so the inner loop stays in ints
        const int ia = static_cast<int>(alpha * 256.f + 0.5f);
        const int sr = unpack_r(c), sg = unpack_g(c), sb = unpack_b(c);
        const size_t w = active_stride();
        uint32_t *p = active_data() + static_cast<size_t>(a) * w +
                      static_cast<size_t>(x);
        for (int y = a; y <= b; ++y, p += w)
        {
            const uint32_t d = *p;
            const int dr = unpack_r(d), dg = unpack_g(d), db = unpack_b(d);
            *p = pack(sr + ((dr - sr) * (256 - ia)) / 256,
                      sg + ((dg - sg) * (256 - ia)) / 256,
                      sb + ((db - sb) * (256 - ia)) / 256);
        }
    }

    void Canvas::vspan_ramp(const int gx, const int y0, const int y1,
                            const uint32_t *ramp, const int len, float v,
                            const float dv)
    {
        if (gx < 0 || gx >= active_w())
        {
            return;
        }
        int a = y0;
        int b = y1;
        normalize(a, b);
        if (a < 0)
        {
            a = 0;
        }
        if (b > active_h() - 1)
        {
            b = active_h() - 1;
        }
        if (a > b)
        {
            return;
        }
        // power of two, and the walk WRAPS: a wall taller than one texture
        // repeat tiles its own texture vertically, which is what kWallRepeat
        // means. So the mask is len - 1, and len has to be a power of two --
        // see the declaration.
        const int kRamp = len > 0 ? len : 1;
        const size_t w = active_stride();
        uint32_t *p = active_data() + static_cast<size_t>(a) * w +
                      static_cast<size_t>(gx);
        for (int y = a; y <= b; ++y, p += w)
        {
            int idx = static_cast<int>(v);
            idx &= kRamp - 1;
            *p = ramp[idx];
            v += dv;
        }
    }

    void Canvas::fill_rect(const int x0, const int y0, const int x1,
                           const int y1, const uint32_t c)
    {
        int top = y0;
        int bottom = y1;
        normalize(top, bottom);
        for (int y = top; y <= bottom; ++y)
        {
            hspan(y, x0, x1, c);
        }
    }

    void Canvas::fill_gradient_v(const int x0, const int y0, const int x1,
                                 const int y1, const Rgba &from,
                                 const Rgba &to)
    {
        int top = y0;
        int bottom = y1;
        normalize(top, bottom);
        const int steps = bottom - top;
        for (int y = top; y <= bottom; ++y)
        {
            hspan(y, x0, x1, pack(lerp_rgba(from, to, y - top, steps)));
        }
    }

    void Canvas::fill_hole(const int cx, const int cy, const int radius,
                           const uint32_t core, const uint32_t rim,
                           const uint32_t skirt)
    {
        if (radius < 1)
        {
            return;
        }
        // Three discs, largest first. A hole is not a dot: what sells it is
        // the rim of bright material the round pushed out and the shadow
        // the hole's own depth implies. Each is one fill_circle, so the
        // whole decal is three span-based discs -- no per-pixel loop, which
        // is the rule D9 exists to protect.
        fill_circle(cx, cy, radius, skirt);
        if (radius >= 2)
        {
            fill_circle(cx, cy, radius - 1, rim);
        }
        // The core must leave the rim VISIBLE, and the first version did
        // not: it drew the core at (radius * 2) / 3, which at radius 2 is
        // radius 1 -- the same disc as the rim, drawn over it. So a hole
        // at the range where most of them are (2 px, i.e. anything more
        // than four cells away) came out as a dark dot with no rim at all,
        // and the rim is the whole reason a hole reads as a hole.
        //
        // radius - 2 keeps one ring of rim at every size: radius 3 gives
        // core 1 inside rim 2, radius 2 gives core 0 -- a single dark pixel
        // inside a bright ring, which is what a 2-pixel bullet hole looks
        // like.
        const int core_radius = radius - 2;
        if (core_radius >= 1)
        {
            fill_circle(cx, cy, core_radius, core);
        }
        else
        {
            fill_rect(cx, cy, cx, cy, core);
        }
    }

    void Canvas::fill_circle(const int cx, const int cy, const int radius,
                             const uint32_t c)
    {
        if (radius < 0)
        {
            return;
        }
        // midpoint circle, mirroring Graphics::fill_circle: four
        // horizontal spans per step, walked to the octant limit. Kept
        // verbatim (including the float octant_limit -- the lib's desktop
        // build is not compiled with USE_INTEGER_GEOMETRY) so the discs
        // land on exactly the same pixels.
        int px = 0;
        int py = radius;
        int d = -radius;
        int x2m1 = -1;
        const double octant_limit =
            static_cast<double>(radius) / std::sqrt(2.0);

        auto incircle_pixels = [&](const int a, const int b) {
            hspan(cy + b, cx - a, cx + a, c);
            hspan(cy - b, cx - a, cx + a, c);
            hspan(cy + a, cx - b, cx + b, c);
            hspan(cy - a, cx - b, cx + b, c);
        };

        incircle_pixels(px, py);
        for (px = 1; static_cast<double>(px) < octant_limit; ++px) {
            x2m1 += 2;
            d += x2m1;
            if (d >= 0) {
                py--;
                d -= py << 1;
            }
            incircle_pixels(px, py);
        }
    }

    void Canvas::glow(const int cx, const int cy, const int radius, const int r,
                      const int g, const int b, const float strength)
    {
        if (radius <= 0 || strength <= 0.f)
        {
            return;
        }
        const int rr = radius * radius;
        for (int y = cy - radius; y <= cy + radius; ++y)
        {
            if (y < 0 || y >= active_h())
            {
                continue;
            }
            for (int x = cx - radius; x <= cx + radius; ++x)
            {
                if (x < 0 || x >= active_w())
                {
                    continue;
                }
                const int dx = x - cx;
                const int dy = y - cy;
                const int d2 = dx * dx + dy * dy;
                if (d2 > rr)
                {
                    continue;
                }
                // linear falloff to the rim: no sqrt, and a sprite this
                // small does not need the extra fidelity
                const float f = 1.f - std::sqrt(static_cast<float>(d2)) /
                                            static_cast<float>(radius);
                const float k = f * f * strength;
                uint32_t &p = active_row(y)[x];
                // X-20: the int channel -> float promotion of `r * k` was
                // always implicit; spelled out here without changing it
                int nr = unpack_r(p) + static_cast<int>(static_cast<float>(r) * k);
                int ng = unpack_g(p) + static_cast<int>(static_cast<float>(g) * k);
                int nb = unpack_b(p) + static_cast<int>(static_cast<float>(b) * k);
                p = pack(nr > 255 ? 255 : nr, ng > 255 ? 255 : ng,
                         nb > 255 ? 255 : nb);
            }
        }
    }
} // namespace zb::raster
