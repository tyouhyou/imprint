#include "annotate_canvas.hpp"

#include "logging.hpp"
#include "theme.hpp"

namespace zb::ui
{
    void AnnotateCanvas::set_canvas_size(const int w, const int h)
    {
        if (w <= 0 || h <= 0)
        {
            LW << "annotate_canvas: non-positive canvas size rejected; "
                  "keeping the previous logical space";
            return;
        }
        if (w == canvas_w && h == canvas_h)
        {
            return;  // fed its current space: no repaint obligation
        }
        canvas_w = w;
        canvas_h = h;
        mark_dirty();
    }

    core::imsize_t AnnotateCanvas::canvas_size() const
    {
        return logical_extent();
    }

    bool AnnotateCanvas::add_stroke(Stroke stroke)
    {
        for (const Stroke &s : strokes_)
        {
            if (s.id == stroke.id)
            {
                LW << "annotate_canvas: duplicate stroke id " << stroke.id
                   << " rejected";
                return false;
            }
        }
        strokes_.push_back(std::move(stroke));
        grow_device_buffer();
        mark_dirty();
        return true;
    }

    bool AnnotateCanvas::remove_stroke(const int id)
    {
        for (auto it = strokes_.begin(); it != strokes_.end(); ++it)
        {
            if (it->id == id)
            {
                strokes_.erase(it);
                mark_dirty();
                return true;
            }
        }
        return false;
    }

    void AnnotateCanvas::clear_strokes()
    {
        if (strokes_.empty())
        {
            return;
        }
        strokes_.clear();
        strokes_.shrink_to_fit();
        mark_dirty();
    }

    void AnnotateCanvas::set_annotate_enabled(const bool enabled)
    {
        if (enabled == annotate_enabled_)
        {
            return;
        }
        annotate_enabled_ = enabled;
        mark_dirty();
    }

    void AnnotateCanvas::set_stroke_color(const core::Color &c)
    {
        stroke_color_ = c;
        mark_dirty();
    }

    void AnnotateCanvas::set_stroke_width(const int w)
    {
        if (w == stroke_width_)
        {
            return;
        }
        stroke_width_ = w;
        mark_dirty();
    }

    core::Color AnnotateCanvas::stroke_color() const
    {
        return stroke_color_.value_or(theme().text);
    }

    core::imsize_t AnnotateCanvas::logical_extent() const
    {
        if (canvas_w > 0 && canvas_h > 0)
        {
            return {canvas_w, canvas_h};
        }
        return measure();  // the default: the image extent (0x0 empty)
    }

    std::optional<std::pair<int, int>> AnnotateCanvas::device_to_logical(
        const int64_t dx, const int64_t dy) const
    {
        const letterbox_rect box = image_letterbox();
        const core::imsize_t logical = logical_extent();
        if (box.w <= 0 || box.h <= 0 || logical.width <= 0 ||
            logical.height <= 0)
        {
            return std::nullopt;  // no canvas: nothing maps
        }
        // the exact inverse of the logical -> device map, then clamped
        // into the logical rect: a stroke cannot escape the canvas
        const auto clamp_axis = [](const int64_t v, const int64_t hi)
        {
            if (v < 0)
            {
                return 0;
            }
            if (v > hi)
            {
                return static_cast<int>(hi);
            }
            return static_cast<int>(v);
        };
        const int lx = clamp_axis(
            (static_cast<int64_t>(dx - box.x) * logical.width) / box.w,
            logical.width - 1);
        const int ly = clamp_axis(
            (static_cast<int64_t>(dy - box.y) * logical.height) / box.h,
            logical.height - 1);
        return std::make_pair(lx, ly);
    }

    bool AnnotateCanvas::map_stroke_points(const Stroke &s) const
    {
        const letterbox_rect box = image_letterbox();
        const core::imsize_t logical = logical_extent();
        if (box.w <= 0 || box.h <= 0 || logical.width <= 0 ||
            logical.height <= 0)
        {
            return false;
        }
        dev_pts_.clear();
        for (const auto &p : s.points)
        {
            dev_pts_.push_back(core::impoint_t{
                box.x + static_cast<int>(
                            static_cast<int64_t>(p.first) * box.w /
                            logical.width),
                box.y + static_cast<int>(
                            static_cast<int64_t>(p.second) * box.h /
                            logical.height)});
        }
        return true;
    }

    void AnnotateCanvas::draw_one_stroke(core::Graphics &area,
                                         const Stroke &s) const
    {
        if (s.points.size() < 2)
        {
            return;  // a lone sample has no band (the polyline rule)
        }
        if (!map_stroke_points(s))
        {
            return;
        }
        area.draw_polyline_aa(dev_pts_.data(),
                              static_cast<int>(dev_pts_.size()), s.width,
                              s.color);
    }

    void AnnotateCanvas::draw_at(core::Graphics &area) const
    {
        // the background: the inherited ImageView draw (the letterboxed
        // image the ink sits on)
        ImageView::draw_at(area);

        // strokes in insertion order, the in-progress stroke last; the
        // device points refill the reserved buffer in place
        for (const Stroke &s : strokes_)
        {
            draw_one_stroke(area, s);
        }
        if (capturing_)
        {
            draw_one_stroke(area, in_progress_);
        }
    }

    void AnnotateCanvas::grow_device_buffer()
    {
        size_t total = in_progress_.points.size();
        for (const Stroke &s : strokes_)
        {
            total += s.points.size();
        }
        if (dev_pts_.capacity() < total)
        {
            dev_pts_.reserve(total);
        }
    }

    int AnnotateCanvas::take_capture_id()
    {
        const auto taken = [this](const int id)
        {
            for (const Stroke &s : strokes_)
            {
                if (s.id == id)
                {
                    return true;
                }
            }
            return false;
        };
        while (taken(next_capture_id))
        {
            ++next_capture_id;
        }
        return next_capture_id++;
    }

    bool AnnotateCanvas::on_input(const zb::input::input_event &ev)
    {
        if (!annotate_enabled_)
        {
            return false;
        }
        switch (ev.type)
        {
        case zb::input::input_type::mouse_left_down:
        case zb::input::input_type::touch_down:
        {
            // a press starts an in-progress stroke; without a logical
            // space and an image rect nothing maps (no canvas, no capture)
            const core::impoint_t pos = get_absolute_position();
            const auto p = device_to_logical(
                static_cast<int64_t>(ev.x) - pos.x,
                static_cast<int64_t>(ev.y) - pos.y);
            if (!p)
            {
                return false;
            }
            capturing_ = true;
            in_progress_ = Stroke{};
            in_progress_.id = take_capture_id();
            in_progress_.color = stroke_color();
            in_progress_.width = stroke_width_;
            in_progress_.points.push_back(*p);
            grow_device_buffer();
            mark_dirty();
            return true;  // the press is claimed either way (Slider pattern)
        }
        case zb::input::input_type::mouse_move:
        case zb::input::input_type::touch_move:
        {
            if (!capturing_)
            {
                return false;
            }
            const core::impoint_t pos = get_absolute_position();
            const auto p = device_to_logical(
                static_cast<int64_t>(ev.x) - pos.x,
                static_cast<int64_t>(ev.y) - pos.y);
            if (!p || p == in_progress_.points.back())
            {
                return false;  // same-point samples dropped
            }
            in_progress_.points.push_back(*p);
            grow_device_buffer();
            mark_dirty();
            return true;  // a change: repaint gate
        }
        case zb::input::input_type::mouse_left_up:
        case zb::input::input_type::touch_up:
        {
            if (!capturing_)
            {
                return false;
            }
            capturing_ = false;
            Stroke committed = std::move(in_progress_);
            in_progress_ = Stroke{};
            strokes_.push_back(committed);
            grow_device_buffer();
            mark_dirty();
            // a stable copy: a handler may mutate the stroke list
            stroke_ended(committed);
            return true;
        }
        default:
            return false;
        }
    }

    void AnnotateCanvas::on_cancel()
    {
        if (!capturing_)
        {
            return;
        }
        capturing_ = false;
        in_progress_ = Stroke{};
        mark_dirty();  // the in-progress ink disappears
    }
}
