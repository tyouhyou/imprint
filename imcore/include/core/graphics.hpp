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
         * clip/damage/alpha conventions hold (A-12/A-13).
         */
        void fill_gradient(int x1, int y1, int x2, int y2, const Color &from, const Color &to, bool horizontal = true);

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
         * Anti-aliased opt-in variants (Batch V-1): Wu's two-pixel
         * coverage split for lines, exact-chord coverage for circles.
         * They ALWAYS blend -- the coverage is the weight -- regardless
         * of the alpha_enabled switch that governs the plain primitives;
         * at 16bpp (binary alpha) coverage quantizes to plot/skip at
         * half coverage and the stroke stays one pixel wide. Endpoints
         * plot solid. Integer-only math (FPU-less targets included);
         * every write goes through plot_aa, so the clip/damage
         * conventions (A-12/A-13) hold.
         */
        void draw_line_aa(int x1, int y1, int x2, int y2, const Color &colr);
        void draw_circle_aa(int x, int y, int radius, const Color &colr);

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

        // the AA primitives' single write path: one pixel with a 0..255
        // coverage weight (V-1); same offset/bounds/damage gate as
        // draw_pixel, then a coverage-weighted source-over blend
        void plot_aa(int x, int y, int coverage, const Color &colr);

#pragma endregion
    };
}