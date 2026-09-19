#include "widget.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "text/utf8.hpp"

namespace zb::ui
{
    namespace
    {
        struct shadow_span
        {
            int left = 0;
            int right = -1;
            int fringe_l = 0;
            int fringe_r = 0;
        };

        // Row span of a rounded box on the pixel-center grid, in 1/4-px
        // units (corner_chord works on integers, so the half-pixel
        // centers ride scaled coordinates). Arc centers sit at
        // (left + r, top + r) / (right + 1 - r, bottom + 1 - r)
        // continuously; straight-run rows keep the full span. The old
        // index-space chords measured even boxes against half-pixel arc
        // centers, collapsing tangent rows a row early — the model500
        // knob's padding circle lost its last-row clip, so the inset
        // shadow dropped the whole bottom arc (the bright leak ring
        // inside the rim).
        shadow_span rounded_shadow_span(int left, int top, int right,
                                        int bottom, int radius, int row)
        {
            if (left > right || top > bottom || row < top || row > bottom)
            {
                return {};
            }
            const int r = std::max(0, std::min(radius,
                std::min(right - left + 1, bottom - top + 1) / 2));
            const int r4 = 4 * r;
            const int y4 = 4 * row + 2;
            const int top_arc4 = 4 * top + r4;
            const int bot_arc4 = 4 * (bottom + 1) - r4;
            const int dy4 = std::max(0, std::max(top_arc4 - y4, y4 - bot_arc4));
            const int chord4 = core::Graphics::corner_chord(r4, dy4);
            const int lo4 = 4 * left + r4 - chord4;
            const int hi4 = 4 * (right + 1) - r4 + chord4;
            shadow_span s;
            // first/last FULL pixels: ceil(lo4/4) and hi4/4 - 1, with the
            // fractional remainder carried as explicit partial pixels
            // just outside (fractions of 255). Rounding both edges to
            // nearest keeps the span mirror-symmetric — the old
            // floor-at-the-start rule painted a half-covered edge pixel
            // as full on one side only, and the blur leaked that 1/4-px
            // bias straight into the spill (the disc mirror lock).
            s.left = std::max(left, (lo4 + 3) / 4);
            s.right = std::min(right, hi4 / 4 - 1);
            if (s.left > s.right)
            {
                return {};  // sub-pixel chord: the blur smears the sliver
            }
            const int fl = 4 * s.left - lo4;
            const int fr = hi4 - 4 * (s.right + 1);
            s.fringe_l = fl > 0 ? fl * 255 / 4 : 0;
            s.fringe_r = fr > 0 ? fr * 255 / 4 : 0;
            return s;
        }

        int triangle_prefix(int offset, int blur)
        {
            if (offset < -blur)
            {
                return 0;
            }
            const int norm = (blur + 1) * (blur + 1);
            if (offset >= blur)
            {
                return norm;
            }
            if (offset <= 0)
            {
                const int n = offset + blur + 1;
                return n * (n + 1) / 2;
            }
            const int n = blur - offset;
            return norm - n * (n + 1) / 2;
        }

        void paint_inset_shadow(core::Graphics &area, int width, int height,
                                int radius, int border, int ox, int oy,
                                int blur, int spread, const core::Color &color)
        {
            const int left = border;
            const int top = border;
            const int right = width - 1 - border;
            const int bottom = height - 1 - border;
            if (left > right || top > bottom || color.a() == 0)
            {
                return;
            }
        // Continuous radii: the box is `width` px wide (the old
        // (width-1)/2 index clamp shaved the padding circle a full
        // pixel on even boxes, thinning the inset all around and
        // collapsing its bottom arc).
        const int outer_radius = std::max(0, std::min(radius,
            std::min(width, height) / 2));
        const int inner_radius = std::max(0, outer_radius - border);
            const int norm = (blur + 1) * (blur + 1);
            const int64_t divisor = 1LL * norm * norm;
            // Shadow blur is uint8_t; row spans bound scratch space even
            // for large widgets and avoid allocating a mask per frame.
            std::array<shadow_span, 511> rows;
            for (int y = top; y <= bottom; ++y)
            {
                const auto clip = rounded_shadow_span(left, top, right, bottom,
                                                       inner_radius, y);
                for (int dy = -blur; dy <= blur; ++dy)
                {
                    rows[dy + blur] = rounded_shadow_span(
                        left + spread + ox, top + spread + oy,
                        right - spread + ox, bottom - spread + oy,
                        std::max(0, inner_radius - spread), y + dy);
                }
                const int start = std::max(left, clip.left - 1);
                const int end = std::min(right, clip.right + 1);
            for (int x = start; x <= end; ++x)
            {
                const int coverage = x < clip.left ? clip.fringe_l
                                     : x > clip.right ? clip.fringe_r
                                                      : 255;
                if (coverage == 0)
                {
                    continue;
                }
                int64_t hole = 0;
                for (int dy = -blur; dy <= blur; ++dy)
                {
                    const auto &row = rows[dy + blur];
                    if (row.left > row.right)
                    {
                        continue;
                    }
                    int horizontal = 255 *
                        (triangle_prefix(row.right - x, blur) -
                         triangle_prefix(row.left - x - 1, blur));
                    horizontal += row.fringe_l *
                        std::max(0, blur + 1 - std::abs(row.left - 1 - x));
                    horizontal += row.fringe_r *
                        std::max(0, blur + 1 - std::abs(row.right + 1 - x));
                    hole += 1LL * horizontal * (blur + 1 - std::abs(dy));
                }
                    const int shadow = 255 - static_cast<int>((hole + divisor / 2) /
                                                                             divisor);
                    area.plot_aa(x, y, (shadow * coverage + 127) / 255, color);
                }
            }
        }

    // Outer (drop) shadow: the blurred silhouette indicator — the exact
    // mirror of the inset's hole. Mask = spread-expanded, offset rounded
    // box from the same continuous quarter-px row chords; filter = the
    // same separable normalized integer triangular kernel (support
    // [-blur, blur], weights blur + 1 - |offset| on each axis). A
    // blurred edge reads half strength at the silhouette contour and
    // decays smoothly over the blur radius. The old approximation (flat
    // half-alpha core + stepped halo rings) put a 2x luminance wall
    // exactly at the contour — the knob's "popped ring" — and the rings'
    // SDF tails stacked darker crescents on the diagonal arcs. Two
    // passes: horizontal is the inset's analytic interval formula per
    // mask row into an int32 line block, vertical is the weighted dy sum
    // normalized by norm^2 (the inset's hole math). plot_aa gates
    // clip/damage and binary depths (half-coverage rule).
    void paint_outer_shadow(core::Graphics &area, const int x0, const int y0,
                            const int x1, const int y1, const int radius,
                            const int blur, const core::Color &color)
    {
        if (x0 > x1 || y0 > y1 || blur <= 0 || color.a() == 0)
        {
            return;
        }
        const int norm = (blur + 1) * (blur + 1);
        const int64_t divisor = 1LL * norm * norm;
        // coverage is nonzero only within blur (+1 fringe) of the
        // silhouette; that block bounds the scratch
        const int xr0 = x0 - blur - 1;
        const int xr1 = x1 + blur + 1;
        const int yr0 = y0 - blur;
        const int yr1 = y1 + blur;
        const long long bw = xr1 - xr0 + 1;
        const long long bh = yr1 - yr0 + 1;
        // retained scratch grown to the high-water mark: no per-frame
        // allocation (the inset binds scratch to blur for the same
        // reason); paints are single-threaded
        static std::vector<int32_t> buf;
        const size_t need = static_cast<size_t>(bw * bh);
        if (buf.size() < need)
        {
            buf.resize(need);
        }
        std::fill(buf.begin(), buf.begin() + need, 0);
        // pass 1 — horizontal triangular of each mask row (rows outside
        // the silhouette stay zero)
        for (int row = y0; row <= y1; ++row)
        {
            const shadow_span sp =
                rounded_shadow_span(x0, y0, x1, y1, radius, row);
            if (sp.left > sp.right)
            {
                continue;
            }
            const int xl = std::max(xr0, sp.left - 1 - blur);
            const int xr = std::min(xr1, sp.right + 1 + blur);
            for (int x = xl; x <= xr; ++x)
            {
                int horizontal = 255 *
                    (triangle_prefix(sp.right - x, blur) -
                     triangle_prefix(sp.left - 1 - x, blur));
                horizontal += sp.fringe_l *
                    std::max(0, blur + 1 - std::abs(sp.left - 1 - x));
                horizontal += sp.fringe_r *
                    std::max(0, blur + 1 - std::abs(sp.right + 1 - x));
                buf[static_cast<size_t>((row - yr0) * bw + (x - xr0))] =
                    horizontal;
            }
        }
        // pass 2 — vertical weighted sum, normalized like the inset's
        // hole (the kernel sums to norm per axis, norm^2 total)
        for (int y = yr0; y <= yr1; ++y)
        {
            const int dy0 = std::max(-blur, y0 - y);
            const int dy1 = std::min(blur, y1 - y);
            for (int x = xr0; x <= xr1; ++x)
            {
                int64_t sum = 0;
                for (int dy = dy0; dy <= dy1; ++dy)
                {
                    sum += 1LL *
                           buf[static_cast<size_t>((y + dy - yr0) * bw +
                                                   (x - xr0))] *
                           (blur + 1 - std::abs(dy));
                }
                const int coverage =
                    static_cast<int>((sum + divisor / 2) / divisor);
                if (coverage > 0)
                {
                    area.plot_aa(x, y, coverage, color);
                }
            }
        }
    }

        // process-level shared fallback provider (batch J6): constructed
        // at the first widget construction, then leaked so it outlives
        // every widget, including static-storage ones destroyed after
        // main. Safe because BitmapProvider is stateless.
        const zb::SharedPtr<BitmapProvider> &fallback_singleton()
        {
            static zb::SharedPtr<BitmapProvider> *p =
                new zb::SharedPtr<BitmapProvider>(zb::make_shared<BitmapProvider>());
            return *p;
        }

        // process-wide default glyph provider (code-contract 2.4, plan 2);
        // leaked like the fallback so static-lifetime widgets never touch
        // a dead pointer
        zb::SharedPtr<GlyphProvider> &default_provider_state()
        {
            static zb::SharedPtr<GlyphProvider> *p = new zb::SharedPtr<GlyphProvider>();
            return *p;
        }
    }  // namespace

    void set_default_glyph_provider(const zb::SharedPtr<GlyphProvider> &provider)
    {
        default_provider_state() = provider;
    }

    const zb::SharedPtr<GlyphProvider> &default_glyph_provider()
    {
        return default_provider_state();
    }

#if defined(IMCORE_HAS_TTF_RUNTIME)
    namespace
    {
        // process font family (code-contract §2.4): the family the
        // single-argument set_font_size resolves against. A plain
        // function-static (not leaked): widgets hold their own anchor
        // copy, and the shared family state is refcounted, so no
        // lifetime hazard either way.
        TtfFamily &font_family_state()
        {
            static TtfFamily fam;
            return fam;
        }
        bool &font_family_set()
        {
            static bool set = false;
            return set;
        }
    }  // namespace

    void set_font_family(const TtfFamily &family)
    {
        font_family_state() = family;
        font_family_set() = true;
    }

    void clear_font_family()
    {
        font_family_state() = TtfFamily();
        font_family_set() = false;
    }

    bool has_font_family() { return font_family_set(); }

    const TtfFamily &font_family() { return font_family_state(); }

    void Widget::set_font_size(const int px)
    {
        if (!has_font_family())
        {
            throw error("Widget::set_font_size: no font family installed "
                        "(set_font_family or the two-argument overload)");
        }
        set_font_size(px, font_family_state());
    }

    void Widget::set_font_size(const int px, const TtfFamily &family)
    {
        if (px <= 0)
        {
            // 0 = unset (the percent convention): drop the declaration
            // and reset the primary provider to the process default
            if (ext_ != nullptr)
            {
                ext_->has_font = 0;
                ext_->font_px = 0;
                ext_->font_family = TtfFamily();
            }
            set_glyph_provider(zb::SharedPtr<GlyphProvider>());
            return;
        }
        if (px < 1 || px > 128)
        {
            throw error("Widget::set_font_size: pixel size out of range 1..128");
        }
        zb::SharedPtr<GlyphProvider> provider = family.provider_for(px);
        ensure_ext();
        ext_->has_font = 1;
        ext_->font_px = static_cast<int16_t>(px);
        ext_->font_family = family;
        set_glyph_provider(provider);
    }
#endif

    Widget::Widget() : bitmap_fallback_(fallback_singleton())
    {
    }

    void Widget::set_text(const char *text)
    {
        mark_dirty();
        advance_cache_ = -1;
        mark_layout_dirty();
        text_.clear();
        if (nullptr == text)
        {
            return;
        }
        text_ = utf8_to_utf16(text);
    }

    void Widget::draw(core::Graphics &g) const
    {
        if (!visible || size.width <= 0 || size.height <= 0)
        {
            return;
        }

        // damage culling: outside the reported region the whole subtree
        // is by definition invisible, skip the rasterizer entirely
        if (g.damage_on())
        {
            const auto abs = get_absolute_position();
            int pl = 0, pt = 0, pr = 0, pb = 0;
            outer_shadow_pad(pl, pt, pr, pb);
            if (!g.damage_intersects(abs.x - pl, abs.y - pt,
                                     size.width + pl + pr, size.height + pt + pb))
            {
                return;
            }
        }

        // P-2e: outer shadows may reach past the box (within the
        // parent's clip): phase 1 paints the shadow silhouettes under
        // the expanded clip (the guard restores the parent clip on
        // scope exit); phase 2 repaints the full dress + content under
        // the widget's own clip, which cuts the shadow spill at the
        // box edge
        draw_background_shadows_only(g);
        {
            auto box_area = g.clip_safe(position.x, position.y, size.width, size.height);
            if (!box_area)
            {
                return;
            }
            draw_background(g);
            draw_at(g);
            // ::after paints above the whole subtree (H-10)
            paint_pseudo(g, 1);
        }
    }

    // generated box paint (H-10 narrow): resolves the box against the
    // widget (sides % of self or px, margins push inward), then paints
    // solid / three-stop-linear / rotated-solid. Anything else was
    // refused at build; an unresolvable box skips silently.
    void Widget::paint_pseudo(core::Graphics &area, const int kind) const
    {
        const pseudo_spec *ps = pseudo(kind);
        if (ps == nullptr)
        {
            return;
        }
        const auto s = get_size();
        auto side = [&](const int i) {
            int v = ps->off[i];
            if ((ps->off_pct & (1U << i)) != 0U)
            {
                v = v * (i % 2 == 0 ? s.width : s.height) / 100;
            }
            return v;
        };
        const bool has_l = (ps->off_mask & 1U) != 0U;
        const bool has_t = (ps->off_mask & 2U) != 0U;
        const bool has_r = (ps->off_mask & 4U) != 0U;
        const bool has_b = (ps->off_mask & 8U) != 0U;
        int x = 0;
        int w = 0;
        if (ps->has_w != 0)
        {
            w = ps->w;
            x = has_l ? side(0) + ps->margin[0]
                      : (has_r ? s.width - side(2) - ps->margin[2] - w : 0);
        }
        else if (has_l && has_r)
        {
            x = side(0) + ps->margin[0];
            w = s.width - x - side(2) - ps->margin[2];
        }
        else
        {
            return;
        }
        int y = 0;
        int h = 0;
        if (ps->has_h != 0)
        {
            h = ps->h;
            y = has_t ? side(1) + ps->margin[1]
                      : (has_b ? s.height - side(3) - ps->margin[3] - h : 0);
        }
        else if (has_t && has_b)
        {
            y = side(1) + ps->margin[1];
            h = s.height - y - side(3) - ps->margin[3];
        }
        else
        {
            return;
        }
        if (w <= 0 || h <= 0)
        {
            return;
        }
        int radius = 0;
        if (ps->radius_kind == 2)
        {
            radius = std::min(w, h) / 2;
        }
        else if (ps->radius_kind == 1)
        {
            const int half = std::min(w, h) / 2;
            radius = ps->radius_px < half ? ps->radius_px : half;
        }
        const bool bak = area.is_alpha_enabled();
        area.enable_alpha(true);
        if (ps->rot_ang != 0)
        {
            // rotated paints solid only (builder refused the rest)
            if (ps->grad_kind == 0)
            {
                const int ox = ps->rot_ox_pct != 0 ? ps->rot_ox * w / 100
                                                  : ps->rot_ox;
                const int oy = ps->rot_oy_pct != 0 ? ps->rot_oy * h / 100
                                                  : ps->rot_oy;
                area.fill_round_rect_rotated(x, y, w, h, radius, ps->rot_ang,
                                             x + ox, y + oy, ps->bg);
            }
        }
        else if (ps->grad_kind == 5)
        {
            area.fill_gradient3(x, y, x + w - 1, y + h - 1, ps->bg, ps->mid,
                                ps->grad_mid_p, ps->to, ps->grad_h != 0,
                                radius);
        }
        else if (radius > 0)
        {
            area.fill_round_rect_aa(x, y, x + w - 1, y + h - 1, radius,
                                    ps->bg);
        }
        else
        {
            area.fill_rect(x, y, x + w - 1, y + h - 1, ps->bg);
        }
        area.enable_alpha(bak);
    }

    void Widget::draw_background(core::Graphics &area) const
    {
        draw_background_impl(area, true, 0, 0);
    }

    void Widget::draw_background_shadows_only(core::Graphics &g) const
    {
        int pl = 0, pt = 0, pr = 0, pb = 0;
        outer_shadow_pad(pl, pt, pr, pb);
        // position is relative to the current (parent) clip — the same
        // convention draw() uses for the box clip
        auto area = g.clip_safe(position.x - pl, position.y - pt,
                                size.width + pl + pr, size.height + pt + pb);
        if (!area)
        {
            return;
        }
        // the expanded clip's origin is the padded box; the shadow
        // silhouettes shift by (pl, pt) to stay at the widget position
        draw_background_impl(g, false, pl, pt);
    }

    void Widget::draw_background_impl(core::Graphics &area, bool full,
                                      const int sh_dx, const int sh_dy) const
    {
        const auto s = get_size();
        // corner radius shared by the background and the border (P-1):
        // px clamps to half the smaller side, the half form resolves
        // 50% at draw time
        int radius = 0;
        if (dress_.radius_kind != 0)
        {
            const int half_min = std::min(s.width, s.height) / 2;
            radius = dress_.radius_kind == 2
                         ? half_min
                         : std::min<int>(dress_.radius_px, half_min);
        }
        // a dressed color below opaque paints blended; on 16bpp
        // (binary alpha) the enable is a visual no-op — alpha_blend
        // returns front for any set bit — so one rule covers both
        auto needs_blend = [](const core::Color &c) { return c.a() < 255; };
        bool blend = background.has_value() && needs_blend(*background);
        if (dress_.bg_kind != 0 &&
            (needs_blend(dress_.bg_from) || needs_blend(dress_.bg_to)))
        {
            blend = true;
        }
        // extended gradient colors (P-2b) and a translucent border
        // (the knob's rgba outline) blend the same way, as do the
        // repeating overlay stops (P-2c)
        if (const grad_ex *const g = grad())
        {
            for (int i = 0; i < 4; ++i)
            {
                if (needs_blend(g->col[i]))
                {
                    blend = true;
                    break;
                }
            }
        }
        if (const rep_ex *const r = rep())
        {
            for (int i = 0; i < r->n && i < 6; ++i)
            {
                if (needs_blend(r->col[i]))
                {
                    blend = true;
                    break;
                }
            }
        }
        if (dress_.border_w > 0 && needs_blend(dress_.border_color))
        {
            blend = true;
        }
        // the top band blends like any border (P-2d)
        if (const bord_t *const b = top_border();
            b != nullptr && needs_blend(b->c))
        {
            blend = true;
        }
        // Filtered shadow coverage requires blending even for opaque colors.
        if (ext_ != nullptr)
        {
            if (ext_->n_sh_in > 0)
            {
                blend = true;
            }
            for (int i = 0; i < ext_->n_sh_out && i < 2; ++i)
            {
                if (needs_blend(ext_->sh_out[i].c) ||
                    ext_->sh_out[i].blur > 0)
                {
                    blend = true;
                    break;
                }
            }
        }
        const bool bak = area.is_alpha_enabled();
        if (blend)
        {
            area.enable_alpha(true);
        }
        // outer silhouettes first (P-2e): spread-expanded rounded box
        // at the offset, all under the background (the widget clip keeps
        // the inside part — see the contract's overdraw note). A blurred
        // shadow is the silhouette indicator run through the SAME
        // separable normalized triangular kernel as the inset: half
        // strength at the contour, smooth decay over ±blur. blur==0
        // keeps the hard silhouette exactly. Wireframe skips shadows
        // (bones).
        const bool shadows =
            area.get_render_mode() == core::Graphics::render_mode::full;
        if (shadows && ext_ != nullptr)
        {
            for (int i = 0; i < ext_->n_sh_out && i < 2; ++i)
            {
                const shadow_spec &sh = ext_->sh_out[i];
                const int x0 = sh_dx + sh.ox - sh.spread;
                const int y0 = sh_dy + sh.oy - sh.spread;
                const int x1 = sh_dx + s.width - 1 + sh.ox + sh.spread;
                const int y1 = sh_dy + s.height - 1 + sh.oy + sh.spread;
                if (sh.blur > 0 && sh.c.a() > 0)
                {
                    // real blur, same kernel as the inset (see
                    // paint_outer_shadow): no interior plateau, no
                    // stepped halo — the spill decays smoothly from half
                    // strength at the contour; binary depths threshold
                    // the coverage at half inside plot_aa
                    paint_outer_shadow(area, x0, y0, x1, y1,
                                       radius + sh.spread, sh.blur, sh.c);
                }
                else
                {
                    area.fill_round_rect_aa(x0, y0, x1, y1,
                                            radius + sh.spread, sh.c);
                }
            }
        }
        if (!full)
        {
            // shadow-only phase: the background and everything after it
            // repaint under the widget's own clip (draw_background)
            area.enable_alpha(bak);
            return;
        }
        if (background.has_value())
        {
            // S-1: through fill_rect (not fill) so a wireframe view
            // degrades the face to an outline; pixel-identical to fill()
            // in FULL mode. The dialog mask keeps fill() (immune).
            if (radius > 0)
            {
                area.fill_round_rect_aa(0, 0, s.width - 1, s.height - 1, radius,
                                        *background);
            }
            else
            {
                area.fill_rect(0, 0, s.width - 1, s.height - 1, *background);
            }
        }
        // extended forms override every paint_dress gradient
        // (contract P-2b/c; the builder sets exactly one overall)
        if (const grad_ex *const g = grad();
            g != nullptr && (g->kind == 3 || g->kind == 5 || g->kind == 6))
        {
            if (g->kind == 3)
            {
                int pos[4] = {0, 0, 0, 0};
                const int n = g->b < 2 ? 2 : (g->b > 4 ? 4 : g->b);
                for (int i = 0; i < n; ++i)
                {
                    pos[i] = g->pos[i];
                }
                area.fill_conic(0, 0, s.width - 1, s.height - 1, g->a, pos,
                                g->col, n, radius);
            }
            else if (g->kind == 6)
            {
                int pos[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                const int n = g->b < 4 ? 4 : (g->b > 8 ? 8 : g->b);
                for (int i = 0; i < n; ++i)
                {
                    pos[i] = g->pos[i];
                }
                area.fill_linear_stops(0, 0, s.width - 1, s.height - 1, pos,
                                       g->col, n, (g->flags & 1) != 0, radius);
            }
            else
            {
                area.fill_gradient3(0, 0, s.width - 1, s.height - 1,
                                    g->col[0], g->col[1], g->a, g->col[2],
                                    (g->flags & 1) != 0, radius);
            }
        }
        // radial wins when both kinds are set (contract P-1; the
        // builder sets exactly one, this is direct-setter misuse)
        else if (dress_.bg_kind == 2)
        {
            area.fill_radial(0, 0, s.width - 1, s.height - 1,
                             s.width * dress_.bg_ax / 100,
                             s.height * dress_.bg_ay / 100, dress_.bg_from,
                             dress_.bg_p0, dress_.bg_to, dress_.bg_p1, radius);
        }
        else if (dress_.bg_kind == 1)
        {
            area.fill_gradient(0, 0, s.width - 1, s.height - 1, dress_.bg_from,
                               dress_.bg_to, dress_.bg_ax != 0, radius);
        }
        // repeating texture overlays any base (P-2c); square mostly
        // (vubottom/brushed), the radius rides along when set
        if (const rep_ex *const r = rep(); r != nullptr && r->n >= 2)
        {
            int pos[6] = {0, 0, 0, 0, 0, 0};
            const int n = r->n > 6 ? 6 : r->n;
            for (int i = 0; i < n; ++i)
            {
                pos[i] = r->pos[i];
            }
            area.fill_repeating(0, 0, s.width - 1, s.height - 1,
                                (r->flags & 1) != 0, r->period, pos, r->col,
                                n, radius);
        }
        if (background_image.has_value())
        {
            area.draw_image(*background_image, 0, 0);
        }
        // border-box outline over the background; a default (alpha-0)
        // color never reaches here through the builder (parse_color
        // rejects transparent), the guard is for direct app misuse
        for (int i = 0; i < dress_.border_w; ++i)
        {
            if (dress_.border_color.a() == 0)
            {
                break;
            }
            if (radius > 0)
            {
                area.draw_round_rect_aa(i, i, s.width - 1 - i, s.height - 1 - i,
                                        radius - i < 0 ? 0 : radius - i,
                                        dress_.border_color);
            }
            else
            {
                area.draw_rect(i, i, s.width - 1 - i, s.height - 1 - i,
                               dress_.border_color);
            }
        }
        // top border band (P-2d): full-width strip; radius corners
        // are not cut (contract)
        if (const bord_t *const b = top_border(); b != nullptr)
        {
            const int bh = b->w > s.height ? s.height : b->w;
            area.fill_rect(0, 0, s.width - 1, bh - 1, b->c);
        }
        if (shadows && ext_ != nullptr)
        {
            for (int k = 0; k < ext_->n_sh_in && k < 2; ++k)
            {
                const shadow_spec &sh = ext_->sh_in[k];
                paint_inset_shadow(area, s.width, s.height, radius,
                                   dress_.border_w, sh.ox, sh.oy,
                                   sh.blur, sh.spread, sh.c);
            }
        }
        if (blend)
        {
            area.enable_alpha(bak);
        }
        // ::before paints above the dress, below the children (H-10)
        paint_pseudo(area, 0);
    }

    const GlyphProvider *Widget::primary_provider() const
    {
        if (primary_provider_ != nullptr)
        {
            return primary_provider_.get();
        }
        return default_glyph_provider().get();
    }

    void Widget::draw_text(core::Graphics &area) const
    {
        if (text_.empty())
        {
            return;
        }

        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();

        // pick the provider for a code unit; nullptr means "not covered"
        const auto pick = [&](const char16_t ch) -> const GlyphProvider *
        {
            if (primary != nullptr && primary->covers(ch))
            {
                return primary;
            }
            if (fallback->covers(ch))
            {
                return fallback;
            }
            return nullptr;
        };

            const auto s = get_size();
    const char16_t *const data = text_.data();
    const int len = static_cast<int>(text_.size());

    // total advance: split into covered runs; uncovered units add 0
    const int total = text_advance();

        // line metrics come from the primary provider, or the fallback;
        // line_metrics does not scan the string (no per-glyph loads)
        const text_metrics m = primary != nullptr
                                   ? primary->line_metrics()
                                   : fallback->line_metrics();

        int x = 0;
        switch (halign)
        {
        case h_align::left:
            x = 0;
            break;
        case h_align::center:
            x = (s.width - total) / 2;
            break;
        case h_align::right:
            x = s.width - total;
            break;
        }

        int y = 0;  // baseline of the first (only) line
        switch (valign)
        {
        case v_align::top:
            y = m.ascent;
            break;
        case v_align::center:
            y = (s.height - m.height) / 2 + m.ascent;
            break;
        case v_align::bottom:
            y = s.height - m.height + m.ascent;
            break;
        }

        x += text_offset_.x;
        y += text_offset_.y;

        draw_text_at(area, data, len, x, y);
    }

    bool Widget::hit(const int x, const int y) const
    {
        return visible && x >= 0 && y >= 0 && x < size.width && y < size.height;
    }

    Widget *Widget::find_by_id(const std::string &id)
    {
        if (get_id() == id)
        {
            return this;
        }
        for (size_t i = 0; i < child_count(); ++i)
        {
            if (auto *c = child_at(i))
            {
                if (const auto hit = c->find_by_id(id))
                {
                    return hit;
                }
            }
        }
        return nullptr;
    }

    int Widget::text_advance() const
    {
        if (advance_cache_ >= 0)
        {
            return advance_cache_;
        }
        advance_cache_ = advance_of(text_.data(), static_cast<int>(text_.size()));
        return advance_cache_;
    }

    int Widget::text_ascent() const
    {
        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();
        const text_metrics m = primary != nullptr
                                   ? primary->line_metrics()
                                   : fallback->line_metrics();
        return m.ascent;
    }

    int Widget::advance_of(const char16_t *data, const int len) const
    {
        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();

        // pick the provider for a code unit; nullptr means "not covered"
        const auto pick = [&](const char16_t ch) -> const GlyphProvider *
        {
            if (primary != nullptr && primary->covers(ch))
            {
                return primary;
            }
            if (fallback->covers(ch))
            {
                return fallback;
            }
            return nullptr;
        };

        if (len <= 0)
        {
            return 0;
        }
        // letter-spacing (P-2a): per covered code unit, trailing unit
        // included. The zero-spacing run path below stays untouched so
        // plain text is bit-identical (splitting is safe — no provider
        // kerns — but the runs also skip one measure call per glyph)
        const int letter =
            (ext_ != nullptr && ext_->has_text != 0) ? ext_->letter_px : 0;
        if (letter > 0)
        {
            int spaced = 0;
            for (int i = 0; i < len; ++i)
            {
                const GlyphProvider *const p = pick(data[i]);
                if (p != nullptr)
                {
                    spaced += p->measure(data + i, 1).width + letter;
                }
            }
            return spaced;
        }
        int total = 0;
        for (int i = 0; i < len;)
        {
            const GlyphProvider *cur = pick(data[i]);
            int j = i;
            while (j < len && pick(data[j]) == cur)
            {
                ++j;
            }
            if (cur != nullptr)
            {
                total += cur->measure(data + i, j - i).width;
            }
            i = j;
        }
        return total;
    }

    void Widget::draw_text_at(core::Graphics &area, const char16_t *data, const int len,
                              const int x, const int y) const
    {
        draw_text_at(area, data, len, x, y, effective_text_color());
    }

    void Widget::draw_text_at(core::Graphics &area, const char16_t *data, const int len,
                              const int x, const int y, const core::Color &color) const
    {
        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();

        const auto pick = [&](const char16_t ch) -> const GlyphProvider *
        {
            if (primary != nullptr && primary->covers(ch))
            {
                return primary;
            }
            if (fallback->covers(ch))
            {
                return fallback;
            }
            return nullptr;
        };

        // one run of glyphs at (x0, y0): the provider-run fast path
        // when tracking is off, per-unit pen (unit advance + spacing)
        // when on. Uncovered units keep the pen position either way
        const int letter =
            (ext_ != nullptr && ext_->has_text != 0) ? ext_->letter_px : 0;
        const bool is_bold = bold();
        const core::Color shadow =
            has_text_shadow() ? ext_->shadow_color : core::Color{};
        const int sh_dx =
            has_text_shadow() ? static_cast<int>(ext_->shadow_dx) : 0;
        const int sh_dy =
            has_text_shadow() ? static_cast<int>(ext_->shadow_dy) : 0;
        const auto pass = [&](const int x0, const int y0, const core::Color &c)
        {
            if (letter <= 0)
            {
                int pen = 0;
                for (int i = 0; i < len;)
                {
                    const GlyphProvider *cur = pick(data[i]);
                    int j = i;
                    while (j < len && pick(data[j]) == cur)
                    {
                        ++j;
                    }
                    if (cur != nullptr)
                    {
                        cur->write(area, data + i, j - i, x0 + pen, y0, c);
                        pen += cur->measure(data + i, j - i).width;
                    }
                    i = j;
                }
                return;
            }
            int pen = 0;
            for (int i = 0; i < len; ++i)
            {
                const GlyphProvider *const p = pick(data[i]);
                if (p != nullptr)
                {
                    p->write(area, data + i, 1, x0 + pen, y0, c);
                    pen += p->measure(data + i, 1).width + letter;
                }
            }
        };

        // the bitmap provider plots without blending, so a translucent
        // pass color (the title's rgba shadow) needs the enable here;
        // the TTF providers self-enable (their own save/restore nests)
        const bool blend =
            color.a() < 255 || (has_text_shadow() && shadow.a() < 255);
        const bool bak = area.is_alpha_enabled();
        if (blend)
        {
            area.enable_alpha(true);
        }
        // shadow first (spacing + double-strike included), then the
        // face; bold adds a +1px second face pass (P-2a double-strike)
        if (has_text_shadow())
        {
            pass(x + sh_dx, y + sh_dy, shadow);
            if (is_bold)
            {
                pass(x + sh_dx + 1, y + sh_dy, shadow);
            }
        }
        pass(x, y, color);
        if (is_bold)
        {
            pass(x + 1, y, color);
        }
        if (blend)
        {
            area.enable_alpha(bak);
        }
    }

    int Widget::text_height() const
    {
        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();
        // line metrics come from the primary provider, or the fallback;
        // line_metrics does not scan the string (no per-glyph loads)
        const text_metrics m = primary != nullptr
                                   ? primary->line_metrics()
                                   : fallback->line_metrics();
        return m.height;
    }

    bool Widget::is_descendant_of(const Widget *ancestor) const
    {
        for (const Widget *w = this; w != nullptr; w = w->parent)
        {
            if (w == ancestor)
            {
                return true;
            }
        }
        return false;
    }

    void Widget::walk_damage(int *out_l, int *out_t, int *out_r, int *out_b)
    {
        take_dirty(out_l, out_t, out_r, out_b);
        // consume at read time: damage reported after this point (a
        // setter firing during the draw) is not wiped by the paint pass
        // and survives into the next frame
        clear_dirty();
        bool pending = false;
        for (size_t i = 0; i < child_count(); ++i)
        {
            if (auto *c = child_at(i))
            {
                c->walk_damage(out_l, out_t, out_r, out_b);
                pending = pending || c->subtree_dirty_;
            }
        }
        subtree_dirty_ = pending;
    }

    void Widget::walk_clear_damage()
    {
        clear_dirty();
        subtree_dirty_ = false;
        for (size_t i = 0; i < child_count(); ++i)
        {
            if (auto *c = child_at(i))
            {
                c->walk_clear_damage();
            }
        }
    }
}
