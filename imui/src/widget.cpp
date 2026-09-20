#include "widget.hpp"

#include <algorithm>
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
            const int r = core::Graphics::inscribed_radius(
                right - left + 1, bottom - top + 1, radius);
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

        // Integer exp(-t) in Q16 (t arrives in Q16): the limit identity
        // e^-t = (1 - t/2^16)^(2^16), seeded with a two-term Taylor and
        // evaluated as 16 renormalized squarings in Q31. Deterministic
        // integer math on every target — no FPU, no float, no runtime
        // table — so desktop and NDS compute identical Gaussian weights.
        int gauss_weight_q16(const int64_t t_q16)
        {
            const int64_t y = (t_q16 + 1) >> 1;  // t / 2^16, in Q31
            int64_t v = (1LL << 31) - y + ((y * y + (1LL << 31)) >> 32);
            for (int i = 0; i < 16; ++i)
            {
                v = (v * v + (1LL << 30)) >> 31;
            }
            return static_cast<int>((v + (1 << 14)) >> 15);
        }

        // Blurred rounded-box coverage field shared by both shadow
        // paths. Mask = the box's exact per-pixel area coverage
        // (rounded_overlap255; straight-edge pixels sit on the box
        // boundary, so only corner bands carry partial values). Filter
        // = the CSS box-shadow Gaussian: sigma = blur/2, per-axis
        // weights exp(-d^2/(2 sigma^2)) sampled at integer offsets,
        // support ceil(1.5 blur) = 3 sigma, renormalized over the
        // truncated window. Two separable prefix-sum passes —
        // horizontal into an int64 line field, vertical through a
        // per-column prefix — with one final normalization by wsum^2.
        // The triangular kernel this replaces died at 1 blur with a
        // linear slope and C0 kinks: the knob's shadows read harder
        // than the browser's. The Gaussian keeps half strength at the
        // contour but the falloff reaches 1.5 blur. Scratch is
        // retained and grown to the high-water mark (no per-frame
        // allocation; paints are single-threaded). Returns the field
        // and its origin/extent; pixels outside it read zero.
        const uint8_t *gaussian_field(const int mleft, const int mtop,
                                      const int mright, const int mbottom,
                                      const int mradius, const int blur,
                                      int &fx0, int &fy0, int &fw, int &fh)
        {
            const int support = blur > 0 ? (3 * blur + 1) / 2 : 0;
            static std::vector<int64_t> wtab;
            if (static_cast<int>(wtab.size()) < support + 1)
            {
                wtab.resize(support + 1);
            }
            for (int d = 0; d <= support; ++d)
            {
                int64_t t_q16 = 0;
                if (blur > 0)
                {
                    t_q16 = (2LL * d * d * 65536 + 1LL * blur * blur / 2) /
                            (1LL * blur * blur);
                }
                wtab[static_cast<size_t>(d)] = gauss_weight_q16(t_q16);
            }
            int64_t wsum = wtab[0];
            for (int d = 1; d <= support; ++d)
            {
                wsum += 2 * wtab[static_cast<size_t>(d)];
            }
            const int64_t wsum2 = wsum * wsum;

            // field region = mask box expanded by the support; the mask
            // is zero past the box, so the clamped prefix reads below
            // are the exact zero-padded convolution
            fx0 = mleft - support;
            fy0 = mtop - support;
            fw = mright - mleft + 2 * support + 1;
            fh = mbottom - mtop + 2 * support + 1;
            const size_t need = static_cast<size_t>(fw) * fh;

            static std::vector<uint8_t> mask;
            if (mask.size() < need)
            {
                mask.resize(need);
            }
            std::fill(mask.begin(), mask.begin() + need, 0);
            for (int y = mtop; y <= mbottom; ++y)
            {
                uint8_t *rowp = &mask[static_cast<size_t>(y - fy0) * fw +
                                      (mleft - fx0)];
                std::fill(rowp, rowp + (mright - mleft + 1), 255);
            }
            const int rc = core::Graphics::inscribed_radius(
                mright - mleft + 1, mbottom - mtop + 1, mradius);
            if (rc > 0)
            {
                for (int y = mtop; y <= mbottom; ++y)
                {
                    if (y >= mtop + rc && y <= mbottom - rc)
                    {
                        continue;  // straight-section row: all 255
                    }
                    uint8_t *rowp = &mask[static_cast<size_t>(y - fy0) * fw];
                    for (int x = mleft; x < mleft + rc; ++x)
                    {
                        rowp[x - fx0] = static_cast<uint8_t>(
                            core::Graphics::rounded_overlap255(
                                mleft, mtop, mright, mbottom, mradius, x, y));
                    }
                    for (int x = mright - rc + 1; x <= mright; ++x)
                    {
                        rowp[x - fx0] = static_cast<uint8_t>(
                            core::Graphics::rounded_overlap255(
                                mleft, mtop, mright, mbottom, mradius, x, y));
                    }
                }
            }

            // pass 1 — horizontal: each output is the weighted window
            // sum of the row's mask, read as prefix differences
            static std::vector<int64_t> hbuf;
            if (hbuf.size() < need)
            {
                hbuf.resize(need);
            }
            static std::vector<int64_t> pfx;
            if (static_cast<int>(pfx.size()) < fw + 1)
            {
                pfx.resize(fw + 1);
            }
            for (int y = 0; y < fh; ++y)
            {
                const uint8_t *mp = &mask[static_cast<size_t>(y) * fw];
                pfx[0] = 0;
                int64_t acc = 0;
                for (int x = 0; x < fw; ++x)
                {
                    pfx[x + 1] = acc += mp[x];
                }
                int64_t *hp = &hbuf[static_cast<size_t>(y) * fw];
                for (int x = 0; x < fw; ++x)
                {
                    int64_t h = 0;
                    for (int d = -support; d <= support; ++d)
                    {
                        const int64_t w =
                            wtab[static_cast<size_t>(d < 0 ? -d : d)];
                        const int hi = x + d + 1;
                        const int lo = x - d;
                        h += w * (pfx[hi < 0 ? 0 : hi > fw ? fw : hi] -
                                  pfx[lo < 0 ? 0 : lo > fw ? fw : lo]);
                    }
                    hp[x] = h;
                }
            }

            // pass 2 — vertical, same prefix scheme on the line field,
            // normalized once at the very end
            static std::vector<uint8_t> cov;
            if (cov.size() < need)
            {
                cov.resize(need);
            }
            static std::vector<int64_t> colp;
            if (static_cast<int>(colp.size()) < fh + 1)
            {
                colp.resize(fh + 1);
            }
            for (int x = 0; x < fw; ++x)
            {
                colp[0] = 0;
                int64_t acc = 0;
                for (int y = 0; y < fh; ++y)
                {
                    colp[y + 1] = acc += hbuf[static_cast<size_t>(y) * fw + x];
                }
                for (int y = 0; y < fh; ++y)
                {
                    int64_t v = 0;
                    for (int d = -support; d <= support; ++d)
                    {
                        const int64_t w =
                            wtab[static_cast<size_t>(d < 0 ? -d : d)];
                        const int hi = y + d + 1;
                        const int lo = y - d;
                        v += w * (colp[hi < 0 ? 0 : hi > fh ? fh : hi] -
                                  colp[lo < 0 ? 0 : lo > fh ? fh : lo]);
                    }
                    cov[static_cast<size_t>(y) * fw + x] =
                        static_cast<uint8_t>((v + wsum2 / 2) / wsum2);
                }
            }
            return cov.data();
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
            const int outer_radius = core::Graphics::inscribed_radius(
                width, height, radius);
            const int inner_radius = std::max(0, outer_radius - border);
            // hole = padding box spread-contracted and offset. The blur
            // field may not cover the whole padding box (offset holes
            // push it away) — pixels past the field read hole 0, the
            // hole at full strength.
            int fx0, fy0, fw, fh;
            const uint8_t *field = gaussian_field(
                left + spread + ox, top + spread + oy,
                right - spread + ox, bottom - spread + oy,
                std::max(0, inner_radius - spread), blur, fx0, fy0, fw, fh);
            for (int y = top; y <= bottom; ++y)
            {
                const auto clip = rounded_shadow_span(left, top, right, bottom,
                                                      inner_radius, y);
                const int start = std::max(left, clip.left - 1);
                const int end = std::min(right, clip.right + 1);
                for (int x = start; x <= end; ++x)
                {
                    // the clip's boundary pixels take their true area overlap
                    // with the padding box: the 1D chord fraction flickers
                    // row by row where the arc crosses pixel boundaries, and
                    // the multiplied shadow read as beads along the rim (the
                    // model500 knob's bottom inset)
                    const int coverage =
                        x < clip.left || x > clip.right
                            ? core::Graphics::rounded_overlap255(left, top, right,
                                                                 bottom,
                                                                 inner_radius,
                                                                 x, y)
                            : 255;
                    if (coverage == 0)
                    {
                        continue;
                    }
                    int hole = 0;
                    if (x >= fx0 && x < fx0 + fw && y >= fy0 && y < fy0 + fh)
                    {
                        hole = field[static_cast<size_t>(y - fy0) * fw +
                                     (x - fx0)];
                    }
                    const int shadow = 255 - hole;
                    area.plot_aa(x, y, (shadow * coverage + 127) / 255, color);
                }
            }
        }

        // Outer (drop) shadow: the blurred silhouette — the exact mirror
        // of the inset's hole. Mask = spread-expanded, offset rounded
        // box as exact area coverage; filter = the same CSS Gaussian
        // through the shared gaussian_field (half strength at the
        // contour, smooth decay out to 1.5 blur). The old approximation
        // (flat half-alpha core + stepped halo rings) put a 2x luminance
        // wall exactly at the contour; the triangular kernel after it
        // still died at 1 blur with a linear slope. plot_aa gates
        // clip/damage and binary depths (half-coverage rule).
        void paint_outer_shadow(core::Graphics &area, const int x0,
                                const int y0, const int x1, const int y1,
                                const int radius, const int blur,
                                const core::Color &color)
        {
            if (x0 > x1 || y0 > y1 || blur <= 0 || color.a() == 0)
            {
                return;
            }
            int fx0, fy0, fw, fh;
            const uint8_t *field =
                gaussian_field(x0, y0, x1, y1, radius, blur,
                               fx0, fy0, fw, fh);
            for (int y = fy0; y < fy0 + fh; ++y)
            {
                for (int x = fx0; x < fx0 + fw; ++x)
                {
                    const int c = field[static_cast<size_t>(y - fy0) * fw +
                                        (x - fx0)];
                    if (c > 0)
                    {
                        area.plot_aa(x, y, c, color);
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
        reset_wrap_cache();
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

        // P-2e: outer shadows may reach past the box (bounded by the
        // surface, see draw_background_shadows_only): phase 1 paints the
        // shadow silhouettes under that escaped clip; phase 2 repaints
        // the full dress + content under the widget's own clip, which
        // cuts the shadow spill at the box edge
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
            radius = core::Graphics::inscribed_radius(w, h, ps->radius_px);
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
        // the shadow pass is bounded by the SURFACE, not the parent clip
        // (P-2e): a browser box-shadow overdraws everything already
        // painted and is covered only by later paint, while a parent-clip
        // bound amputates the spill for shrink-wrapped parents (the
        // model500 knob column is exactly as wide as its knob — the side
        // spill vanished and the corners filled as hard-cut rectangle
        // gradients). Paint order still matches the browser: this phase
        // runs per widget in tree order, so later siblings' backgrounds
        // cover earlier siblings' spill. Coordinates are surface
        // absolute; the guard restores the parent clip on scope exit.
        const auto abs = get_absolute_position();
        auto area = g.clip_surface_safe(abs.x - pl, abs.y - pt,
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
            // kind 2 is 50%: the inscribed radius itself (max request)
            radius = core::Graphics::inscribed_radius(
                s.width, s.height,
                dress_.radius_kind == 2 ? (1 << 30) : dress_.radius_px);
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
        // outer silhouettes (P-2e): painted ONCE per pixel, here in the
        // shadow-only phase — spread-expanded rounded box at the offset,
        // all under the background. The dress phase (full) must NOT
        // repaint them: the field already lies under the face, and a
        // second pass double-composited every box-interior pixel the
        // rounded face leaves uncovered (the corner windows between a
        // 50% face and its box) — the model500 knob's bottom corners
        // read ~2x too dark, a dark wedge flush with the bottom tangent
        // while the spill below the box (cut by the dress clip, single
        // pass) stayed correct. A blurred shadow is the silhouette run
        // through the SAME Gaussian as the inset (see
        // paint_outer_shadow): half strength at the contour, smooth
        // decay out to 1.5 blur. blur==0 keeps the hard silhouette
        // exactly. Wireframe skips shadows (bones).
        const bool shadows =
            area.get_render_mode() == core::Graphics::render_mode::full;
        if (shadows && !full && ext_ != nullptr)
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
                    // real blur, same Gaussian as the inset (see
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
        // An opaque border on a rounded box paints as a filled frame
        // with the face shrunk inside it (the browser border-box shape):
        // the old path filled the face out to the outer arc and stroked
        // the band with 1px AA outlines — the two fringes never tile the
        // diagonal, so face pixels peeked through the ring as a dirty
        // corner staircase. Translucent borders (the knob's rgba
        // outline) keep the stroked path: the face must stay under them
        // out to the outer arc, per the border-box rule. Square corners
        // tile exactly, no frame fill needed.
        // opacity is the depth's own semantics (graphics.cpp tint
        // precedent): a real 8-bit weight at 32bpp, the single bit at
        // 16bpp — needs_blend reads a() < 255, which on the bit depth
        // flags even an opaque border, so the frame fill would never
        // engage there
        const bool frame_opaque = core::Color::per_channel_blend
                                      ? dress_.border_color.a() >= 0xFF
                                      : dress_.border_color.a() != 0;
        const bool opaque_frame =
            radius > 0 && dress_.border_w > 0 && frame_opaque &&
            (background.has_value() || dress_.bg_kind != 0 || grad() != nullptr ||
             rep() != nullptr);
        // the face paints inside the frame: the box inset by the border
        // on every side, radius minus the border (browser border-box)
        const int face_inset = opaque_frame ? dress_.border_w : 0;
        const int face_radius =
            opaque_frame ? std::max(0, radius - dress_.border_w) : radius;
        const int fx2 = s.width - 1 - face_inset;
        const int fy2 = s.height - 1 - face_inset;
        if (opaque_frame)
        {
            area.fill_round_rect_aa(0, 0, s.width - 1, s.height - 1, radius,
                                    dress_.border_color);
        }
        if (background.has_value())
        {
            // S-1: through fill_rect (not fill) so a wireframe view
            // degrades the face to an outline; pixel-identical to fill()
            // in FULL mode. The dialog mask keeps fill() (immune).
            if (face_radius > 0)
            {
                area.fill_round_rect_aa(face_inset, face_inset, fx2, fy2,
                                        face_radius, *background);
            }
            else
            {
                area.fill_rect(face_inset, face_inset, fx2, fy2, *background);
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
                area.fill_conic(face_inset, face_inset, fx2, fy2, g->a, pos,
                                g->col, n, face_radius);
            }
            else if (g->kind == 6)
            {
                int pos[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                const int n = g->b < 4 ? 4 : (g->b > 8 ? 8 : g->b);
                for (int i = 0; i < n; ++i)
                {
                    pos[i] = g->pos[i];
                }
                area.fill_linear_stops(face_inset, face_inset, fx2, fy2, pos,
                                       g->col, n, (g->flags & 1) != 0,
                                       face_radius);
            }
            else
            {
                area.fill_gradient3(face_inset, face_inset, fx2, fy2,
                                    g->col[0], g->col[1], g->a, g->col[2],
                                    (g->flags & 1) != 0, face_radius);
            }
        }
        // radial wins when both kinds are set (contract P-1; the
        // builder sets exactly one, this is direct-setter misuse)
        else if (dress_.bg_kind == 2)
        {
            area.fill_radial(face_inset, face_inset, fx2, fy2,
                             s.width * dress_.bg_ax / 100,
                             s.height * dress_.bg_ay / 100, dress_.bg_from,
                             dress_.bg_p0, dress_.bg_to, dress_.bg_p1,
                             face_radius);
        }
        else if (dress_.bg_kind == 1)
        {
            area.fill_gradient(face_inset, face_inset, fx2, fy2, dress_.bg_from,
                               dress_.bg_to, dress_.bg_ax != 0, face_radius);
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
            area.fill_repeating(face_inset, face_inset, fx2, fy2,
                                (r->flags & 1) != 0, r->period, pos, r->col,
                                n, face_radius);
        }
        if (background_image.has_value())
        {
            area.draw_image(*background_image, 0, 0);
        }
        // border-box outline over the background; a default (alpha-0)
        // color never reaches here through the builder (parse_color
        // rejects transparent), the guard is for direct app misuse.
        // The opaque rounded frame already painted itself above
        // (frame fill + shrunk face); stroking again would darken its
        // own AA edge.
        for (int i = 0; i < dress_.border_w && !opaque_frame; ++i)
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

        // line metrics come from the primary provider, or the fallback;
        // line_metrics does not scan the string (no per-glyph loads)
        const text_metrics m = primary != nullptr
                                   ? primary->line_metrics()
                                   : fallback->line_metrics();

        if (text_wrap() && s.width > 0)
        {
            // wrapped block (H-1): each span is one line aligned by the
            // block's v_align and its own h_align inside the box; the
            // pitch is the declared line height or the provider line
            // metrics; text_offset rides the whole block
            const int pitch = line_height() > 0 ? line_height() : m.height;
            const auto &spans = wrap_spans(s.width);
            const int lines = static_cast<int>(spans.size());
            const int h = lines > 1 ? (lines - 1) * pitch + m.height : m.height;
            int y = 0;
            switch (valign)
            {
            case v_align::top:
                y = m.ascent;
                break;
            case v_align::center:
                y = (s.height - h) / 2 + m.ascent;
                break;
            case v_align::bottom:
                y = s.height - h + m.ascent;
                break;
            }
            y += text_offset_.y;
            for (int i = 0; i < lines; ++i)
            {
                const int off = spans[i].first;
                const int l = spans[i].second;
                int x = 0;
                const int w = advance_of(data + off, l);
                switch (halign)
                {
                case h_align::left:
                    x = 0;
                    break;
                case h_align::center:
                    x = (s.width - w) / 2;
                    break;
                case h_align::right:
                    x = s.width - w;
                    break;
                }
                draw_text_at(area, data + off, l, x + text_offset_.x, y);
                y += pitch;
            }
            return;
        }

    // total advance: split into covered runs; uncovered units add 0
    const int total = text_advance();

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

    // H-1 greedy word wrap (code-contract §2 "Wrapped text"): breaks only
    // at U+0020 and '\n', a line trims its leading/trailing spaces, the
    // break space drops at a wrap, a word wider than the whole width sits
    // alone on its line (the draw clip keeps the edge), and consecutive
    // hard breaks keep their empty lines. Per-unit advances match the
    // draw pen exactly (letter-spacing rides every covered unit).
    static void greedy_wrap(const std::u16string &text,
                            const GlyphProvider *primary,
                            const GlyphProvider *fallback, const int letter,
                            const int width,
                            std::vector<std::pair<int, int>> &out)
    {
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
        const int n = static_cast<int>(text.size());
        const auto w_of = [&](const int i) -> int
        {
            const GlyphProvider *const p = pick(text[i]);
            return p != nullptr
                       ? p->measure(text.data() + i, 1).width + letter
                       : 0;
        };
        const auto emit_line = [&](const int start, int end)
        {
            while (end > start && text[end - 1] == ' ')
            {
                --end;
            }
            out.emplace_back(start, end - start);
        };

        int line_top = 0;
        int p = 0;
        int acc = 0;
        int break_before = -1;
        while (p < n)
        {
            const char16_t ch = text[p];
            if (p == line_top && ch == ' ')
            {
                // leading whitespace collapses off a fresh line
                ++p;
                line_top = p;
                acc = 0;
                break_before = -1;
                continue;
            }
            if (ch == '\n')
            {
                emit_line(line_top, p);
                ++p;
                line_top = p;
                acc = 0;
                break_before = -1;
                continue;
            }
            if (ch == ' ')
            {
                break_before = p;
            }
            acc += w_of(p);
            ++p;
            if (acc > width)
            {
                if (break_before != -1)
                {
                    out.emplace_back(line_top, break_before - line_top);
                    p = break_before + 1;  // the break space drops
                    line_top = p;
                    acc = 0;
                    break_before = -1;
                }
                else
                {
                    // an over-wide word: it sits alone on its own line
                    int next = line_top;
                    while (next < n && text[next] != ' ' && text[next] != '\n')
                    {
                        ++next;
                    }
                    out.emplace_back(line_top, next - line_top);
                    p = next + (next < n ? 1 : 0);
                    line_top = p;
                    acc = 0;
                    break_before = -1;
                }
            }
        }
        if (line_top < n || out.empty())
        {
            emit_line(line_top, n);
        }
    }

    const std::vector<std::pair<int, int>> &
    Widget::wrap_spans(const int width) const
    {
        if (ext_ == nullptr)
        {
            static const std::vector<std::pair<int, int>> kNone{};
            return kNone;
        }
        widget_ext *const e = ext_.get();
        if (e->wrap_enabled != 0 && e->spans_key_w == width &&
            e->spans_key_gen == wrap_gen_)
        {
            return e->spans;
        }
        e->spans.clear();
        const int len = static_cast<int>(text_.size());
        if (e->wrap_enabled == 0 || width <= 0)
        {
            // single-line demand (also the non-wrapping defensive path)
            e->spans.emplace_back(0, len);
        }
        else
        {
            const GlyphProvider *const primary = primary_provider();
            const GlyphProvider *const fallback = bitmap_fallback_.get();
            greedy_wrap(text_, primary, fallback, e->letter_px, width,
                        e->spans);
            if (e->spans.empty())
            {
                e->spans.emplace_back(0, 0);  // empty paragraph: one empty line
            }
        }
        e->spans_key_w = width;
        e->spans_key_gen = wrap_gen_;
        return e->spans;
    }

    int Widget::wrapped_block_height(const int width) const
    {
        const auto &spans = wrap_spans(width);
        if (spans.empty())
        {
            return 0;
        }
        const GlyphProvider *const primary = primary_provider();
        const GlyphProvider *const fallback = bitmap_fallback_.get();
        const text_metrics m = primary != nullptr
                                   ? primary->line_metrics()
                                   : fallback->line_metrics();
        const int pitch = line_height() > 0 ? line_height() : m.height;
        const int lines = static_cast<int>(spans.size());
        return lines > 1 ? (lines - 1) * pitch + m.height : m.height;
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
