#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "widget.hpp"

namespace zb::ui
{
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
     * code-contract §3.3), no fills, no aspect preservation.
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
        void add_text(const Text &t);
        void clear_vectors();
        [[nodiscard]] const std::vector<Line> &lines() const { return lines_; }
        [[nodiscard]] const std::vector<Path> &paths() const { return paths_; }
        [[nodiscard]] const std::vector<Text> &texts() const { return texts_; }

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

        int vb_x_ = 0, vb_y_ = 0, vb_w_ = 0, vb_h_ = 0;
        std::vector<Line> lines_;
        std::vector<Path> paths_;
        std::vector<Text> texts_;
    };
}  // namespace zb::ui
