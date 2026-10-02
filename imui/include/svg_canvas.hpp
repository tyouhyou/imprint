#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "widget.hpp"

namespace zb::ui
{
    /*
     * Shared path flatteners (H-6): de Casteljau subdivision to a
     * ~0.1 viewBox-unit chord tolerance, depth-capped — the html
     * converter and the DrawPath canvas form both flatten through
     * these (single source, identical output). Plain IEEE double, so
     * every platform flattens alike.
     */
    void svg_flatten_cubic(std::vector<std::pair<double, double>> &out,
                           double x0, double y0, double x1, double y1,
                           double x2, double y2, double x3, double y3,
                           int depth);
    // quadratic -> cubic elevation, one flattener
    void svg_flatten_quad(std::vector<std::pair<double, double>> &out,
                          double x0, double y0, double x1, double y1,
                          double x, double y);

    /*
     * Vector-dial canvas: the HTML `svg`/`vectordial` subset as a
     * display-only widget (docs/html-path.md §SVG subset). It holds
     * viewBox-unit strokes (lines through their device-space capsule
     * geometry; pre-flattened path polylines through the whole-
     * polyline distance field) and baseline texts (through the widget
     * text seam, provider fallback included) and maps the viewBox
     * onto its bounds by stretch.
     *
     * Deliberately narrow: paths arrive already flattened (the html
     * converter owns `d` parsing and de Casteljau subdivision —
     * code-contract §3.2; the DrawPath form below flattens at add
     * time), fills cover the closed static figures
     * only (rect/ellipse/even-odd polygon), no aspect preservation.
     * Display-only like GaugeDial: not focusable, no events,
     * plain-rect hit.
     */
    class SvgCanvas : public Widget
    {
    public:
        struct Line
        {
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // viewBox units
            core::Color color{};                 // alpha carries opacity
            double width = 1.0;                  // viewBox units (0 clips to 1)
            bool round_caps = false;             // stroke-linecap="round"
        };
        /*
         * One stroked subpath as a device-ready polyline (viewBox
         * units, fractional; the `d` grammar was resolved by the html
         * converter). `closed` = the SVG `Z`: the closing segment is
         * appended at draw time, butt caps leave no exposed ends.
         */
        struct Path
        {
            std::vector<std::pair<double, double>> pts;  // viewBox units
            bool closed = false;
            core::Color color{};                 // alpha carries opacity
            double width = 1.0;                  // viewBox units (0 clips to 1)
            bool round_caps = false;             // stroke-linecap="round"
            // a Path strokes by default; the DrawPath front-end clears
            // this for fill-only paths
            bool has_stroke = true;
            // optional fill under the stroke (the static geometry
            // subset: polyline/polygon); SVG paint order = fill first
            bool has_fill = false;
            core::Color fill{};                  // alpha carries opacity
        };
        /*
         * One closed static figure (rect / circle / ellipse). Stored as
         * center + half-extents in viewBox units (fractional): a rect
         * at x/y/width/height maps to cx = x + width/2 etc.; a circle
         * is an ellipse with rx == ry. rx <= 0 || ry <= 0 renders
         * nothing (per SVG). Fill and stroke are independent — SVG
         * default fill = black, stroke = none; the converter decides,
         * the canvas stores what it is given.
         */
        struct Shape
        {
            enum class Kind { rect, ellipse };
            Kind kind = Kind::rect;
            double cx = 0, cy = 0, rx = 0, ry = 0;  // viewBox units
            bool has_fill = false;
            core::Color fill{};                  // alpha carries opacity
            bool has_stroke = false;
            core::Color stroke{};                // alpha carries opacity
            double stroke_width = 1.0;           // viewBox units (0 clips to 1)
        };
        struct Text
        {
            std::u16string text;
            int x = 0, y = 0;  // viewBox units; y is the baseline
            core::Color color{};
            bool has_color = false;  // unset = theme text at draw time
            int anchor = 0;          // 0 = start, 1 = middle, 2 = end
            double font_size = 0.0;  // viewBox units; 0 = seam default
        };

        SvgCanvas() = default;

        // viewBox mapping; w/h <= 0 means pixel units (coordinates map 1:1)
        void set_view_box(int x, int y, int w, int h);
        [[nodiscard]] int view_x() const { return vb_x_; }
        [[nodiscard]] int view_y() const { return vb_y_; }
        [[nodiscard]] int view_w() const { return vb_w_; }
        [[nodiscard]] int view_h() const { return vb_h_; }

        void add_line(const Line &l);
        void add_path(const Path &p);
        void add_shape(const Shape &s);
        void add_text(const Text &t);
        /*
         * DrawCommand path form (H-6): structured commands flattened at
         * add time into per-subpath Path items (same paint style on
         * every subpath; fill is even-odd within one subpath —
         * compound-path holes are out, code-contract §3.2).
         */
        struct PathCmd
        {
            enum class Op { move, line, cubic, quad, close };
            Op op = Op::move;
            // move/line: (x,y); cubic: controls (x1,y1) (x2,y2) -> (x,y);
            // quad: control (x1,y1) -> (x,y); close: unused
            double x1 = 0, y1 = 0, x2 = 0, y2 = 0, x = 0, y = 0;
        };
        struct DrawPath
        {
            std::vector<PathCmd> cmds;
            bool has_fill = false;
            core::Color fill{};                  // alpha carries opacity
            bool has_stroke = false;
            core::Color stroke{};                // alpha carries opacity
            double width = 1.0;                  // viewBox units (0 clips to 1)
            bool round_caps = false;             // stroke-linecap="round"
        };
        void add_draw_path(const DrawPath &p);
        void clear_vectors();
        [[nodiscard]] const std::vector<Line> &lines() const { return lines_; }
        [[nodiscard]] const std::vector<Path> &paths() const { return paths_; }
        [[nodiscard]] const std::vector<Shape> &shapes() const { return shapes_; }
        [[nodiscard]] const std::vector<Text> &texts() const { return texts_; }

        /*
         * Canvas transform (H-6): one affine per canvas, composed
         * translate · rotate(deg, positive = visually clockwise on
         * y-down) · scale, applied after the viewBox stretch
         * (code-contract §3.2). Identity clears the flag; text draws
         * upright at the transformed baseline.
         */
        void set_transform(double tx, double ty, double deg = 0.0,
                           double sx = 1.0, double sy = 1.0);
        void clear_transform();
        [[nodiscard]] bool transform_active() const { return tf_active_; }

        // natural size: the viewBox in pixels, 64x64 without one
        [[nodiscard]] core::imsize_t measure() const override;

    protected:
        void draw_at(core::Graphics &area) const override;

    private:
        [[nodiscard]] int map_x(int vx) const;
        [[nodiscard]] int map_y(int vy) const;
        // fractional viewBox mapping for the stroke geometry (the
        // integer pair truncates; a transformed stroke quad needs the
        // sub-pixel corners)
        [[nodiscard]] double map_fx(double vx) const;
        [[nodiscard]] double map_fy(double vy) const;
        // one stroke through its device-space stroke rectangle (plus
        // round caps and the sub-1.5px hairline fallback)
        void draw_line_stroke(core::Graphics &area, const Line &l) const;
        // one flattened polyline through whole-polyline capsule
        // coverage (2x2 supersampled; joints never double-blend)
        void draw_path_stroke(core::Graphics &area, const Path &p) const;
        // the polyline worker behind draw_path_stroke / the static
        // shape outlines (viewBox-unit points, no allocation per draw)
        void stroke_points(core::Graphics &area,
                           const std::pair<double, double> *pts, size_t n,
                           bool closed, bool round_caps,
                           const core::Color &color, double width) const;
        // the static-shape fill (rect / ellipse / even-odd polygon),
        // 2x2 supersampled like the strokes
        void draw_shape_fill(core::Graphics &area, const Shape &s) const;
        // the static-shape outline closed into a polyline, riding the
        // path stroke machinery (an ellipse flattens to an N-gon)
        void draw_shape_stroke(core::Graphics &area, const Shape &s) const;
        // even-odd fill over device-space Q10 points (the polygon and
        // implicit-closed polyline fill; the closing segment is implied)
        void draw_evenodd_fill(core::Graphics &area,
                               const std::vector<int64_t> &qx,
                               const std::vector<int64_t> &qy,
                               const core::Color &color) const;
        // the shape outline in viewBox units (rect corners / flattened
        // ellipse), shared by the stroke and the rotated-fill fallback
        void build_outline(const Shape &s,
                           std::vector<std::pair<double, double>> &out) const;
        // apply the user affine to device-space doubles (no-op when
        // inactive)
        void apply_tf(double &x, double &y) const;
        // sqrt(|det|) of the user affine in Q10 (isqrt, no libm;
        // kQ10 when inactive)
        [[nodiscard]] int64_t tf_len_q10() const;

        int vb_x_ = 0, vb_y_ = 0, vb_w_ = 0, vb_h_ = 0;
        // user affine T·R·S (device space, applied after the stretch):
        // x' = tf_a_ x + tf_c_ y + tf_e_; y' = tf_b_ x + tf_d_ y + tf_f_
        bool tf_active_ = false;
        double tf_a_ = 1.0, tf_b_ = 0.0, tf_c_ = 0.0;
        double tf_d_ = 1.0, tf_e_ = 0.0, tf_f_ = 0.0;
        std::vector<Line> lines_;
        std::vector<Path> paths_;
        std::vector<Shape> shapes_;
        std::vector<Text> texts_;
    };
}  // namespace zb::ui
