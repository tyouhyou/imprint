#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pixel.hpp"

namespace zb::raster
{
    /*
     * The tent's one tap: `from` lerped towards `to` by w/256, per
     * channel, in integers. Alpha is carried from `from` untouched.
     *
     * Public, and deliberately so. This is the arithmetic the golden
     * frames were blessed against, and a golden hash can only prove
     * "same as last time" (DESIGN F19) -- it cannot tell you the filter
     * is still the filter. Exposing the tap lets
     * game/test/upscale_check.cpp hold it against the documented
     * per-channel expression over every endpoint pair and every weight a
     * replication factor can produce, which is the whole domain rather
     * than a sample.
     *
     * The tempting one-liner here is wrong:
     *
     *     (a + ((b - a) * w >> 8)) & 0xffffff   // spills across lanes
     *
     * a packed multiply shifts bits out of each byte into the next one,
     * so a channel's low byte picks up its neighbour's high bits. On a
     * dark gradient the error is invisible; measured over the full
     * domain it is 523744 wrong answers out of 2.1 million.
     */
    [[nodiscard]] uint32_t tent_blend(uint32_t from, uint32_t to, int w);

    /*
     * The consumer-owned pixel block for the 3D viewport: a coarse
     * color/depth grid for the world plus the full-size surface the
     * framework hands the UI.
     *
     * Every primitive here is a deliberate, pixel-exact re-implementation
     * of the zb::ui::core::Graphics call it replaces -- same spans, same
     * rounding, same clip-then-fill order -- with the per-pixel
     * clip/damage/blend gate dropped (see DESIGN.md D8 / F9). Keeping the
     * arithmetic identical is what makes phase 1 verifiable: the frames
     * before and after the swap are required to be bit-identical, so the
     * only thing this layer is allowed to change is speed.
     *
     * Semantics match Graphics: coordinates are inclusive and normalized
     * (a reversed rect fills the same block), and out-of-range writes are
     * clipped rather than dropped -- callers already pre-clamp, so the
     * clip is a no-op in practice and cannot change the output.
     *
     * Resolution scale (DESIGN.md D9), phase 8. There are TWO surfaces:
     *
     *   grid_  gw x gh  (surface / replicate())  where the world passes
     *                                           shade: walls, floor,
     *                                           ceiling, bodies
     *   px_    vw x vh  the full surface the framework is handed
     *
     * The world shades into the grid; `upscale()` then resamples the grid
     * onto the surface, and the passes that must stay sharp (the weapon,
     * the decals) draw on the surface afterwards. Replication used to be
     * fused into the shading loops, which measured ~2x cheaper than a
     * gather pass -- but it also meant every shaded pixel landed as an
     * n x n block of identical color, which is the single ugliest thing a
     * low internal resolution can do to an image. Interpolating the
     * upscale costs a pass and buys back the gradients (see DESIGN F21).
     *
     * Which surface a primitive touches is the existing `replicate()`
     * switch, not a new one: replicate() > 1 means "the world grid",
     * replicate() == 1 means "the surface, one shaded pixel per pixel".
     */

    class Canvas
    {
    public:
        void resize(int w, int h);

        [[nodiscard]] int width() const noexcept { return w_; }
        [[nodiscard]] int height() const noexcept { return h_; }
        [[nodiscard]] size_t stride() const noexcept
        {
            return static_cast<size_t>(w_);
        }
        [[nodiscard]] const uint32_t *pixels() const noexcept
        {
            return px_.data();
        }
        [[nodiscard]] size_t pixel_count() const noexcept { return px_.size(); }

        // ---- resolution scale (D9) --------------------------------
        // grid_w/grid_h are cached rather than divided on read: the
        // raster asks for them once per triangle column, and an integer
        // division there cost more than the span it was guarding.
        // n is clamped into 1..8: the low end because rep 0 divides
        // nothing, the high end because the upscale's per-output weight
        // table is a fixed 8-slot array (canvas.cpp kMaxRep) -- a larger
        // n degrades to 8 instead of walking that table past its end.
        void set_replicate(int n);
        [[nodiscard]] int replicate() const noexcept { return rep_; }
        // the grid a world pass works on
        [[nodiscard]] int grid_w() const noexcept { return grid_w_; }
        [[nodiscard]] int grid_h() const noexcept { return grid_h_; }

        /*
         * Direct access to one grid row -- the hot loops (floor, ceiling)
         * walk a row and want a pointer, not a bounds-checked setter.
         * Null when the surface is at 1:1, where there is no grid.
         */
        [[nodiscard]] uint32_t *grid_row(int gy) noexcept
        {
            return grid_.data() + static_cast<size_t>(gy) *
                                      static_cast<size_t>(grid_w_);
        }
        [[nodiscard]] const uint32_t *grid_row(int gy) const noexcept
        {
            return grid_.data() + static_cast<size_t>(gy) *
                                      static_cast<size_t>(grid_w_);
        }
        [[nodiscard]] uint32_t *grid_at(int gx, int gy) noexcept
        {
            return grid_row(gy) + gx;
        }
        [[nodiscard]] const uint32_t *grid_data() const noexcept
        {
            return grid_.data();
        }
        [[nodiscard]] size_t grid_count() const noexcept { return grid_.size(); }

        /*
         * The sky mask: one byte per ACTIVE pixel, set where the sky ramp is
         * still what the frame shows there.
         *
         * Stars cannot be drawn on the grid. For rep 4 the tent puts a texel's
         * exact value at TWO output pixels and then ramps (1, 1, 1/4, 1/2, 0),
         * so a star texel becomes a 2x2 flat square core with a ramp around
         * it -- a square, not a star -- and two equal texels give a SIX pixel
         * plateau, so the core cannot be sharpened by neighbour placement
         * either. Stars are therefore drawn after the upscale, at full
         * resolution, and the only thing they need from the renderer is
         * "which pixels are sky", which nothing tracked.
         *
         * Sizing follows the ACTIVE surface rather than the grid, so the
         * accessors below read the same whether the world is drawing on the
         * grid (rep > 1) or straight onto the surface (rep == 1). That is not
         * tidiness: a look that changes with the resolution tier is a broken
         * renderer (D9), and stars that appear at one render scale and not at
         * another is exactly that.
         *
         * The mask is maintained by construction rather than by asking: the
         * sky fill marks its rows, and every pass that writes geometry clears
         * the cells it wrote. A star landing on a wall is therefore only
         * possible if a geometry pass forgot to clear, which the golden frames
         * and the 15 shots would show immediately.
         */
        [[nodiscard]] uint8_t *sky_row(int gy) noexcept
        {
            return sky_.data() + static_cast<size_t>(gy) *
                                     static_cast<size_t>(active_w());
        }
        [[nodiscard]] int sky_w() const noexcept { return active_w(); }
        [[nodiscard]] int sky_h() const noexcept { return active_h(); }
        // mark a horizontal band of rows as sky (the ramp fill)
        void mark_sky(int y0, int y1) noexcept;
        /*
         * Record one column's wall span instead of clearing bytes for it.
         *
         * This started as clear_sky(gx, y0, y1), and it was the single most
         * expensive thing the star pass added: measured at about 1.3 ms by
         * min-of-five p50 against the same binary with the clear disabled. The
         * reason is the access order, not the work -- 480 columns each
         * striding down the mask buffer a row at a time is 65k writes that
         * touch 480 separate lines in the wrong sequence, and no amount of
         * "it is only a byte store" survives that.
         *
         * The wall pass has ALREADY computed top and bottom for the column,
         * because it needs them to shade the span. Recording them is two
         * int16 stores per column -- 960 per frame, sequential -- and the star
         * pass then asks a question instead of reading a maintained buffer:
         * a cell is sky if it is outside every recorded span. Maintenance by
         * remembering beats maintenance by clearing.
         */
        void set_wall_span(int gx, int y0, int y1) noexcept
        {
            if (gx < 0 || gx >= active_w() || sky_sentinel_ <= y1)
            {
                return;  // an empty span never occludes anything
            }
            span_top_[static_cast<size_t>(gx)] = static_cast<int16_t>(y0 < 0 ? 0 : y0);
            span_bot_[static_cast<size_t>(gx)] = static_cast<int16_t>(y1);
        }
        void clear_sky_cell(int gx, int gy) noexcept
        {
            if (gx >= 0 && gy >= 0 && gx < active_w() && gy < active_h())
            {
                sky_row(gy)[gx] = 0;
            }
        }
        /*
         * Is a WALL in front of this cell?
         *
         * The wall pass records one span per column -- the nearest wall face's
         * top and bottom row -- and anything inside it is occluded. This was
         * only ever inlined inside sky_at(), which is fine for deciding whether
         * a star may be drawn and useless for deciding whether the ceiling may
         * be drawn, which is the same question asked by a second pass. Named,
         * because the ceiling pass needs it (see draw_ceiling) and a private
         * helper reachable only through sky_at() would have meant asking the
         * sky mask a question it does not mean.
         */
        [[nodiscard]] bool wall_at(const int gx, const int gy) const noexcept
        {
            if (gx < 0 || gy < 0 || gx >= active_w())
            {
                return false;
            }
            return gy >= span_top_[static_cast<size_t>(gx)] && gy <= span_bot_[static_cast<size_t>(gx)];
        }
        [[nodiscard]] bool sky_at(const int gx, const int gy) const noexcept
        {
            if (gx < 0 || gy < 0 || gx >= active_w() || gy >= active_h())
            {
                return false;
            }
            // outside every wall column's span...
            if (wall_at(gx, gy))
            {
                return false;
            }
            // ...and not a ceiling. The ceiling is the one occluder that is
            // genuinely per-CELL rather than per-column -- its screen row
            // depends on the distance to the cell -- so it keeps a byte, and
            // it is cleared from inside a pass that already walks rows outer
            // and columns inner, which makes those writes sequential.
            return sky_[static_cast<size_t>(gy) * static_cast<size_t>(active_w()) +
                        static_cast<size_t>(gx)] != 0;
        }
        // every column starts the frame occluding nothing
        void reset_wall_spans() noexcept
        {
            for (int gx = 0; gx < active_w(); ++gx)
            {
                span_top_[static_cast<size_t>(gx)] = static_cast<int16_t>(sky_sentinel_);
                span_bot_[static_cast<size_t>(gx)] = static_cast<int16_t>(-1);
            }
        }

        /*
         * Resample the grid onto the surface.
         *
         * Separable bilinear in two passes: horizontal into a
         * (width x grid_h) scratch, then vertical onto the surface. One
         * fused 2x2 pass would be a quarter fewer memory touches but four
         * taps per output pixel instead of two, and the second tap's row
         * is the one that misses cache on every line -- measured cheaper
         * as two passes, and it is also the only form that lets the
         * vertical pass work on rep-sized groups of rows.
         *
         * No-op at replicate() == 1: there is no grid, the world already
         * drew on the surface.
         */
        void upscale();

        // ---- depth buffer: one entry per grid column ----------------
        [[nodiscard]] const float *depth_data() const noexcept
        {
            return depth_.data();
        }
        [[nodiscard]] float depth(int gx) const noexcept
        {
            return depth_[static_cast<size_t>(gx)];
        }
        void set_depth(int gx, const float d) noexcept
        {
            depth_[static_cast<size_t>(gx)] = d;
        }

        /*
         * Per-pixel depth for the ONE pass that needs it -- the mesh
         * triangle path (mesh.hpp): stored as 1/view-depth (larger =
         * nearer), 0 = nothing drawn yet, sized to the ACTIVE surface and
         * kept in step by resize()/set_replicate() like the sky mask.
         *
         * The per-column buffer above is exact for vertical prisms (walls,
         * boxes); an organic mesh self-occludes at pixel granularity, so
         * the mesh path tests BOTH: its own per-pixel depth, then the
         * column's wall distance. Cleared by reset_mesh_depth() -- once
         * per frame, NOT per body, so a body drawn later depth-tests
         * against the bodies drawn before it.
         */
        void reset_mesh_depth() noexcept;
        [[nodiscard]] float *mesh_depth_data() noexcept
        {
            return mesh_depth_.data();
        }
        [[nodiscard]] size_t mesh_depth_count() const noexcept
        {
            return mesh_depth_.size();
        }

        /*
         * Direct per-pixel access to whichever surface the world is
         * currently drawing on (the grid at rep > 1, the surface at 1:1).
         *
         * The one pass that needs this is the mesh trooper: a triangle
         * rasterizer is per-pixel by nature, and forcing it through
         * span primitives would be exactly the per-pixel shading tax the
         * spans exist to avoid -- at which point there is no triangle
         * path to evaluate. Every OTHER world pass must keep using the
         * span primitives; this is an exception, not a door.
         *
         * The sky mask is the caller's job: a pixel written here that is
         * not the sky must clear its sky cell (clear_sky_cell), or the
         * star pass will draw through the body.
         */
        [[nodiscard]] uint32_t *world_row(int y) noexcept
        {
            return active_row(y);
        }
        [[nodiscard]] int world_w() const noexcept { return active_w(); }
        [[nodiscard]] int world_h() const noexcept { return active_h(); }

        // ---- primitives (Graphics equivalents) ----
        void fill_rect(int x0, int y0, int x1, int y1, uint32_t c);
        // vertical (top->bottom) linear ramp, radius 0
        void fill_gradient_v(int x0, int y0, int x1, int y1, const Rgba &from,
                             const Rgba &to);
        void hspan(int y, int x0, int x1, uint32_t c);
        void vspan(int x, int y0, int y1, uint32_t c);
        /*
         * A vertical span whose color is picked from a small ramp by row:
         * `v` is the ramp position at y0 and advances by `dv` per row.
         *
         * This is the textured wall column. It has to be a SPAN and not a
         * per-pixel loop: a wall span is ~300 rows, and paying full
         * per-pixel shading math for each of them cost ~9 ms a frame
         * where the ramp costs ~1.5 (measured -- the difference between
         * rep=2 at 29 ms and rep=2 with flat spans at 16 ms). The ramp is
         * built once per column, because a wall's light does not change
         * down its own face.
         *
         * `len` is the ramp's length in entries, and it has to be passed
         * rather than assumed. The wall pass picks a mip level per column
         * (see Textures), so the ramp it builds can be 128, 64, 32 ... long,
         * and a fixed 128 here walked straight past the end of a shorter one
         * into uninitialised stack -- which is not a subtle wrong colour but
         * white walls with coloured speckle, because it read whatever was
         * above the array on the stack. The index WRAPS (a wall taller than
         * one repeat tiles its texture), so `len` must be a power of two.
         */
        void vspan_ramp(int gx, int y0, int y1, const uint32_t *ramp, int len,
                        float v, float dv);
        // vertical ramp between two packed colors, one integer step per
        // row (in-face gradients: the color depends on the row only, so
        // this stays a single pass over the span)
        void vspan_grad(int x, int y0, int y1, uint32_t top, uint32_t bot);
        // src-over blend of one color over the span, alpha 0..1. The
        // body's contact shadow needs this: a floor-aligned blob has to
        // DARKEN whatever the floor pass wrote there (concrete, grime,
        // the flashlight pool), not paint a flat patch over it.
        void vspan_blend(int x, int y0, int y1, uint32_t c, float alpha);
        void fill_circle(int cx, int cy, int radius, uint32_t c);
        /*
         * A bullet hole: a dark core, a lighter rim of blown-out material,
         * and a shadow skirt. Filled as three passes rather than one
         * per-pixel loop, because the whole point of D9 is that this class
         * of shading stays in spans and rectangles.
         *
         * Drawn at 1:1 into the surface (callers set replicate(1) first) --
         * a 3-pixel decal is exactly the sort of thing the resolution
         * scale would erase, and it is the one place where the player is
         * looking for a detail that must not be blocky.
         */
        void fill_hole(int cx, int cy, int radius, uint32_t core,
                       uint32_t rim, uint32_t skirt);
        // additive radial glow, `strength` 0..1 at the center falling to
        // 0 at the rim (muzzle flash, tracers). Per-pixel, so keep it to
        // a sprite-sized radius.
        void glow(int cx, int cy, int radius, int r, int g, int b,
                  float strength);

        /*
         * One additive pixel, on the SURFACE -- not on the active surface.
         *
         * This is the only blitter that is deliberately not rep-aware, and
         * the reason is that its only caller is the star pass, which runs
         * AFTER the upscale: at that point the active surface is still the
         * grid (rep_ is reset to 1 later, for the viewmodel), so an
         * active-surface write would put stars into the grid where the next
         * frame's world pass would overwrite them and the tent would smear
         * them anyway. Writing px_ directly is what "full resolution" means
         * here.
         *
         * `gain` is 0..256, so a caller can express a magnitude without
         * touching a float.
         */
        void add_pixel(const int x, const int y, const int r, const int g,
                       const int b, const int gain) noexcept;

    private:
        // the surface the next primitive writes to: the grid while the
        // world is drawing, the surface once replicate(1) is set
        [[nodiscard]] uint32_t *active_row(int y) noexcept
        {
            return rep_ > 1 ? grid_row(y)
                            : px_.data() + static_cast<size_t>(y) *
                                               static_cast<size_t>(w_);
        }
        [[nodiscard]] uint32_t *active_data() noexcept
        {
            return rep_ > 1 ? grid_.data() : px_.data();
        }
        [[nodiscard]] int active_w() const noexcept
        {
            return rep_ > 1 ? grid_w_ : w_;
        }
        [[nodiscard]] int active_h() const noexcept
        {
            return rep_ > 1 ? grid_h_ : h_;
        }
        [[nodiscard]] size_t active_stride() const noexcept
        {
            return rep_ > 1 ? static_cast<size_t>(grid_w_)
                            : static_cast<size_t>(w_);
        }

        int w_ = 0;
        int h_ = 0;
        int rep_ = 1;
        int grid_w_ = 0;
        int grid_h_ = 0;
        std::vector<uint32_t> px_;
        std::vector<uint32_t> grid_;
        // The upscale's working set: two tent-blended rows, 2 x w x 4. The
        // separable filter used to hand these from one pass to the other
        // through a width x grid_h scratch (2.07 MB at 1080p), which was
        // 4.15 MB of DRAM traffic per frame to move 15 KB of data; see
        // Canvas::upscale.
        std::vector<uint32_t> row_;
        std::vector<uint8_t> sky_;                  // active_w x active_h, 1 where sky shows
        std::vector<int16_t> span_top_, span_bot_;  // per column, the wall span
        // larger than any row index, so a column with no span occludes
        // nothing; int16 because grid_h is a few hundred
        static constexpr int sky_sentinel_ = 30000;
        std::vector<float> depth_;
        // the mesh pass's per-pixel depth (active-surface sized, above)
        std::vector<float> mesh_depth_;
    };
}  // namespace zb::raster
