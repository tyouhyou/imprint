#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "imcore.hpp"
#include "input.hpp"
#include "theme.hpp"

#include "text/bitmap_provider.hpp"

namespace zb::ui
{
    /*
     * Base class of a widget.
     *
     * Geometry:
     *   - position and size are relative to the parent widget (or to the
     *     window for the root widget)
     *   - draw() clips to the widget's own area and calls draw_at() with a
     *     Graphics whose origin is the widget's top-left corner; drawing
     *     outside the area is clipped away
     *   - hit() tests a point given in the widget's local coordinates
     *
     * Background: optional color and/or image (the image wins over the
     * color). Drawn by the base class before draw_at().
     *
     * Text: always rendered (UTF-8 input, UTF-16 storage). The primary
     * glyph provider is the one assigned via set_glyph_provider() or the
     * process-wide default (set_default_glyph_provider); code units the
     * primary provider does not cover fall back to the built-in 5x7
     * bitmap glyphs, and code units nothing covers are skipped (see
     * docs/code-contract.md section 2.4). draw_at() defaults to drawing
     * the text, so a widget that just shows background and text needs no
     * override (Label).
     *
     * A widget is a leaf primitive. Widgets that hold children (containers)
     * keep the child list themselves and render children inside draw_at()
     * by calling child->draw().
     */
    class Widget
    {
        friend class InputDispatcher;
        friend class Panel;
        friend class FlexPanel;
        friend class Dialog;

    public:
        enum class h_align
        {
            left,
            center,
            right
        };

        enum class v_align
        {
            top,
            center,
            bottom
        };

        Widget();
        virtual ~Widget() = default;

        Widget(const Widget &) = delete;
        Widget &operator=(const Widget &) = delete;

        void set_position(const core::impoint_t &p)
        {
            mark_dirty();     // old area
            position = p;
            mark_dirty();     // new area
        }
        void set_position(const int x, const int y)
        {
            mark_dirty();
            position = {x, y};
            mark_dirty();
        }
        [[nodiscard]] core::impoint_t get_position() const { return position; }

        void set_size(const core::imsize_t &s)
        {
            mark_dirty();
            size = s;
            size_explicit_w_ = true;
            size_explicit_h_ = true;
            width_percent_ = 0;
            height_percent_ = 0;
            mark_dirty();
            mark_layout_dirty();
        }
        void set_size(const int w, const int h)
        {
            mark_dirty();
            size = {w, h};
            size_explicit_w_ = true;
            size_explicit_h_ = true;
            width_percent_ = 0;
            height_percent_ = 0;
            mark_dirty();
            mark_layout_dirty();
        }
        [[nodiscard]] core::imsize_t get_size() const { return size; }

        /*
         * Natural (content-derived) size of the widget, used by flex
         * layouts for children that were never explicitly sized
         * (set_size). The default returns the current size; widgets with
         * intrinsic geometry (Label, Checkbox, Slider, ListBox, ...)
         * override it.
         */
        [[nodiscard]] virtual core::imsize_t measure() const { return size; }

        /*
         * Inset of the content box from the border box (P-3): containers
         * with padding override (FlexPanel/Panel return padding);
         * the abs resolver reads the anchor's content box through it.
         */
        [[nodiscard]] virtual int content_inset() const { return 0; }

        // layout-driven resize: does not mark the size as explicit, so a
        // flex-assigned size never overrides the widget's measure()
        void set_size_auto(const int w, const int h)
        {
            mark_dirty();
            size = {w, h};
            size_explicit_w_ = false;
            size_explicit_h_ = false;
            mark_dirty();
            // the layout that writes this size re-lays all children in
            // this pass, so the widget itself needs no invalidation; its
            // ancestors' layout can depend on the size, so they do
            mark_layout_dirty(false);
        }
        // per-axis variants for flex layouts: growing a child along the
        // main axis must not discard an explicit cross-axis size (the
        // two-axis set_size_auto clears both flags)
        void set_width_auto(const int w)
        {
            mark_dirty();
            size.width = w;
            size_explicit_w_ = false;
            mark_dirty();
            mark_layout_dirty(false);
        }
        void set_height_auto(const int h)
        {
            mark_dirty();
            size.height = h;
            size_explicit_h_ = false;
            mark_dirty();
            mark_layout_dirty(false);
        }
        /*
         * Percentage size (batch L-4): declares the axis as a percentage
         * of the FlexPanel parent's content box. The parent resolves it
         * during its layout() and writes the result through the per-axis
         * auto setters, so the axis never becomes explicit and every
         * re-layout re-resolves it (resizing the parent resizes the
         * child). The declaration replaces explicitness on the axis;
         * set_size clears both declarations -- the last geometry setter
         * on an axis wins. pct clamps into 1..100; 0 (or negative)
         * clears the declaration. Outside a FlexPanel (Panel children,
         * the tree root) the declaration stays unresolved: the axis
         * keeps its current size (docs/code-contract.md 3).
         */
        void set_width_percent(const int pct)
        {
            width_percent_ = percent_clamp(pct);
            if (width_percent_ != 0)
            {
                size_explicit_w_ = false;  // the declaration replaces
                                           // explicitness (contract §3)
            }
            mark_layout_dirty();
        }
        void set_height_percent(const int pct)
        {
            height_percent_ = percent_clamp(pct);
            if (height_percent_ != 0)
            {
                size_explicit_h_ = false;  // the declaration replaces
                                           // explicitness (contract §3)
            }
            mark_layout_dirty();
        }
        [[nodiscard]] bool is_width_percent() const { return width_percent_ != 0; }
        [[nodiscard]] bool is_height_percent() const { return height_percent_ != 0; }
        // the declared percent (0 = the axis is not a percentage)
        [[nodiscard]] int width_percent() const { return width_percent_; }
        [[nodiscard]] int height_percent() const { return height_percent_; }
        [[nodiscard]] virtual bool is_size_explicit() const { return size_explicit_w_ || size_explicit_h_; }
        [[nodiscard]] bool is_width_explicit() const { return size_explicit_w_; }
        [[nodiscard]] bool is_height_explicit() const { return size_explicit_h_; }

        /*
         * Aspect ratio (H-5): when set (w,h > 0), a FlexPanel derives the
         * child's open main axis from its settled cross axis
         * (width = height * w/h in a row, height = width * h/w in a
         * column) -- but only while that axis is auto with no percent
         * declaration and the cross axis is explicit or percent-settled.
         * Explicit sizes and percent declarations always win; without a
         * settled cross axis the widget keeps its measured size.
         */
        void set_aspect_ratio(const int w, const int h)
        {
            mark_dirty();
            if (w > 0 && h > 0)
            {
                aspect_w_ = w > 65535 ? uint16_t{65535} : static_cast<uint16_t>(w);
                aspect_h_ = h > 65535 ? uint16_t{65535} : static_cast<uint16_t>(h);
            }
            else
            {
                aspect_w_ = 0;
                aspect_h_ = 0;
            }
            mark_dirty();
            mark_layout_dirty();
        }
        [[nodiscard]] bool has_aspect() const { return aspect_w_ > 0 && aspect_h_ > 0; }

        // sidecar sections (defined up here: the public setters below
        // name these types; storage + mutators stay private at the
        // bottom). One heap sidecar per dressed widget (batch J).
        // positioning spec (P-3): offsets l/t/r/b then translate
        // x/y, INT16_MIN = unset, pctmask bits l/t/r/b/tx/ty
        struct abs_spec
        {
            int16_t off[4] = {INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN};
            int16_t tr[2] = {INT16_MIN, INT16_MIN};
            uint8_t kind = 0;  // 1 relative, 2 absolute
            uint8_t pctmask = 0;
        };
        // extended gradient (P-2b/c): forms the packed paint_dress
        // cannot hold. kind 3 = conic (a = from deg, b = nstops,
        // pos/col = stops); kind 5 = three-stop linear (col =
        // from/mid/to, a = mid %, flags bit0 = horizontal)
        struct grad_ex
        {
            uint8_t kind = 0;
            uint8_t flags = 0;
            uint16_t a = 0;
            uint16_t b = 0;
            uint16_t pos[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            core::Color col[8]{};
        };
        // repeating stripe overlay (P-2c): up to 6 px stops, period =
        // last stop; paints translucently over any base
        struct rep_ex
        {
            uint8_t flags = 0;  // bit0 = horizontal
            uint8_t n = 0;
            uint16_t period = 0;
            uint16_t pos[6] = {0, 0, 0, 0, 0, 0};
            core::Color col[6]{};
        };
        // top border band (P-2d): width px + color (alpha 0 = none)
        struct bord_t
        {
            uint8_t w = 0;
            core::Color c{};
        };
        // one box shadow (P-2e): px offset, blur/spread px, color.
        // Inset paints inner bands (full effect); outer paints its
        // silhouette under the box (clip-bound — see the contract).
        struct shadow_spec
        {
            int8_t ox = 0;
            int8_t oy = 0;
            uint8_t blur = 0;
            uint8_t spread = 0;
            core::Color c{};
        };

        /*
         * Positioning (P-3): relative lays out in flow and anchors abs
         * descendants; absolute leaves the flow (resolved by the
         * FlexPanel against the containing block). Offsets are px
         * (INT16_MIN = unset) or % of the containing block; translate
         * shifts after placement (% of self). Storage is the heap
         * sidecar below, allocated only for dressed widgets, so the
         * bare Widget stays allocation-free and lean (batch J).
         */
        void set_relative()
        {
            mut_pos()->kind = 1;
            mark_layout_dirty();
        }
        void set_absolute()
        {
            mut_pos()->kind = 2;
            mark_layout_dirty();
        }
        // side: 0 left, 1 top, 2 right, 3 bottom; translate: 0 x, 1 y
        void set_abs_offset(const int side, const int v, const bool pct)
        {
            if (side < 0 || side > 3)
            {
                return;
            }
            abs_spec *const p = mut_pos();
            int c = v < -32767 ? -32767 : (v > 32767 ? 32767 : v);
            if (pct)
            {
                c = c < 0 ? 0 : (c > 100 ? 100 : c);
            }
            p->off[side] = static_cast<int16_t>(c);
            if (pct)
            {
                p->pctmask |= static_cast<uint8_t>(1U << side);
            }
            else
            {
                p->pctmask &= static_cast<uint8_t>(~(1U << side));
            }
            mark_layout_dirty();
        }
        void set_translate(const int axis, const int v, const bool pct)
        {
            if (axis < 0 || axis > 1)
            {
                return;
            }
            abs_spec *const p = mut_pos();
            int c = v < -32767 ? -32767 : (v > 32767 ? 32767 : v);
            if (pct)
            {
                c = c < -100 ? -100 : (c > 100 ? 100 : c);
            }
            p->tr[axis] = static_cast<int16_t>(c);
            if (pct)
            {
                p->pctmask |= static_cast<uint8_t>(1U << (4 + axis));
            }
            else
            {
                p->pctmask &= static_cast<uint8_t>(~(1U << (4 + axis)));
            }
            mark_layout_dirty();
        }
        [[nodiscard]] bool is_positioned() const
        {
            const abs_spec *const p = pos();
            return p != nullptr && p->kind != 0;
        }
        [[nodiscard]] bool is_absolute() const
        {
            const abs_spec *const p = pos();
            return p != nullptr && p->kind == 2;
        }
        // nearest positioned ancestor (relative or absolute) or null;
        // the FlexPanel resolver falls back to the direct parent box
        [[nodiscard]] const Widget *positioned_ancestor() const
        {
            for (const Widget *a = parent; a != nullptr; a = a->parent)
            {
                if (a->is_positioned())
                {
                    return a;
                }
            }
            return nullptr;
        }
        // abs-spec readers for the FlexPanel resolver (INT16_MIN = unset;
        // pct bit per side in mask order l/t/r/b/tx/ty)
        [[nodiscard]] int16_t abs_off(const int side) const
        {
            const abs_spec *const p = pos();
            return (p != nullptr && side >= 0 && side <= 3) ? p->off[side]
                                                           : INT16_MIN;
        }
        [[nodiscard]] bool abs_off_pct(const int side) const
        {
            const abs_spec *const p = pos();
            return p != nullptr && side >= 0 && side <= 3 &&
                   (p->pctmask & (1U << side)) != 0;
        }
        [[nodiscard]] int16_t abs_tr(const int axis) const
        {
            const abs_spec *const p = pos();
            return (p != nullptr && axis >= 0 && axis <= 1) ? p->tr[axis]
                                                           : INT16_MIN;
        }
        [[nodiscard]] bool abs_tr_pct(const int axis) const
        {
            const abs_spec *const p = pos();
            return p != nullptr && axis >= 0 && axis <= 1 &&
                   (p->pctmask & (1U << (4 + axis))) != 0;
        }
        [[nodiscard]] int aspect_w() const { return aspect_w_; }
        [[nodiscard]] int aspect_h() const { return aspect_h_; }

        // in-flow margins, top/right/bottom/left (H-3, contract §flex):
        // heap sidecar, bare widgets read 0 without allocating; the
        // FlexPanel/Panel flow counts them around the border box
        // (never collapsed, never shrunk); absolutely positioned
        // children ignore them
        void set_margin(const int t, const int r, const int b, const int l)
        {
            const auto cl = [](const int v) {
                return static_cast<int16_t>(v < 0 ? 0 : (v > 32767 ? 32767 : v));
            };
            const int16_t m[4] = {cl(t), cl(r), cl(b), cl(l)};
            if (m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == 0 &&
                (ext_ == nullptr || ext_->has_margin == 0))
            {
                return;  // all-zero without storage: stay allocation-free
            }
            ensure_ext();
            ext_->has_margin = 1;
            ext_->margin[0] = m[0];
            ext_->margin[1] = m[1];
            ext_->margin[2] = m[2];
            ext_->margin[3] = m[3];
            mark_layout_dirty();
        }
        [[nodiscard]] int margin_top() const
        {
            return (ext_ != nullptr && ext_->has_margin != 0) ? ext_->margin[0] : 0;
        }
        [[nodiscard]] int margin_right() const
        {
            return (ext_ != nullptr && ext_->has_margin != 0) ? ext_->margin[1] : 0;
        }
        [[nodiscard]] int margin_bottom() const
        {
            return (ext_ != nullptr && ext_->has_margin != 0) ? ext_->margin[2] : 0;
        }
        [[nodiscard]] int margin_left() const
        {
            return (ext_ != nullptr && ext_->has_margin != 0) ? ext_->margin[3] : 0;
        }

        void set_visible(const bool v)
        {
            mark_dirty();
            visible = v;
        }
        [[nodiscard]] bool is_visible() const { return visible; }

        // visible and every ancestor visible: the state keyboard focus
        // and active presses must respect (a widget inside a hidden
        // dialog is not effectively visible even though its own flag
        // is still set)
        [[nodiscard]] bool is_effectively_visible() const
        {
            for (const Widget *w = this; w != nullptr; w = w->parent)
            {
                if (!w->is_visible())
                {
                    return false;
                }
            }
            return true;
        }

        /*
         * Identity for host/designer-side references and event wiring.
         * Optional: ids must be unique within the widget tree that uses
         * them; the framework itself never depends on them.
         */
        void set_id(const char *id) { id_ = id; }
        void set_id(const std::string &id) { id_ = id; }
        [[nodiscard]] const std::string &get_id() const { return id_; }

        /*
         * Damage reporting (region invalidation, see docs/code-contract.md
         * section 3.2). Every state setter marks the widget automatically,
         * and the layout containers mark themselves, so application code
         * never calls these. Widget authors call mark_dirty() from their
         * own state setters (the same obligation as implementing draw_at).
         *
         * The no-argument form reports the widget's own bounds; the rect
         * form reports an absolute-screen rectangle (the widget still has
         * to be within the tree -- damage is collected at paint time by
         * walking the tree, never through the pointer).
         *
         * A widget with a zero-size area reports nothing (no visible
         * damage); a repaint requested while not a single widget reported
         * damage falls back to a full-frame repaint.
         */
        void mark_dirty() { mark_dirty_rect(get_absolute_position().x, get_absolute_position().y, size.width, size.height); }
        void mark_dirty(const int x, const int y, const int w, const int h) { mark_dirty_rect(x, y, w, h); }
        [[nodiscard]] bool is_dirty() const { return dirty_; }
        // this widget or any descendant reported damage since the last
        // paint (the window's repaint-owed source, see CanvasWindow)
        [[nodiscard]] bool is_subtree_dirty() const { return subtree_dirty_; }
        // aggregate reads the damage rect and clears it in one pass
        void take_dirty(int *out_l, int *out_t, int *out_r, int *out_b)
        {
            if (dirty_)
            {
                *out_l = std::min(*out_l, dirty_l_);
                *out_t = std::min(*out_t, dirty_t_);
                *out_r = std::max(*out_r, dirty_r_);
                *out_b = std::max(*out_b, dirty_b_);
            }
        }
        void clear_dirty() { dirty_ = false; }

        /*
         * Damage walk: aggregates this widget's and every descendant's
         * reported rect into the union given in (l, t, r, b), consuming
         * each rect as it is read, then recomputes the subtree-pending
         * flag bottom-up. Damage reported after this walk (e.g. by a
         * setter firing during the draw) survives into the next frame.
         * Called once by the window at paint time.
         */
        void walk_damage(int *out_l, int *out_t, int *out_r, int *out_b);
        // resets every reported rect and pending flag in the subtree
        void walk_clear_damage();

        // background
        void set_background_color(const core::Color &c)
        {
            background = c;
            mark_dirty();
        }
        void set_background_image(const core::image_t &img)
        {
            background_image = img;
            mark_dirty();
        }
        // linear-dressing background (P-1): interpolates from->to along
        // the width (horizontal) or height; coexists with the solid
        // (painted over it) and the image (painted over both)
        void set_background_linear(const core::Color &from, const core::Color &to,
                                   const bool horizontal)
        {
            dress_.bg_kind = 1;
            dress_.bg_from = from;
            dress_.bg_to = to;
            dress_.bg_ax = horizontal ? 1 : 0;
            mark_dirty();
        }
        // circular-dressing background (P-1): from at (cx_pct, cy_pct)
        // of the box, to at the farthest corner; stop offsets 0..100
        // rescale the ramp (outside clamps to the end stops)
        void set_background_radial(const int cx_pct, const int cy_pct,
                                   const core::Color &from, const int from_pos,
                                   const core::Color &to, const int to_pos)
        {
            auto clamp100 = [](const int v) {
                return v < 0 ? 0 : (v > 100 ? 100 : v);
            };
            dress_.bg_kind = 2;
            dress_.bg_from = from;
            dress_.bg_to = to;
            dress_.bg_ax = static_cast<uint8_t>(clamp100(cx_pct));
            dress_.bg_ay = static_cast<uint8_t>(clamp100(cy_pct));
            dress_.bg_p0 = static_cast<uint8_t>(clamp100(from_pos));
            dress_.bg_p1 = static_cast<uint8_t>(clamp100(to_pos));
            mark_dirty();
        }
        // 1px+ outline painted over the background, inside the box
        // (border-box); only solid is honored
        void set_border(const int width_px, const core::Color &c)
        {
            dress_.border_w =
                width_px < 0 ? 0 : (width_px > 255 ? 255 : width_px);
            dress_.border_color = c;
            mark_dirty();
        }
        // rounded background/border corners (P-1): px clamps to half the
        // smaller side at draw time; the half form resolves 50% then
        void set_corner_radius(const int px)
        {
            dress_.radius_kind = 1;
            dress_.radius_px =
                static_cast<uint16_t>(px < 0 ? 0 : (px > 65535 ? 65535 : px));
            mark_dirty();
        }
        void set_corner_radius_half()
        {
            dress_.radius_kind = 2;
            mark_dirty();
        }
        [[nodiscard]] bool has_background() const
        {
            return background.has_value() || background_image.has_value() ||
                   dress_.bg_kind != 0 || grad() != nullptr ||
                   rep() != nullptr;
        }
        [[nodiscard]] bool has_border() const { return dress_.border_w > 0; }
        // top border band (P-2d): full-width strip over the
        // background; a zero width or alpha-0 color paints nothing
        void set_top_border(const int width_px, const core::Color &c)
        {
            bord_t *const b = mut_bord();
            b->w = static_cast<uint8_t>(width_px < 0 ? 0
                                          : (width_px > 255 ? 255 : width_px));
            b->c = c;
            mark_dirty();
        }
        [[nodiscard]] bool has_top_border() const
        {
            return ext_ != nullptr && ext_->has_bord != 0 &&
                   ext_->bord.w != 0 && ext_->bord.c.a() != 0;
        }
        // band geometry for the draw path (null = none)
        [[nodiscard]] const bord_t *top_border() const
        {
            return has_top_border() ? &ext_->bord : nullptr;
        }
        // box shadows (P-2e): incremental init-path setters, first two
        // per kind win, further calls are ignored. Lengths clamp to
        // int8, blur/spread to uint8
        void add_shadow_outer(const int ox, const int oy, const int blur,
                              const int spread, const core::Color &c)
        {
            if (ext_ != nullptr && ext_->n_sh_out >= 2)
            {
                return;
            }
            ensure_ext();
            shadow_spec *const s = &ext_->sh_out[ext_->n_sh_out++];
            s->ox = static_cast<int8_t>(ox < -128 ? -128 : (ox > 127 ? 127 : ox));
            s->oy = static_cast<int8_t>(oy < -128 ? -128 : (oy > 127 ? 127 : oy));
            s->blur = static_cast<uint8_t>(blur < 0 ? 0 : (blur > 255 ? 255 : blur));
            s->spread =
                static_cast<uint8_t>(spread < 0 ? 0 : (spread > 255 ? 255 : spread));
            s->c = c;
            mark_dirty();
        }
        void add_shadow_inset(const int ox, const int oy, const int blur,
                              const int spread, const core::Color &c)
        {
            if (ext_ != nullptr && ext_->n_sh_in >= 2)
            {
                return;
            }
            ensure_ext();
            shadow_spec *const s = &ext_->sh_in[ext_->n_sh_in++];
            s->ox = static_cast<int8_t>(ox < -128 ? -128 : (ox > 127 ? 127 : ox));
            s->oy = static_cast<int8_t>(oy < -128 ? -128 : (oy > 127 ? 127 : oy));
            s->blur = static_cast<uint8_t>(blur < 0 ? 0 : (blur > 255 ? 255 : blur));
            s->spread =
                static_cast<uint8_t>(spread < 0 ? 0 : (spread > 255 ? 255 : spread));
            s->c = c;
            mark_dirty();
        }
        // conic-dressing background (P-2b): `from_deg` start angle (CSS
        // degrees), 2..4 {deg, color} stops (center fixed at 50%/50%).
        // Rides the sidecar (flags its section); overrides every
        // paint_dress gradient at draw time
        void set_background_conic(const int from_deg, const int *stop_deg,
                                  const core::Color *stop_col, const int nstops)
        {
            if (stop_deg == nullptr || stop_col == nullptr || nstops < 2 ||
                nstops > 4)
            {
                return;
            }
            grad_ex *const g = mut_grad();
            g->kind = 3;
            g->a = static_cast<uint16_t>(from_deg);
            g->b = static_cast<uint16_t>(nstops);
            for (int i = 0; i < nstops; ++i)
            {
                const int d = stop_deg[i] < 0 ? 0 : stop_deg[i];
                g->pos[i] = static_cast<uint16_t>(d > 360 ? 360 : d);
                g->col[i] = stop_col[i];
            }
            mark_dirty();
        }
        // three-stop linear (P-2c): full from/mid/to + mid % ride the
        // sidecar (kind 5); horizontal in flags bit0. Replaces any
        // paint_dress gradient at draw time
        void set_background_linear3(const core::Color &from,
                                    const core::Color &mid, const int mid_p,
                                    const core::Color &to,
                                    const bool horizontal)
        {
            grad_ex *const g = mut_grad();
            g->kind = 5;
            g->flags = horizontal ? 1 : 0;
            g->a = static_cast<uint16_t>(mid_p < 0 ? 0
                                           : (mid_p > 100 ? 100 : mid_p));
            g->col[0] = from;
            g->col[1] = mid;
            g->col[2] = to;
            mark_dirty();
        }
        // N-stop linear (4..8 stops): full percent positions (0..100,
        // non-decreasing) + colors ride the sidecar (kind 6);
        // horizontal in flags bit0. Replaces any paint_dress gradient
        // at draw time
        void set_background_linearN(const int *stop_pos,
                                    const core::Color *stop_col,
                                    const int nstops, const bool horizontal)
        {
            if (stop_pos == nullptr || stop_col == nullptr || nstops < 4 ||
                nstops > 8)
            {
                return;
            }
            grad_ex *const g = mut_grad();
            g->kind = 6;
            g->flags = horizontal ? 1 : 0;
            g->b = static_cast<uint16_t>(nstops);
            int prev = 0;
            for (int i = 0; i < nstops; ++i)
            {
                int p = stop_pos[i] < 0 ? 0 : stop_pos[i];
                if (p > 100)
                {
                    p = 100;
                }
                if (p < prev)
                {
                    p = prev;  // CSS non-decreasing clamp
                }
                prev = p;
                g->pos[i] = static_cast<uint16_t>(p);
                g->col[i] = stop_col[i];
            }
            mark_dirty();
        }
        // repeating stripe overlay (P-2c): 2..6 px stops, period > 0;
        // paints over any base. Anything else is ignored
        void set_background_repeating(const bool horizontal, const int period,
                                      const int *stop_pos,
                                      const core::Color *stop_col,
                                      const int nstops)
        {
            if (stop_pos == nullptr || stop_col == nullptr || nstops < 2 ||
                nstops > 6 || period <= 0 || period > 65535)
            {
                return;
            }
            rep_ex *const r = mut_rep();
            r->flags = horizontal ? 1 : 0;
            r->n = static_cast<uint8_t>(nstops);
            r->period = static_cast<uint16_t>(period);
            for (int i = 0; i < nstops; ++i)
            {
                const int p = stop_pos[i] < 0 ? 0 : stop_pos[i];
                r->pos[i] =
                    static_cast<uint16_t>(p > 65535 ? 65535 : p);
                r->col[i] = stop_col[i];
            }
            mark_dirty();
        }

        // text (input is UTF-8, see docs/code-contract.md section 2)
        void set_text(const char *text);
        void set_text(const std::u16string &text)
        {
            text_ = text;
            advance_cache_ = -1;
            mark_dirty();
            mark_layout_dirty();
        }
        [[nodiscard]] const std::u16string &get_text() const { return text_; }
        // per-widget override of the theme's `text` token (contract 10.3)
        void set_text_color(const core::Color &c)
        {
            text_color_ = c;
            mark_dirty();
        }

        // moves the text baseline start (e.g. a checkbox labelling to the
        // right of its box); applied on top of the alignment. Set when
        // the geometry that determines it changes (ctor/setters), never
        // from the const draw path
        void set_text_offset(const core::impoint_t &off) { text_offset_ = off; }
        void set_h_align(const h_align a)
        {
            halign = a;
            mark_dirty();
        }
        void set_v_align(const v_align a)
        {
            valign = a;
            mark_dirty();
        }
[[nodiscard]] h_align get_h_align() const { return halign; }
        [[nodiscard]] v_align get_v_align() const { return valign; }
        // text dressing (P-2a, all default-off): tracking in px
        // (negative clamps to 0), bold = double-strike +1px, one solid
        // offset shadow copy (shadow alpha 0 = none). Rides the
        // sidecar; every setter clears the advance cache (measure
        // depends on them)
        void set_letter_spacing(const int px)
        {
            widget_ext *const e = mut_text();
            const int c = px < 0 ? 0 : (px > 32767 ? 32767 : px);
            e->letter_px = static_cast<int16_t>(c);
            advance_cache_ = -1;
            mark_dirty();
            mark_layout_dirty();
        }
        [[nodiscard]] int letter_spacing() const
        {
            return (ext_ != nullptr && ext_->has_text != 0)
                       ? ext_->letter_px
                       : 0;
        }
        void set_bold(const bool on)
        {
            widget_ext *const e = mut_text();
            if (on)
            {
                e->text_flags |= 1;
            }
            else
            {
                e->text_flags &= ~1;
            }
            advance_cache_ = -1;
            mark_dirty();
            mark_layout_dirty();
        }
        [[nodiscard]] bool bold() const
        {
            return ext_ != nullptr && ext_->has_text != 0 &&
                   (ext_->text_flags & 1) != 0;
        }
        void set_text_shadow(const core::Color &c, const int dx, const int dy)
        {
            widget_ext *const e = mut_text();
            e->shadow_color = c;
            const int cx = dx < -128 ? -128 : (dx > 127 ? 127 : dx);
            const int cy = dy < -128 ? -128 : (dy > 127 ? 127 : dy);
            e->shadow_dx = static_cast<int8_t>(cx);
            e->shadow_dy = static_cast<int8_t>(cy);
            mark_dirty();
        }
        [[nodiscard]] bool has_text_shadow() const
        {
            return ext_ != nullptr && ext_->has_text != 0 &&
                    ext_->shadow_color.a() != 0;
        }
#if defined(IMCORE_HAS_TTF_RUNTIME)
        /*
         * Per-widget font size (code-contract §2.4): resolves
         * provider_for(px) against the widget's family (explicit
         * argument, else the process font family) and installs it
         * through set_glyph_provider (eager resolve — the draw path
         * is untouched). px <= 0 clears the declaration and resets
         * the primary provider to the process default; out-of-range
         * px or a missing family throws zb::ui::error (init path).
         * Init path: may allocate (first family copy, provider memo).
         */
        void set_font_size(int px);
        void set_font_size(int px, const TtfFamily &family);
        [[nodiscard]] int font_size() const
        {
            return (ext_ != nullptr && ext_->has_font != 0)
                       ? ext_->font_px
                       : 0;
        }
#else
        // Documented degradation (code-contract §2.4): without
        // IMCORE_HAS_TTF_RUNTIME the declaration is ignored (5x7 has
        // no sizes, the build-time subset has exactly one).
        void set_font_size(int px) { (void)px; }
        [[nodiscard]] int font_size() const { return 0; }
#endif

        /*
         * Sets the primary glyph provider (e.g. a TtfRuntimeProvider).
         * Uncovered code units fall back to the built-in bitmap glyphs.
         */
        void set_glyph_provider(const zb::SharedPtr<GlyphProvider> &provider)
        {
            primary_provider_ = provider;
            advance_cache_ = -1;
            mark_dirty();
            mark_layout_dirty();
        }

        /*
         * Renders the widget: clips to the widget's own area, draws the
         * background (color then image), then the foreground (draw_at).
         */
        void draw(core::Graphics &g) const;

        /*
         * Recomputes the position of the widget's children (if any).
         * Leaf widgets do nothing. Called after the widget tree is set up
         * or when child geometry changes.
         */
        virtual void layout() { clear_layout_dirty(); }

        /*
         * Layout invalidation (batch J5): the geometry/content setters
         * flag this widget and its ancestors; a host that enables
         * automatic layout (CanvasWindow::set_auto_layout) runs the root
         * layout from paint(). The flag is cleared at the end of every
         * layout pass, so a pending layout runs at most once per paint.
         */
        void mark_layout_dirty(const bool include_self = true)
        {
            Widget *w = include_self ? this : parent;
            for (; w != nullptr; w = w->parent)
            {
                w->layout_dirty_ = true;
            }
        }
        [[nodiscard]] bool is_layout_dirty() const { return layout_dirty_; }
        void clear_layout_dirty() { layout_dirty_ = false; }

        /*
         * Tests a point given in the widget's local coordinates.
         * Virtual so widgets can restrict hit-testing (e.g. a closed
         * Dialog is not hittable).
         */
        [[nodiscard]] virtual bool hit(const int x, const int y) const;

        // widget tree
        [[nodiscard]] bool is_descendant_of(const Widget *ancestor) const;

        /*
         * First descendant (or this widget) with the given id; nullptr
         * when missing. Linear search: id lookup is for wiring and
         * debugging, not for hot paths.
         */
        Widget *find_by_id(const std::string &id);

        // containers report true so host code can adapt add_child() (see
        // FlexPanel); leaves are false
        [[nodiscard]] virtual bool is_flex_container() const { return false; }

        /*
         * Absolute position in the widget tree (the root's offset is the
         * origin of the coordinate system used by input events).
         */
        [[nodiscard]] core::impoint_t get_absolute_position() const
        {
            core::impoint_t p{0, 0};
            for (const Widget *w = this; w != nullptr; w = w->parent)
            {
                p.x += w->position.x;
                p.y += w->position.y;
            }
            return p;
        }

        // focus
        [[nodiscard]] bool is_focused() const { return focused; }
        [[nodiscard]] virtual bool is_focusable() const { return false; }

    protected:
        /*
         * Draws the widget's foreground; the origin of `area` is the
         * widget's top-left corner and drawing is clipped to the widget's
         * own area. The default foreground is the text.
         */
        virtual void draw_at(core::Graphics &area) const { draw_text(area); }

        // draws the background (color then image); called by draw()
        void draw_background(core::Graphics &area) const;

        /*
         * Effective text color (theme contract 10.3): the per-widget
         * override when set, else the active theme's `text` token,
         * resolved at draw time so theme switches recolor live trees.
         */
        [[nodiscard]] core::Color effective_text_color() const
        {
            return text_color_.value_or(theme().text);
        }

        // draws the text via the primary provider with bitmap fallback
        void draw_text(core::Graphics &area) const;

        // text metrics over the whole text_, split into covered runs
        [[nodiscard]] int text_advance() const;
        [[nodiscard]] int text_height() const;
        // line metrics helpers (do not scan the string)
        [[nodiscard]] int text_ascent() const;

        /*
         * Advance width of an arbitrary UTF-16 run (not just text_),
         * split into covered runs like text_advance(). Text-hosting
         * widgets (TextInput) use it for caret math.
         */
        [[nodiscard]] int advance_of(const char16_t *data, const int len) const;

        /*
         * Draws an arbitrary UTF-16 run (not just text_) with the
         * baseline of the first (only) line at (x, y). Text-hosting
         * widgets (TextInput) render their own buffer with it.
         */
        void draw_text_at(core::Graphics &area, const char16_t *data, const int len,
                          const int x, const int y) const;
        /*
         * draw_text_at with an explicit color (svg <text fill>): the
         * five-argument form above delegates with effective_text_color().
         */
        void draw_text_at(core::Graphics &area, const char16_t *data, const int len,
                          const int x, const int y, const core::Color &color) const;

        /*
         * Returns the primary glyph provider: the custom provider set via
         * set_glyph_provider(), else the process-wide default
         * (set_default_glyph_provider), else nullptr (bitmap-only
         * rendering).
         */
        [[nodiscard]] const GlyphProvider *primary_provider() const;

        /*
         * Input handling, driven by the input dispatcher. The event
         * coordinates refer to the root of the widget tree, but a widget
         * only receives events that hit its own area (the dispatcher
         * guarantees this).
         *
         * Return true to claim the event (the dispatcher then tracks the
         * widget as the pressed target until the release).
         */
        virtual bool on_input(const input::input_event &ev) { (void)ev; return false; }

        // called when the pressed target leaves its area while held
        virtual void on_cancel() {}

        /*
         * Pointer capture (drag semantics): when true, the dispatcher
         * delivers every move to this widget while it is the pressed
         * target and never cancels the press, no matter how far the
         * pointer leaves its area (e.g. a slider being dragged). The
         * return value of on_input() then reports whether the widget
         * changed (repaint gate). Default: false, the press is cancelled
         * by the standard slop rule.
         */
        [[nodiscard]] virtual bool captures_pointer() const { return false; }

        /*
         * Returns the deepest interactive child hit by a point given in
         * this widget's local coordinates, or nullptr.
         */
        virtual Widget *pick(const int x, const int y) { (void)x; (void)y; return nullptr; }

        /*
         * Tree traversal, used by the input dispatcher for focus cycling.
         * Containers override these.
         */
        virtual size_t child_count() const { return 0; }
        virtual Widget *child_at(const size_t i) { (void)i; return nullptr; }

        // set by the input dispatcher when the widget gains/loses focus
        void set_focused(const bool f)
        {
            if (focused != f)
            {
                mark_dirty();
            }
            focused = f;
        }

        // called when the focused widget is activated (Enter/Space)
        virtual void on_activate() {}

        /*
         * Group selection notification: called by a widget on its
         * same-parent siblings when it joined a selection group (the
         * group id and the selected widget). RadioButton uses it to
         * unselect itself when another member of its group is chosen;
         * the base does nothing.
         */
        virtual void on_group_selected(const int group, const Widget *selected)
        {
            (void)group;
            (void)selected;
        }

        /*
         * Notifies every same-parent sibling (except this) that this
         * widget joined a selection group. Sits here (rather than in
         * RadioButton) because only the class itself may reach the
         * protected child walk through a base pointer.
         */
        void notify_siblings_group_selection(const int group, const Widget *selected)
        {
            if (parent != nullptr)
            {
                for (size_t i = 0; i < parent->child_count(); ++i)
                {
                    if (auto *sibling = parent->child_at(i))
                    {
                        if (sibling != this)
                        {
                            sibling->on_group_selected(group, selected);
                        }
                    }
                }
            }
        }

        // set by the container that owns this widget
        Widget *parent = nullptr;

    private:
        core::impoint_t position{0, 0};
        core::imsize_t size{0, 0};
        std::string id_;  // host/designer reference handle, unused by the framework
        bool visible = true;
        bool focused = false;
        bool size_explicit_w_ = false;  // set_size marks the axes it sized
        bool size_explicit_h_ = false;
        // percentage declarations per axis (batch L-4): 0 = not a
        // percent, otherwise the declared percent (1..100) of the
        // FlexPanel parent's content box
        unsigned char width_percent_ = 0;
        unsigned char height_percent_ = 0;
        // aspect ratio declaration (H-5): 0 = none, otherwise w/h pair
        // (uint16_t pair: 4 bytes against the J1 size gate)
        uint16_t aspect_w_ = 0;
        uint16_t aspect_h_ = 0;
        bool layout_dirty_ = true;  // first paint lays out the tree

        static unsigned char percent_clamp(const int pct)
        {
            return pct <= 0 ? 0 : (pct > 100 ? 100 : static_cast<unsigned char>(pct));
        }

        // damage reporting: one unioned rect per widget, in absolute
        // coordinates; empty (dirty_ false) means "nothing reported".
        // subtree_dirty_: this widget or any descendant reported damage
        // since the last paint; the hosting window reads the root's flag
        // to know a frame is owed without walking the tree. It sits next
        // to dirty_ to fill its alignment padding (J1 size gate).
        bool dirty_ = false;
        bool subtree_dirty_ = false;
        int dirty_l_ = 0;
        int dirty_t_ = 0;
        int dirty_r_ = -1;
        int dirty_b_ = -1;

        void mark_dirty_rect(const int x, const int y, const int w, const int h)
        {
            if (w <= 0 || h <= 0)
            {
                return;
            }
            const int r = x + w;
            const int b = y + h;
            if (!dirty_)
            {
                dirty_l_ = x;
                dirty_t_ = y;
                dirty_r_ = r;
                dirty_b_ = b;
                dirty_ = true;
            }
            else
            {
                dirty_l_ = std::min(dirty_l_, x);
                dirty_t_ = std::min(dirty_t_, y);
                dirty_r_ = std::max(dirty_r_, r);
                dirty_b_ = std::max(dirty_b_, b);
            }
            // the pending flag bubbles to the root (the same path as
            // mark_layout_dirty). A set flag implies every ancestor is
            // set too -- walk_damage's recompute never clears a parent
            // while a child is pending -- so the loop stops early when
            // this widget is pending already
            if (!subtree_dirty_)
            {
                subtree_dirty_ = true;
                for (Widget *a = parent; a != nullptr; a = a->parent)
                {
                    a->subtree_dirty_ = true;
                }
            }
        }

        // background
        std::optional<core::Color> background;
        std::optional<core::image_t> background_image;
        // paint dressing (P-1, 24 bytes: the batch J budget leaves no
        // room for two optionals + ints, so one packed struct, kinds
        // instead of nullopts; border_w 0 = no border, bg_kind 0 =
        // no gradient, radius_kind 0 = square)
        struct paint_dress
        {
            uint8_t bg_kind = 0;  // 0 none, 1 linear, 2 radial
            uint8_t bg_ax = 1;    // linear: horizontal; radial: cx %
            uint8_t bg_ay = 50;   // radial: cy %
            uint8_t bg_p0 = 0;    // radial from stop
            uint8_t bg_p1 = 100;  // radial to stop
            core::Color bg_from{};
            core::Color bg_to{};
            uint8_t border_w = 0;
            uint8_t radius_kind = 0;  // 0 none, 1 px, 2 half
            uint16_t radius_px = 0;
            core::Color border_color{};
        };
        paint_dress dress_;

        // one heap sidecar for every optional dressing section (batch
        // J): a bare widget keeps ext_ null (zero-alloc ctor, lean
        // inline); each setter allocates once and flags its section,
        // so positioned, gradient, and tracked widgets share one
        // allocation instead of three pointers inline
        struct widget_ext
        {
            abs_spec pos{};
            grad_ex grad{};
            rep_ex rep{};
            bord_t bord{};
            shadow_spec sh_out[2]{};
            shadow_spec sh_in[2]{};
            int16_t letter_px = 0;
            int16_t margin[4] = {0, 0, 0, 0};  // t/r/b/l in-flow margins (H-3)
            int16_t font_px = 0;  // per-widget size declaration (0 = unset)
            uint8_t text_flags = 0;  // bit0 = bold (double-strike)
            core::Color shadow_color{};
            int8_t shadow_dx = 0;
            int8_t shadow_dy = 0;
#if defined(IMCORE_HAS_TTF_RUNTIME)
            TtfFamily font_family{};  // anchor copy: the sized provider
                                      // never outlives its family (§2.4)
#endif
            uint8_t has_pos = 0;
            uint8_t has_grad = 0;
            uint8_t has_rep = 0;
            uint8_t has_bord = 0;
            uint8_t has_text = 0;
            uint8_t has_font = 0;
            uint8_t has_margin = 0;
            uint8_t n_sh_out = 0;
            uint8_t n_sh_in = 0;
        };
        std::unique_ptr<widget_ext> ext_;
        void ensure_ext()
        {
            if (ext_ == nullptr)
            {
                ext_ = std::make_unique<widget_ext>();
            }
        }
        [[nodiscard]] const abs_spec *pos() const
        {
            return (ext_ != nullptr && ext_->has_pos != 0) ? &ext_->pos
                                                          : nullptr;
        }
        abs_spec *mut_pos()
        {
            ensure_ext();
            ext_->has_pos = 1;
            return &ext_->pos;
        }
        [[nodiscard]] const grad_ex *grad() const
        {
            return (ext_ != nullptr && ext_->has_grad != 0) ? &ext_->grad
                                                           : nullptr;
        }
        grad_ex *mut_grad()
        {
            ensure_ext();
            ext_->has_grad = 1;
            return &ext_->grad;
        }
        [[nodiscard]] const rep_ex *rep() const
        {
            return (ext_ != nullptr && ext_->has_rep != 0) ? &ext_->rep
                                                          : nullptr;
        }
        rep_ex *mut_rep()
        {
            ensure_ext();
            ext_->has_rep = 1;
            return &ext_->rep;
        }
        bord_t *mut_bord()
        {
            ensure_ext();
            ext_->has_bord = 1;
            return &ext_->bord;
        }
        widget_ext *mut_text()
        {
            ensure_ext();
            ext_->has_text = 1;
            return ext_.get();
        }

        // text
        std::u16string text_;
        // unset = follow the active theme's `text` token (contract 10.3)
        std::optional<core::Color> text_color_;
        // layout hint applied at draw time; widgets set it when the
        // geometry that determines it changes (e.g. a checkbox label
        // offset tracks box_size + text_gap)
        core::impoint_t text_offset_{0, 0};
        h_align halign = h_align::left;
        v_align valign = v_align::top;
        zb::SharedPtr<GlyphProvider> primary_provider_;
        /*
         * Fallback provider (batch J6): every widget shares one
         * process-level BitmapProvider instance instead of allocating
         * its own. BitmapProvider is stateless (zero data members), so
         * the sharing is safe; adding state to it requires unsharing
         * first. The shared instance is created before any widget and
         * never destroyed (leaked on purpose), so a widget that outlives
         * main can never touch a dead provider.
         */
        zb::SharedPtr<BitmapProvider> bitmap_fallback_;
        /*
         * text_advance() cache (batch J4): the measurement splits text_
         * into provider runs and asks each provider, which costs per
         * call; the result depends only on text_ and the glyph providers
         * (covers/measure), so every setter that changes either resets
         * the cache. advance_of() (arbitrary runs) stays uncached.
         */
         mutable int advance_cache_ = -1;
    };

#if defined(IMCORE_HAS_TTF_RUNTIME)
    /*
     * Process font family (code-contract §2.4): the family
     * set_font_size(px) resolves against when the caller passes none.
     * Parallel to set_default_glyph_provider; empty by default.
     * Init path: may allocate (the family handle copy).
     */
    void set_font_family(const TtfFamily &family);
    void clear_font_family();
    [[nodiscard]] bool has_font_family();
    [[nodiscard]] const TtfFamily &font_family();
#else
    [[nodiscard]] inline bool has_font_family() { return false; }
#endif
}
