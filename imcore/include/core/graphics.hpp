#pragma once

#include <memory>
#include "im_defines.hpp"
#include "color.hpp"
#include "error.hpp"
#include "image_view.hpp"
#include "ptr.hpp"

namespace zb::ui::core
{
    class Graphics
    {
    public:
        using ptr = zb::SharedPtr<Graphics>;

        static ptr make_ptr(uint32_t width, uint32_t height, void *data = nullptr)
        {
            return zb::make_shared<Graphics>(width, height, data);
        }

    public:
        // TODO: specify Color type with template

        /*
         * The optional `data` buffer is wrapped, not copied: the framework
         * writes into it, so it must be writable (no const buffer).
         */
        Graphics(uint32_t width, uint32_t height, void* data);
        ~Graphics();

        // Graphics manages a raw pixel buffer. Copying the object itself is forbidden
        // to avoid double-free / unexpected buffer aliasing:
        //   - share a surface: use shared_ptr<Graphics> / Graphics::ptr (cheap)
        //   - deep-copy a region: use clone()
        //   - view a sub-region: use clip()
        Graphics(const Graphics &) = delete;
        Graphics &operator=(const Graphics &) = delete;

        /* deep-copy the specified area, and set size to the area */
        ptr clone(int x, int y, int32_t width, int32_t height) const;

        /* RAII restore of the draw state set up by clip_safe() */
        class ClipGuard
        {
        public:
            explicit operator bool() const { return valid_; }

            ClipGuard(const ClipGuard &) = delete;
            ClipGuard &operator=(const ClipGuard &) = delete;

            ~ClipGuard()
            {
                if (valid_)
                {
                    g_.draw_area = saved_area_;
                    g_.draw_area_offset_enabled = saved_offset_enabled_;
                    g_.draw_area_offset = saved_offset_;
                }
            }

        private:
            friend class Graphics;
            ClipGuard(Graphics &g, const imarea_t &saved_area, const bool saved_offset_enabled,
                      const impoint_t &saved_offset, const bool valid)
                : g_(g), saved_area_(saved_area), saved_offset_enabled_(saved_offset_enabled),
                  saved_offset_(saved_offset), valid_(valid)
            {
            }

            Graphics &g_;
            imarea_t saved_area_;
            bool saved_offset_enabled_;
            impoint_t saved_offset_;
            bool valid_ = false;
        };

        /*
         * Restricts drawing to the requested area: saves the current draw
         * state, intersects the request with the current draw area and
         * switches the surface into the clipped state. The original state
         * is restored when the guard goes out of scope. Zero allocation:
         * this is a stack value, so per-widget per-frame clipping no longer
         * creates objects (see docs/code-contract.md, hot path).
         *
         * Never throws: off-screen widgets produce a guard that converts to
         * false, and drawing on them is a no-op.
         */
        [[nodiscard]] ClipGuard clip_safe(int x, int y, int32_t width, int32_t height);

        /*
         * Like clip_safe(), but the request is in SURFACE coordinates and
         * the intersection bound is the whole surface, not the current
         * draw area: the escape hatch that lets the outer-shadow pass
         * (P-2e) reach past nested box clips the way a browser box-shadow
         * overdraws already painted content and is covered only by later
         * paint. The local-coordinate origin is the requested box, like
         * clip_safe().
         */
        [[nodiscard]] ClipGuard clip_surface_safe(int x, int y, int32_t width, int32_t height);

        /*
         * Damage culling: when active, widgets whose bounds do not
         * intersect the reported region skip rendering entirely (whole
         * subtrees never reach the rasterizer). Set by the window once
         * per paint(); cleared afterwards. Coordinates are surface
         * (absolute) pixels. The rect is HALF-OPEN: (l, t) inclusive,
         * (r, b) exclusive — every raster entry point clips through
         * damage_contains / the fill clamp derived from it.
         */
        void set_damage(int l, int t, int r, int b)
        {
            damage_l_ = l;
            damage_t_ = t;
            damage_r_ = r;
            damage_b_ = b;
            damage_on_ = true;
        }
        void clear_damage() { damage_on_ = false; }
        [[nodiscard]] bool damage_on() const { return damage_on_; }
        [[nodiscard]] inline bool damage_intersects(int x, int y,
                                                    int w, int h) const
        {
            return x < damage_r_ && y < damage_b_ && x + w > damage_l_ && y + h > damage_t_;
        }

        [[nodiscard]] inline Color *data() const
        {
            return pixels;
        }

        [[nodiscard]] inline imsize_t size() const
        {
            return imsize;
        }

        inline void enable_alpha(bool enabled)
        {
            alpha_enabled = enabled;
        }

        [[nodiscard]] inline bool is_alpha_enabled() const
        {
            return alpha_enabled;
        }

        /*
         * Render mode (S-1): FULL draws everything (the default, current
         * behavior); WIREFRAME degrades the shape fills to 1px outlines
         * for layout-debug/low-power views (strokes, text, images,
         * damage, and hit-testing are untouched); SKETCH is reserved and
         * renders as FULL until S-2. Per-Graphics state, set once per
         * frame/window; the rasterizer branches above the pixel gates so
         * clipping, damage, and the pixel model are unaffected.
         */
        enum class render_mode
        {
            full,
            wireframe,
            sketch,  // reserved (S-2): currently renders as full
        };

        inline void set_render_mode(const render_mode mode)
        {
            render_mode_ = mode;
        }

        [[nodiscard]] inline render_mode get_render_mode() const
        {
            return render_mode_;
        }

#pragma region draw and fill

        void fill(const Color &colr);

        // TODO: draw with line thickness

        // TODO: gamma correction
        void draw_pixel(int x, int y, const Color &colr);
        inline void draw_pixel(const impoint_t &p, const Color &colr)
        {
            draw_pixel(p.x, p.y, colr);
        }

        void draw_line(int x1, int y1, int x2, int y2, const Color &colr);
        inline void draw_line(const impoint_t &p1, const impoint_t &p2, const Color &colr)
        {
            draw_line(p1.x, p1.y, p2.x, p2.y, colr);
        }

        void draw_triangle(const impoint_t &p1, const impoint_t &p2, const impoint_t &p3, const Color &colr);
        inline void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, const Color &colr)
        {
            draw_triangle({x1, y1}, {x2, y2}, {x3, y3}, colr);
        }

        // TODO: fill triangle, draw/fill polygon

        void draw_circle(int x, int y, int radius, const Color &colr);
        void fill_circle(int x, int y, int radius, const Color &colr);
        inline void draw_circle(const impoint_t &p, int radius, const Color &colr)
        {
            draw_circle(p.x, p.y, radius, colr);
        }
        inline void fill_circle(const impoint_t &p, int radius, const Color &colr)
        {
            fill_circle(p.x, p.y, radius, colr);
        }

        /* draw a rectangle which left-top at (x1, y1), right-bottom at (x2, y2) */
        void draw_rect(int x1, int y1, int x2, int y2, const Color &colr);
        void fill_rect(int x1, int y1, int x2, int y2, const Color &colr);
        inline void draw_rect(const impoint_t &p, const imsize_t &s, const Color &colr)
        {
            draw_rect(p.x, p.y, p.x + s.width, p.y + s.height, colr);
        }
        inline void fill_rect(const impoint_t &p, const imsize_t &s, const Color &colr)
        {
            fill_rect(p.x, p.y, p.x + s.width, p.y + s.height, colr);
        }

        void draw_ellipse(int centerx, int centery, int radiusx, int radiusy, const Color &colr);
        void fill_ellipse(int centerx, int centery, int radiusx, int radiusy, const Color &colr);
        inline void draw_ellipse(const impoint_t &center, const imsize_t radiuses, const Color &colr)
        {
            draw_ellipse(center.x, center.y, radiuses.width, radiuses.height, colr);
        }
        inline void fill_ellipse(const impoint_t &center, const imsize_t &radiuses, const Color &colr)
        {
            fill_ellipse(center.x, center.y, radiuses.width, radiuses.height, colr);
        }

        /* fill the triangle with the given vertices, edge-inclusive */
        void fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, const Color &colr);
        inline void fill_triangle(const impoint_t &p1, const impoint_t &p2, const impoint_t &p3, const Color &colr)
        {
            fill_triangle(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, colr);
        }

        /*
         * Fill the rect with a linear interpolation from `from` to `to`,
         * along columns (horizontal, left->right) or rows (vertical,
         * top->bottom). Channels interpolate through the 8-bit-normalized
         * accessors (A-19); a single-column/row span degenerates to the
         * flat `from` color. Every row goes through draw_pixel, so the
         * clip/damage/alpha conventions hold (A-12/A-13). A positive
         * `radius` rounds the corners like fill_round_rect_aa (same
         * chord + fringe formula; corner rows interpolate per pixel);
         * <= 0 keeps the square fast path. Wireframe degrades to the
         * outline like the square form.
         */
        void fill_gradient(int x1, int y1, int x2, int y2, const Color &from, const Color &to, bool horizontal = true,
                           int radius = 0);

        /*
         * Fill the rect with a two-stop circular gradient (P-1): stop
         * `from` at the center (cx, cy, local pixels), stop `to` at the
         * farthest-corner radius; stop offsets from_p/to_p (0..100, CSS
         * stop positions) rescale the ramp, outside clamps to the end
         * stops. Integer-only (isqrt per pixel, no FPU); alpha lerps
         * with the channels. `radius` rounds the corners like
         * fill_gradient above. Wireframe degrades to the outline.
         */
        void fill_radial(int x1, int y1, int x2, int y2, int cx, int cy, const Color &from, int from_p,
                         const Color &to, int to_p, int radius = 0);

        /*
         * Fill the rect with a conic (angular) sweep (P-2b): `from_deg`
         * is the start angle in CSS degrees (0 = up, clockwise), stops
         * are `nstops` (2..4) non-decreasing degree positions with one
         * color each; the pixel angle picks its segment, colors lerp
         * straight in 8-bit RGB (a zero-length segment reads its first
         * stop). The center is the rect midpoint. Angle math is
         * dual-path (integer octant-fold + 1-degree-LUT binary search
         * under USE_INTEGER_GEOMETRY, atan2 otherwise; agree within
         * 1 degree). Per-pixel draw_pixel like fill_gradient (clip /
         * damage / alpha conventions hold); `radius` rounds the corners
         * the same way. Wireframe degrades to the outline.
         */
        void fill_conic(int x1, int y1, int x2, int y2, int from_deg, const int *stop_deg,
                        const Color *stop_col, int nstops, int radius = 0);

        /*
         * Fill the rect with a three-stop linear ramp (P-2c): from at
         * the start edge, mid at mid_p percent (0..100), to at the far
         * edge; two straight 8-bit RGB segments meeting exactly at mid
         * (degenerate mid_p clamps to the nearer end stop). Direction,
         * radius, and wireframe rules match fill_gradient above.
         */
        void fill_gradient3(int x1, int y1, int x2, int y2, const Color &from, const Color &mid, int mid_p,
                            const Color &to, bool horizontal = true, int radius = 0);

        /*
         * Fill the rect with an N-stop linear ramp (2..8 stops,
         * positions in percent 0..100 non-decreasing): each span lerps
         * straight in 8-bit RGB like fill_gradient above; zero-length
         * spans read flat. Direction, radius, wireframe and
         * binary-alpha rules match fill_gradient above.
         */
        void fill_linear_stops(int x1, int y1, int x2, int y2, const int *stop_pos,
                               const Color *stop_col, int nstops, bool horizontal = true,
                               int radius = 0);

        /*
         * Fill the rect with a repeating stripe texture (P-2c): the
         * stops (2..6 px positions, non-decreasing, last = period > 0)
         * tile every `period` px along columns (horizontal) or rows;
         * hard stops (zero-length segments) read flat; past the last
         * stop the ramp lerps into the next period's first stop. On
         * binary-alpha depths every stop follows the standard binary
         * rule (any nonzero alpha plots). Radius and wireframe rules
         * match fill_gradient above.
         */
        void fill_repeating(int x1, int y1, int x2, int y2, bool horizontal, int period, const int *stop_pos,
                            const Color *stop_col, int nstops, int radius = 0);

        /*
         * Rectangle with circular corners of the given radius (clamped to
         * half the shorter side; <= 0 falls back to the plain rect).
         * draw_round_rect outlines exactly the coverage fill_round_rect
         * fills, one pixel thick. Corner chords are integer math under
         * USE_INTEGER_GEOMETRY (FPU-less targets), like draw_circle's
         * octant bound. Both plot through draw_pixel (A-12/A-13).
         */
        void draw_round_rect(int x1, int y1, int x2, int y2, int radius, const Color &colr);
        void fill_round_rect(int x1, int y1, int x2, int y2, int radius, const Color &colr);

        /*
         * Half-chord of the corner circle (radius `r`) `dy` px from the
         * arc-center row (P-2e): the rounded-rect row-span clip shared
         * by the fills above and the widget inset-shadow bands.
         * Integer-only (isqrt, no FPU); `dy <= 0` reads the full `r`.
         */
        static int corner_chord(int r, int dy);

        /*
         * THE single corner-radius clamp (P-2e): the largest radius
         * that fits a `width` x `height` PIXEL box (the continuous
         * extent is width x height — 50% of a 54px circle is 27). Every
         * rounded primitive and span helper clamps through this one
         * definition: the old inclusive-index form
         * min(right-left, bottom-top)/2 read 53/2 = 26 on even boxes
         * and bulged the outline into a rounded square (the model500
         * knob's diagonal packets). Non-fit input (radius <= 0, empty
         * box) returns 0.
         */
        static int inscribed_radius(int width, int height, int radius);

        /*
         * Area coverage (0..255) of pixel (px, py) inside the rounded
         * rect, as an 8x8 subsample of the integer indicator (squared
         * distance corner test — no sqrt). The single source of boundary
         * anti-aliasing for the fills' corner-arc fringes and the
         * widget's inset-shadow clip: a 1D chord fraction flickers where
         * the arc crosses pixel boundaries row by row, and anything
         * translucent painted over it (the knob's border ring) then
         * reads the page through the gap.
         */
        static int rounded_overlap255(int left, int top, int right, int bottom,
                                      int radius, int px, int py);

        /*
         * Anti-aliased opt-in variants (Batch V-1): Wu's two-pixel
         * coverage split for lines, exact-chord coverage for circles.
         * They ALWAYS blend -- the coverage is the weight -- regardless
         * of the alpha_enabled switch that governs the plain primitives;
         * at 16bpp (binary alpha) coverage quantizes to plot/skip at
         * half coverage and the stroke stays one pixel wide. Endpoints
         * plot solid. Integer-only math (FPU-less targets included);
         * every write goes through plot_aa, so the clip/damage
         * conventions (A-12/A-13) hold. skip_first leaves the start
         * pixel untouched (polyline joints: the previous segment
         * already plotted it — plotting twice would stack a
         * translucent color); the plotted set is unchanged.
         */
        void draw_line_aa(int x1, int y1, int x2, int y2, const Color &colr,
                          bool skip_first = false);
        void draw_circle_aa(int x, int y, int radius, const Color &colr);

        /*
         * Anti-aliased disc fill (AA adoption): the same shape
         * fill_circle fills, but the two edge pixels of every chord span
         * blend by the exact fractional coverage (the draw_circle_aa
         * chord formula), so a filled disc meets its background without
         * stairs. Interior spans stay solid single writes. In wireframe
         * mode degrades to draw_circle_aa (bones stay smooth).
         */
        void fill_circle_aa(int x, int y, int radius, const Color &colr);

        /*
         * Anti-aliased round-rect pair (AA adoption): fill_round_rect_aa
         * keeps the solid middle spans and blends only the fractional
         * corner-chord edges; draw_round_rect_aa joins four draw_line_aa
         * edges (each stopping one pixel short of the corner tangents)
         * with four 90-degree draw_arc_aa corners whose extremes supply
         * the tangent pixels, so no pixel plots twice. In wireframe mode
         * the fill degrades to the AA outline. A non-positive radius
         * falls back to the plain rect, like the aliased pair.
         */
        void draw_round_rect_aa(int x1, int y1, int x2, int y2, int radius,
                                const Color &colr);
        void fill_round_rect_aa(int x1, int y1, int x2, int y2, int radius,
                                const Color &colr);

        /*
         * Anti-aliased rotated rounded-rect fill (H-10): the (x, y, w,
         * h) box with corner radius, rotated angle_deg clockwise about
         * (pivot_x, pivot_y) — the CSS rotate()/transform-origin
         * convention, y down. Integer-only SDF (256-scaled LUT trig,
         * isqrt corner distance); every pixel plots at most once
         * through plot_aa, so translucent colors never stack. A zero
         * angle is the caller's cue to use the plain fill instead.
         */
        void fill_round_rect_rotated(int x, int y, int w, int h, int radius,
                                     int angle_deg, int pivot_x, int pivot_y,
                                     const Color &colr);

        /*
         * Anti-aliased circular arc (Batch V-5). Integer degrees in the
         * math convention: start_deg measured from +x (3 o'clock),
         * positive sweep_deg runs counter-clockwise (on the raster's
         * screen coordinates, y down, that is visually clockwise -- the
         * same convention as SVG/Canvas arcs). sweep == 0 draws nothing;
         * |sweep| >= 360 draws the full circle (degenerates to
         * draw_circle_aa). The arc is a polyline of samples kept within
         * ~1px of each other (the step follows the radius), each segment
         * drawn through draw_line_aa, so endpoints plot solid and every
         * write still goes through plot_aa (V-1), inheriting
         * clip/damage/16bpp.
         * Trig follows USE_INTEGER_GEOMETRY: OFF (desktop) = IEEE float
         * sin/cos; ON (FPU-less targets) = compile-time-generated 1-degree
         * lookup table, no runtime float. Both paths agree on each sample
         * within +-0.5px at radius <= 128 (see code-contract.md).
         */
        void draw_arc_aa(int cx, int cy, int radius, int start_deg, int sweep_deg, const Color &colr);

        void draw_bezier_curve(const impoint_t &p1, const impoint_t &p2, const Color &colr, float accuracy = 0.01);
        void draw_bezier_curve(const impoint_t &p1, const impoint_t &p2, const impoint_t &p3, const Color &colr, float accuracy = 0.01);
        void draw_bezier_curve(const impoint_t &p1, const impoint_t &p2, const impoint_t &p3, const impoint_t &p4, const Color &colr, float accuracy = 0.01);

        /* *
         * Draw an image, a Color pixels array, into graphics.
         * */
        void draw_image(
            const Color *img,          // bitmap buffer pointer
            int img_width,      // bitmap widht
            int img_height,     // bitmap height
            int img_row_stride, // pixel amount in one row
            int start_x,        // x coordinate in the graphic where starting to draw bitmap
            int start_y         // y coordinate in the graphic where starting to draw bitmap
        );

        /* draw an image_t view; row_stride 0 means width, a malformed
           view (null pixels or stride < width) draws nothing */
        void draw_image(
            const image_t &img,
            int start_x,
            int start_y);

        /* *
         * Modulate blit: like draw_image, but every source channel
         * (alpha included) scales by the matching tint channel through
         * the 8-bit-normalized accessors. A tint whose channels all sit
         * at the normalized maximum (255 at 32bpp; the 0xF8 expansion
         * maximum and a set alpha bit at 16bpp) is the 1.0 multiplier
         * and takes the plain draw_image path, so the identity is
         * pixel-exact on every depth. draw_pixel still owns blending:
         * at 32bpp alpha_enabled stacks the tint's alpha over the
         * modulated source alpha; at 16bpp the alpha bit is a binary
         * gate -- a transparent tint clears the source alpha, an opaque
         * one keeps it.
         * */
        void draw_image(
            const Color *img,
            int img_width,
            int img_height,
            int img_row_stride,
            int start_x,
            int start_y,
            const Color &tint);

        void draw_image(
            const image_t &img,
            int start_x,
            int start_y,
            const Color &tint);

        /*
         * WIREFRAME companion (S-1, opt-in): 1px dots every `spacing`
         * pixels across the draw area. Mode-independent (the app calls
         * it when wireframing); spacing <= 0 draws nothing. Plots
         * through draw_pixel, so clip/damage gates apply unchanged.
         */
        void draw_wireframe_grid(int spacing, const Color &colr);

        // the AA primitives' single write path, public: widget paint
        // (shadow masks, rim) is a second consumer beside the fills,
        // through the same gate and blend policy.
        // One pixel with a 0..255 coverage weight (V-1); same
        // offset/bounds/damage gate as draw_pixel, then a
        // coverage-weighted source-over blend
        void plot_aa(int x, int y, int coverage, const Color &colr);

#pragma endregion

    private:
#pragma region private constructors

        Graphics() = default;

#pragma endregion

#pragma region private member variables

        // Graphics::data pixels = nullptr;
        Color *pixels = nullptr;
        bool is_wrapper_mode{};
        bool alpha_enabled{};
        render_mode render_mode_ = render_mode::full;
        bool draw_area_offset_enabled{};
        impoint_t draw_area_offset{};
        imsize_t imsize{};
        imarea_t draw_area{};

        // the culling region (see set_damage); off by default
        bool damage_on_ = false;
        int damage_l_ = 0;
        int damage_t_ = 0;
        int damage_r_ = -1;
        int damage_b_ = -1;

#pragma endregion

#pragma region private methods

        void set_draw_area(int x, int y, int width, int height);

        // single canonical test for the damage clip (A-13): the damage
        // rect is half-open, draw_area is inclusive; every raster write
        // goes through this instead of ad-hoc clamps
        [[nodiscard]] inline bool damage_contains(int x, int y) const
        {
            return x >= damage_l_ && x < damage_r_ && y >= damage_t_ && y < damage_b_;
        }

        void draw_8pixels(int x, int y, int px, int py, const Color &colr);
        void draw_incir_pixels(int x, int y, int px, int py, const Color &colr);
        Color alpha_blend(const Color &front_color, const Color &back_color);

#pragma endregion
    };

    /*
     * Sample the point at integer degree `deg` on the circle
     * (cx, cy, radius), rounded to the pixel grid, into (*ox, *oy).
     * Same convention and two-trig-path policy as draw_arc_aa (0° = +x,
     * positive CCW, visually CW on y-down screens): USE_INTEGER_GEOMETRY
     * OFF uses IEEE float, ON uses the compile-time lookup table; both
     * agree within +-0.5px at radius <= 128. The composition widgets
     * (GaugeDial/Knob ticks, needles, pointers) position geometry with
     * it -- the same integer-degree math, so the needle and the arc
     * always agree at every color depth.
     */
    void point_on_circle(int cx, int cy, int radius, int deg, int *ox, int *oy);
}