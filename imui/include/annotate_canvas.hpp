#pragma once

#include "event.hpp"
#include "image_view.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace zb::ui
{
    /*
     * Freehand annotation canvas over an ImageView (TJ-NEW, code-contract
     * §3.6).
     *
     * The widget accepts freehand strokes in a fixed logical-canvas
     * coordinate space and presents them through any letterbox: drawing
     * maps logical -> device through the inherited ImageView letterbox
     * (the same scale/offset the background image uses -- the ink sits
     * exactly on the image), input maps device -> logical through the
     * exact inverse, and samples outside the mapped image rect clamp
     * into the logical rect (a stroke cannot escape the canvas).
     *
     * The stroke model is data: the host relays remote annotations with
     * add_stroke (explicit id; a duplicate id is one LW and false),
     * removes with remove_stroke / clear_strokes, queries with strokes().
     * Captured strokes take monotonically increasing ids from the same
     * space (an id an injected stroke already took is skipped past);
     * stroke_ended fires once per completed capture (by value -- the
     * TextArea changed precedent), the I/F relay point. Undo is
     * host-side over the removal ops (no widget undo stack).
     *
     * Input follows the Slider capture pattern: with
     * set_annotate_enabled(true) the canvas claims the pointer -- a
     * press starts an in-progress stroke, moves extend it (same-point
     * samples dropped), release commits it and fires stroke_ended,
     * on_cancel discards the in-progress stroke. Disabled (the default)
     * the widget never captures and the strokes still draw -- a
     * read-only stage.
     *
     * Rendering: the background is the inherited ImageView draw; strokes
     * render in insertion order through Graphics::draw_polyline_aa, the
     * in-progress stroke last. Same stroke set + same canvas box + same
     * image -> same pixels. draw_at refills a member buffer that the
     * mutation paths keep reserved, so the frame path allocates nothing.
     *
     * Programmatic-only by design: no .ui/HTML tag; the first consumer
     * (the tianji client, Batch FI Part 2) builds it in C++.
     */
    class AnnotateCanvas : public ImageView
    {
    public:
        struct Stroke
        {
            int id = 0;
            core::Color color{};
            int width = 2;
            std::vector<std::pair<int, int>> points;  // logical coordinates
        };

        AnnotateCanvas() = default;

        /* names the logical space (positive; the default follows the
           image extent). A non-positive size is one LW, no change. */
        void set_canvas_size(int w, int h);
        [[nodiscard]] core::imsize_t canvas_size() const;

        /* injects a remote annotation in insertion order (true); a
           duplicate id is one LW and false */
        bool add_stroke(Stroke stroke);
        bool remove_stroke(int id);
        void clear_strokes();
        [[nodiscard]] const std::vector<Stroke> &strokes() const
        {
            return strokes_;
        }

        /* fires once per completed capture, with the committed stroke */
        zb::event::Event<Stroke> stroke_ended;

        /* the Slider capture switch: enabled the canvas claims the
           pointer and captures strokes, disabled it never captures */
        void set_annotate_enabled(bool enabled);
        [[nodiscard]] bool annotate_enabled() const
        {
            return annotate_enabled_;
        }

        /* the captured-stroke defaults: theme text color and width 2,
           until set */
        void set_stroke_color(const core::Color &c);
        void set_stroke_width(int w);
        [[nodiscard]] core::Color stroke_color() const;
        [[nodiscard]] int stroke_width() const { return stroke_width_; }

    protected:
        void draw_at(core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        void on_cancel() override;
        [[nodiscard]] bool captures_pointer() const override
        {
            return annotate_enabled_;
        }

    private:
        /* the effective logical space: the explicit canvas size, else
           the image extent (0x0 while empty) */
        [[nodiscard]] core::imsize_t logical_extent() const;

        /* device -> logical through the letterbox's exact inverse;
           nullopt when nothing maps, samples outside the mapped image
           rect clamped into the logical rect. int64 in: the device
           coordinates come from the C-ABI event and the difference to
           the widget origin must not overflow. */
        [[nodiscard]] std::optional<std::pair<int, int>> device_to_logical(
            int64_t dx, int64_t dy) const;

        /* refills dev_pts_ with one stroke's device points (false when
           nothing maps: no image rect or no logical space). Injected
           points are host-forwarded and untrusted: they clamp into the
           logical rect first, so a remote stroke cannot escape the
           canvas and the device narrowing stays in range. */
        bool map_stroke_points(const Stroke &s) const;

        /* rasterizes one stroke through draw_polyline_aa */
        void draw_one_stroke(core::Graphics &area, const Stroke &s) const;

        /* mutation path: keep dev_pts_ able to hold the current point
           total, so draw_at only refills it in place */
        void grow_device_buffer();

        /* the next captured-stroke id: monotonic, bumped past any id an
           injected stroke already took (one id space, no collisions) */
        int take_capture_id();

        int canvas_w = 0;  // 0 = follow the image extent
        int canvas_h = 0;
        std::vector<Stroke> strokes_;
        int next_capture_id = 1;

        bool annotate_enabled_ = false;
        std::optional<core::Color> stroke_color_;  // unset = theme text
        int stroke_width_ = 2;

        bool capturing_ = false;
        Stroke in_progress_;

        // device-space render scratch of one draw_at pass (const draw
        // path refills it in place; the mutation paths keep it reserved)
        mutable std::vector<core::impoint_t> dev_pts_;
    };
}
