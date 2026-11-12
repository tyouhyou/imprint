#include "widget.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "text/utf8.hpp"
#include "scale.hpp"

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
        //
        // X-5: the surface rect in the field's own coordinate frame (the
        // active draw offset, see draw_background_impl). The mask box
        // used to be sized verbatim from the declared geometry, and a
        // design file declares any px: `width:1000000000px;
        // height:1000000000px; box-shadow:0 0 255px #000` asked for
        // ~1e18 bytes and threw bad_alloc out of an init path (the blur
        // cost was quadratic in the declared box too). No drawn pixel
        // sees the mask farther than the Gaussian support away from the
        // surface -- the draw area is always surface-clamped -- so the
        // mask box is clipped to the surface expanded by the support
        // before anything is sized. An empty rect (r < l) means nothing
        // is drawable.
        struct field_clip
        {
            int l = 0;
            int t = 0;
            int r = -1;
            int b = -1;
        };

        const uint8_t *gaussian_field(const field_clip &clip,
                                      const int mleft, const int mtop,
                                      const int mright, const int mbottom,
                                      const int mradius, const int blur,
                                      int &fx0, int &fy0, int &fw, int &fh)
        {
            const int support = blur > 0 ? (3 * blur + 1) / 2 : 0;
            // the mask box clipped to what the surface can reach (X-5):
            // int64 intermediate -- l - support must not wrap for a box
            // parked far off the surface
            const int64_t bleft64 =
                std::max<int64_t>(mleft, static_cast<int64_t>(clip.l) - support);
            const int64_t btop64 =
                std::max<int64_t>(mtop, static_cast<int64_t>(clip.t) - support);
            const int64_t bright64 =
                std::min<int64_t>(mright, static_cast<int64_t>(clip.r) + support);
            const int64_t bbottom64 =
                std::min<int64_t>(mbottom, static_cast<int64_t>(clip.b) + support);
            fx0 = 0;
            fy0 = 0;
            fw = 0;
            fh = 0;
            if (bleft64 > bright64 || btop64 > bbottom64)
            {
                return nullptr;  // nothing the surface can see
            }
            const int bleft = static_cast<int>(bleft64);
            const int btop = static_cast<int>(btop64);
            const int bright = static_cast<int>(bright64);
            const int bbottom = static_cast<int>(bbottom64);
            static std::vector<int64_t> wtab;
            if (static_cast<int>(wtab.size()) < support + 1)
            {
                wtab.resize(static_cast<std::size_t>(support) + 1);
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

            // field region = clipped mask box expanded by the support;
            // the mask is zero past the clipped box, so the clamped
            // prefix reads below are the exact zero-padded convolution
            fx0 = bleft - support;
            fy0 = btop - support;
            fw = bright - bleft + 2 * support + 1;
            fh = bbottom - btop + 2 * support + 1;
            const size_t need = static_cast<size_t>(fw) * static_cast<size_t>(fh);

            static std::vector<uint8_t> mask;
            if (mask.size() < need)
            {
                mask.resize(need);
            }
            std::fill(mask.begin(), mask.begin() + static_cast<std::ptrdiff_t>(need), static_cast<uint8_t>(0));
            // the CLIPPED box's rows carry the ORIGINAL box's exact
            // coverage: a clipped edge is by construction >= support
            // outside the surface, so re-rounding it there (the box
            // would be a different rounded box) would leak a wrong
            // corner into the pixels the clip exists to protect
            const int rc = core::Graphics::inscribed_radius(
                mright - mleft + 1, mbottom - mtop + 1, mradius);
            for (int y = btop; y <= bbottom; ++y)
            {
                uint8_t *rowp = &mask[static_cast<size_t>(y - fy0) * static_cast<size_t>(fw)];
                std::fill(rowp + (bleft - fx0), rowp + (bright - fx0) + 1, static_cast<uint8_t>(255));
                if (rc <= 0 || (y >= mtop + rc && y <= mbottom - rc))
                {
                    continue;  // straight-section row: all 255
                }
                // corner bands: only the cells inside the clipped box
                for (int x = std::max(bleft, mleft);
                     x < std::min(bright + 1, mleft + rc); ++x)
                {
                    rowp[x - fx0] = static_cast<uint8_t>(
                        core::Graphics::rounded_overlap255(
                            mleft, mtop, mright, mbottom, mradius, x, y));
                }
                for (int x = std::max(bleft, mright - rc + 1);
                     x <= std::min(bright, mright); ++x)
                {
                    rowp[x - fx0] = static_cast<uint8_t>(
                        core::Graphics::rounded_overlap255(
                            mleft, mtop, mright, mbottom, mradius, x, y));
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
                pfx.resize(static_cast<size_t>(fw) + 1);
            }
            for (int y = 0; y < fh; ++y)
            {
                const uint8_t *mp = &mask[static_cast<size_t>(y) * static_cast<size_t>(fw)];
                pfx[0] = 0;
                int64_t acc = 0;
                for (int x = 0; x < fw; ++x)
                {
                    pfx[static_cast<size_t>(x) + 1] = acc += mp[x];
                }
                int64_t *hp = &hbuf[static_cast<size_t>(y) * static_cast<size_t>(fw)];
                for (int x = 0; x < fw; ++x)
                {
                    int64_t h = 0;
                    // X-5 (review): radius d contributes two mirrored
                    // windows, +d and -d; added in that order their
                    // terms cancel pairwise, but only after the
                    // accumulator has dipped by the sum of their
                    // magnitudes -- at blur 255 that dip is -1.1e19,
                    // past int64 (signed-overflow UB, reported by the
                    // sanitizer battery). Reading both mirrors of one
                    // radius together keeps the pair, and therefore
                    // every running sum, non-negative: the pair IS the
                    // mask at x - d and x + d, so the value does not
                    // change and the sum stays bounded by the
                    // convolution, 255 * wsum2.
                    for (int d = 0; d <= support; ++d)
                    {
                        const int64_t w = wtab[static_cast<size_t>(d)];
                        const int hi_p = x + d + 1;
                        const int lo_p = x - d;
                        const int hi_m = x - d + 1;
                        const int lo_m = x + d;
                        const int64_t p =
                            pfx[static_cast<size_t>(hi_p < 0 ? 0 : hi_p > fw ? fw
                                                                             : hi_p)] -
                            pfx[static_cast<size_t>(lo_p < 0 ? 0 : lo_p > fw ? fw
                                                                             : lo_p)];
                        const int64_t m =
                            d > 0 ? pfx[static_cast<size_t>(hi_m < 0 ? 0 : hi_m > fw ? fw
                                                                                     : hi_m)] -
                                        pfx[static_cast<size_t>(lo_m < 0 ? 0 : lo_m > fw ? fw
                                                                                         : lo_m)]
                                  : 0;
                        h += w * (p + m);
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
                colp.resize(static_cast<size_t>(fh) + 1);
            }
            for (int x = 0; x < fw; ++x)
            {
                colp[0] = 0;
                int64_t acc = 0;
                for (int y = 0; y < fh; ++y)
                {
                    colp[static_cast<size_t>(y) + 1] = acc += hbuf[static_cast<size_t>(y) * static_cast<size_t>(fw) + static_cast<size_t>(x)];
                }
                for (int y = 0; y < fh; ++y)
                {
                    int64_t v = 0;
                    // mirrored pairs as in pass 1 (same value, no dip)
                    for (int d = 0; d <= support; ++d)
                    {
                        const int64_t w = wtab[static_cast<size_t>(d)];
                        const int hi_p = y + d + 1;
                        const int lo_p = y - d;
                        const int hi_m = y - d + 1;
                        const int lo_m = y + d;
                        const int64_t p =
                            colp[static_cast<size_t>(hi_p < 0 ? 0 : hi_p > fh ? fh
                                                                              : hi_p)] -
                            colp[static_cast<size_t>(lo_p < 0 ? 0 : lo_p > fh ? fh
                                                                              : lo_p)];
                        const int64_t m =
                            d > 0 ? colp[static_cast<size_t>(hi_m < 0 ? 0 : hi_m > fh ? fh
                                                                                      : hi_m)] -
                                        colp[static_cast<size_t>(lo_m < 0 ? 0 : lo_m > fh ? fh
                                                                                          : lo_m)]
                                  : 0;
                        v += w * (p + m);
                    }
                    cov[static_cast<size_t>(y) * static_cast<size_t>(fw) + static_cast<size_t>(x)] =
                        static_cast<uint8_t>((v + wsum2 / 2) / wsum2);
                }
            }
            return cov.data();
        }

        void paint_inset_shadow(core::Graphics &area, const field_clip &surface,
                                int width, int height,
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
                surface, left + spread + ox, top + spread + oy,
                right - spread + ox, bottom - spread + oy,
                std::max(0, inner_radius - spread), blur, fx0, fy0, fw, fh);
            // X-5: the declared box can be far larger than the surface (a
            // design file declares any px; the field above is clipped for
            // the same reason). A pixel outside the surface rect can never
            // be plotted -- plot_aa clips to the draw area, which is
            // surface-bounded by construction -- so the ring walk is
            // clamped to it. Unclamped, a 1e9px box cost width*height
            // no-op plot_aa calls (~1e18) inside one frame: a hang, not
            // just a large allocation.
            const int ry1 = std::min(bottom, surface.b);
            for (int y = std::max(top, surface.t); y <= ry1; ++y)
            {
                const auto clip = rounded_shadow_span(left, top, right, bottom,
                                                      inner_radius, y);
                const int start = std::max(left, clip.left - 1);
                const int end = std::min(right, clip.right + 1);
                const int rx1 = std::min(end, surface.r);
                for (int x = std::max(start, surface.l); x <= rx1; ++x)
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
                        hole = field[static_cast<size_t>(y - fy0) * static_cast<size_t>(fw) +
                                     static_cast<size_t>(x - fx0)];
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
        void paint_outer_shadow(core::Graphics &area, const field_clip &surface,
                                const int x0,
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
                gaussian_field(surface, x0, y0, x1, y1, radius, blur,
                               fx0, fy0, fw, fh);
            if (fw <= 0 || fh <= 0)
            {
                return;  // the surface sees no part of the silhouette
            }
            for (int y = fy0; y < fy0 + fh; ++y)
            {
                for (int x = fx0; x < fx0 + fw; ++x)
                {
                    const int c = field[static_cast<size_t>(y - fy0) * static_cast<size_t>(fw) +
                                        static_cast<size_t>(x - fx0)];
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

    bool has_font_family()
    {
        return font_family_set();
    }

    const TtfFamily &font_family()
    {
        return font_family_state();
    }

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

    void Widget::anchor_font_family(const TtfFamily &family)
    {
        // anchor copy only (the T-3 borrow rule's keeper half), in its own
        // sidecar slot: the font-size setters reset ext_->font_family, so
        // the run keeper must not share that slot — a widget-level
        // set_font_size must never end a run provider's keeper while the
        // installed runs still borrow it. Rendering state is untouched.
        ensure_ext();
        ext_->run_family = family;
    }
#endif

    Widget::Widget()
        : bitmap_fallback_(fallback_singleton())
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
            clamp_text_runs();
            return;
        }
        text_ = sanitize_utf8(text);  // stored text is always valid UTF-8
        clamp_text_runs();
    }

    // T-3: runs are byte offsets into text_ — after the text changed, a
    // run left past the new end drops, one crossing the end truncates,
    // and an edge that would land inside a multi-byte sequence snaps
    // out to the boundary (code-contract §2 "Styled text runs"). The
    // uint16_t fields bound a run to 65535: a run whose snapped offset
    // no longer fits drops (the field cannot address it), and a length
    // over the cap steps back to the nearest boundary.
    void Widget::clamp_text_runs()
    {
        if (ext_ == nullptr || ext_->runs.empty())
        {
            return;
        }
        const size_t n = text_.size();
        std::vector<text_style_run> kept;
        kept.reserve(ext_->runs.size());
        for (const text_style_run &r : ext_->runs)
        {
            const size_t off =
                snap_boundary(text_, static_cast<size_t>(r.offset));
            if (off >= n || off > 0xFFFF)
            {
                continue;  // past the new end / past the field: dropped
            }
            // the snapped-off end: the run's own end boundary, then the
            // field cap — whichever is smaller
            size_t end = snap_boundary(
                text_, off + static_cast<size_t>(r.len));
            const size_t cap = off + 0xFFFF < n ? off + 0xFFFF : n;
            if (end > cap)
            {
                end = cap;
                while (end > off && end < n &&
                       (static_cast<unsigned char>(text_[end]) & 0xC0) == 0x80)
                {
                    --end;  // a capped edge steps back to the boundary
                }
            }
            text_style_run c = r;
            c.offset = static_cast<uint16_t>(off);
            c.len = static_cast<uint16_t>(end - off);  // <= 0xFFFF by the cap
            kept.push_back(c);
        }
        ext_->runs = std::move(kept);
    }

    // T-3 (code-contract §2 "Styled text runs"): the stored form is the
    // normalized list — sorted by offset (ties keep set order, so the
    // first-set run wins an overlap), zero-length entries dropped
    void Widget::set_text_runs(std::vector<text_style_run> runs)
    {
        if (runs.empty())
        {
            // a bare widget stays allocation-free on the clear path
            if (ext_ != nullptr)
            {
                ext_->runs.clear();
            }
        }
        else
        {
            ensure_ext();
            runs.erase(std::remove_if(runs.begin(), runs.end(),
                                      [](const text_style_run &r)
                                      {
                                          return r.len == 0;
                                      }),
                       runs.end());
            std::stable_sort(runs.begin(), runs.end(),
                             [](const text_style_run &a,
                                const text_style_run &b)
                             {
                                 return a.offset < b.offset;
                             });
            ext_->runs = std::move(runs);
        }
        // the stored form is normalized (T-3): edges snap onto
        // code-point boundaries, past-the-end/past-the-field runs drop,
        // a run crossing the end truncates -- the same clamp set_text
        // applies
        clamp_text_runs();
        reset_wrap_cache();
        mark_dirty();
        mark_layout_dirty();
    }

    void Widget::draw(core::Graphics &g) const
    {
        if (!visible || size.width <= 0 || size.height <= 0)
        {
            return;
        }

        // damage culling: outside the reported region the whole subtree
        // is by definition invisible, skip the rasterizer entirely.
        // A-10: the shadow pass clips to the whole surface, so a
        // descendant's outer shadow can legitimately land outside THIS
        // widget's bounds -- the culling rect grows by the subtree's
        // shadow reach, which is >= each own pad component by construction
        if (g.damage_on())
        {
            const auto abs = get_absolute_position();
            const int reach = subtree_shadow_reach_;
            if (!g.damage_intersects(abs.x - reach, abs.y - reach,
                                     size.width + 2 * reach,
                                     size.height + 2 * reach))
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
        auto side = [&](const int i)
        {
            int v = ps->off[i];
            if ((ps->off_pct & (1U << i)) != 0U)
            {
                // X-9: the side's share rides the 64-bit helper
                v = detail::pct(i % 2 == 0 ? s.width : s.height, v);
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
                const int ox = ps->rot_ox_pct != 0 ? detail::pct(w, ps->rot_ox)
                                                   : ps->rot_ox;
                const int oy = ps->rot_oy_pct != 0 ? detail::pct(h, ps->rot_oy)
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
        // X-5: the surface rect in THIS frame, for the shadow/inset clip
        // (see gaussian_field, paint_inset_shadow). Both are bounded by
        // it because a design file declares any box size (a 1e9px box
        // asked the field for ~1e18 bytes, and the inset ring walk for
        // ~1e18 plot_aa calls).
        //
        // Drawing coordinates are relative to the ACTIVE DRAW OFFSET,
        // which is exactly abs_pos - (sh_dx, sh_dy) here. The clip chain
        // composes each level's origin additively from the surface origin
        // -- clip_safe's requested origin (the "local origin is the
        // requested box" rule) plus the parent's offset -- so the offset
        // in the dress phase (sh_dx == sh_dy == 0) is the widget's own
        // absolute position, and the shadow phase shifts it by the padded
        // box it just clipped to (draw_background_shadows_only). The same
        // invariant is what makes clip_surface_safe's absolute surface
        // coordinates sound (P-2e); a divergence here would break that
        // contract too, not just this clip.
        const auto abs_pos = get_absolute_position();
        const auto surf = area.size();
        // saturating int64 -> int: a saturated bound only ever widens the
        // clip (a superset of the surface still renders identically)
        const auto frame_coord = [](const int64_t v)
        {
            return static_cast<int>(std::clamp(
                v, static_cast<int64_t>(INT32_MIN),
                static_cast<int64_t>(INT32_MAX)));
        };
        const int64_t fox = static_cast<int64_t>(abs_pos.x) - sh_dx;
        const int64_t foy = static_cast<int64_t>(abs_pos.y) - sh_dy;
        field_clip surface;
        surface.l = frame_coord(-fox);
        surface.t = frame_coord(-foy);
        surface.r = frame_coord(static_cast<int64_t>(surf.width) - 1 - fox);
        surface.b = frame_coord(static_cast<int64_t>(surf.height) - 1 - foy);
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
        auto needs_blend = [](const core::Color &c)
        { return c.a() < 255; };
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
                    paint_outer_shadow(area, surface, x0, y0, x1, y1,
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
                             detail::pct(s.width, dress_.bg_ax),
                             detail::pct(s.height, dress_.bg_ay), dress_.bg_from,
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
                paint_inset_shadow(area, surface, s.width, s.height, radius,
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

        const auto s = get_size();
        const char *const data = text_.data();
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
                const int off = spans[static_cast<size_t>(i)].first;
                const int l = spans[static_cast<size_t>(i)].second;
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

    // T-1 CJK break set (code-contract §2 "Wrapped text"): radicals, CJK
    // punctuation, kana, ideographs, compatibility ideographs, fullwidth
    // forms.
    static bool is_cjk_break_unit(const char32_t ch)
    {
        return (ch >= 0x2E80 && ch <= 0x9FFF) ||
               (ch >= 0xF900 && ch <= 0xFAFF) ||
               (ch >= 0xFF00 && ch <= 0xFFEF);
    }

    // kinsoku-lite pinned sets (T-1): exactly the strings the contract
    // pins — do not edit without the contract amendment
    const char32_t kLineHeadForbidden[] =
        U"、。，．：；？！）］｝〉》」』”’…‥";
    const char32_t kLineTailForbidden[] = U"（［｛〈《「『“‘";

    static bool in_pinned_set(const char32_t ch, const char32_t *set)
    {
        for (int i = 0; set[i] != U'\0'; ++i)
        {
            if (set[i] == ch)
            {
                return true;
            }
        }
        return false;
    }

    // T-3: the first run (offset-sorted, ties keep set order) covering
    // `idx`, or nullptr — the shared lookup for measure, wrap, and draw
    static const text_style_run *covering_run(
        const std::vector<text_style_run> &runs, const int idx)
    {
        for (const text_style_run &r : runs)
        {
            if (r.offset > idx)
            {
                break;  // sorted by offset: no later run can cover
            }
            if (idx < static_cast<int>(r.offset) + r.len)
            {
                return &r;
            }
        }
        return nullptr;
    }

    // H-1 greedy word wrap (code-contract §2 "Wrapped text"): breaks at
    // U+0020, '\n', and (T-1) between adjacent CJK break-set code points,
    // walked one code point at a time over the stored UTF-8 bytes; a line
    // trims its leading/trailing spaces, the break space drops at a wrap,
    // a run with no break candidate wider than the whole width sits alone
    // on its line (the draw clip keeps the edge), and consecutive hard
    // breaks keep their empty lines. Per-code-point advances match the
    // draw pen exactly (letter-spacing rides every covered code point;
    // T-3: a covering style run's provider overrides the primary/fallback
    // pick). Spans are [byte offset, byte length) pairs.
    static void greedy_wrap(const std::string &text,
                            const GlyphProvider *primary,
                            const GlyphProvider *fallback, const int letter,
                            const int width,
                            const std::vector<text_style_run> *runs,
                            std::vector<std::pair<int, int>> &out)
    {
        const auto pick = [&](const char32_t cp) -> const GlyphProvider *
        {
            if (primary != nullptr && primary->covers(cp))
            {
                return primary;
            }
            if (fallback->covers(cp))
            {
                return fallback;
            }
            return nullptr;
        };
        const char *const data = text.data();
        const int n = static_cast<int>(text.size());
        const char *const dend = data + n;
        const auto w_of = [&](const int i, const char32_t cp) -> int
        {
            const GlyphProvider *p = nullptr;
            if (runs != nullptr)
            {
                if (const text_style_run *const r = covering_run(*runs, i);
                    r != nullptr && r->provider != nullptr)
                {
                    p = r->provider;
                }
            }
            if (p == nullptr)
            {
                p = pick(cp);
            }
            return p != nullptr ? p->measure(&cp, 1).width + letter : 0;
        };
        const auto emit_line = [&](const int start, int end)
        {
            while (end > start && text[static_cast<std::size_t>(end) - 1] == ' ')
            {
                --end;
            }
            out.emplace_back(start, end - start);
        };
        // kinsoku-lite single-step retraction (T-1): a CJK break at the
        // byte offset `k` that would start the next line on a
        // head-forbidden code point or end the line on a tail-forbidden
        // one retracts one code point back. One step only — no chains —
        // and a retraction that cannot produce a shorter non-empty line
        // leaves the break where it is.
        const auto kinsoku_retract = [&](const int line_top,
                                         const int k) -> int
        {
            const char *hp = data + k;
            const bool head_bad =
                k < n && in_pinned_set(decode_utf8_next(hp, dend),
                                       kLineHeadForbidden);
            const char *tp = data + k;
            const bool tail_bad =
                k > 0 &&
                in_pinned_set(decode_utf8_prev(tp, data), kLineTailForbidden);
            if (!head_bad && !tail_bad)
            {
                return k;
            }
            // one code point back from k: the start of the unit that
            // ends the line
            const char *bp = data + k;
            (void)decode_utf8_prev(bp, data);
            const int k2 = static_cast<int>(bp - data);
            if (k2 <= line_top)
            {
                return k;  // no room: the break stays
            }
            return k2;
        };

        int line_top = 0;
        int p = 0;  // byte cursor: the next code point
        int acc = 0;
        int break_before = -1;  // U+0020: the break space drops at the wrap
        int cjk_before = -1;    // T-1 CJK boundary: the break keeps both units
        bool prev_cjk = false;  // the code point before p sat in the break set
        while (p < n)
        {
            const char *pp = data + p;
            const char32_t cp = decode_utf8_next(pp, dend);
            const int len = static_cast<int>(pp - (data + p));
            const bool cjk = is_cjk_break_unit(cp);
            if (prev_cjk && cjk)
            {
                // T-1: the boundary between two adjacent CJK units — the
                // break keeps both
                cjk_before = p;
            }
            if (p == line_top && cp == U' ')
            {
                // leading whitespace collapses off a fresh line (U+3000 is
                // an ordinary CJK unit, never this path)
                p += len;
                line_top = p;
                acc = 0;
                break_before = -1;
                cjk_before = -1;
                prev_cjk = false;
                continue;
            }
            if (cp == U'\n')
            {
                emit_line(line_top, p);
                p += len;
                line_top = p;
                acc = 0;
                break_before = -1;
                cjk_before = -1;
                prev_cjk = false;
                continue;
            }
            if (cp == U' ')
            {
                break_before = p;
            }
            acc += w_of(p, cp);
            p += len;
            if (acc > width)
            {
                // greedy: the later candidate fills the line further; a
                // space break drops the space, a CJK boundary breaks
                // between the units (kinsoku-lite may retract one). Only
                // candidates recorded before the overflowing unit count —
                // a boundary past it would carry the unit that did not fit
                if (cjk_before != -1 &&
                    (break_before == -1 || cjk_before > break_before))
                {
                    const int k = kinsoku_retract(line_top, cjk_before);
                    emit_line(line_top, k);
                    p = k;
                    line_top = p;
                    acc = 0;
                    break_before = -1;
                    cjk_before = -1;
                    prev_cjk = false;
                }
                else if (break_before != -1)
                {
                    out.emplace_back(line_top, break_before - line_top);
                    p = break_before + 1;  // the break space drops
                    line_top = p;
                    acc = 0;
                    break_before = -1;
                    cjk_before = -1;
                    prev_cjk = false;
                }
                else
                {
                    // an over-wide run: it ends at the next space, hard
                    // break, or (T-1) the first CJK boundary after the
                    // line start, and sits alone on its line
                    int next = line_top;
                    bool pc = false;  // the previous unit was CJK
                    bool first = true;
                    bool cjk_stop = false;
                    while (next < n)
                    {
                        const char *np = data + next;
                        const char32_t ncp = decode_utf8_next(np, dend);
                        const int unit_start = next;
                        const bool ncjk = is_cjk_break_unit(ncp);
                        next = static_cast<int>(np - data);
                        if (!first && (ncp == U' ' || ncp == U'\n'))
                        {
                            next = unit_start;
                            break;
                        }
                        if (!first && pc && ncjk)
                        {
                            cjk_stop = true;
                            next = unit_start;
                            break;
                        }
                        first = false;
                        pc = ncjk;
                    }
                    if (cjk_stop)
                    {
                        // CJK-boundary stop: nothing drops, kinsoku applies
                        const int k = kinsoku_retract(line_top, next);
                        emit_line(line_top, k);
                        p = k;
                    }
                    else
                    {
                        out.emplace_back(line_top, next - line_top);
                        p = next + (next < n ? 1 : 0);  // the break space drops
                    }
                    line_top = p;
                    acc = 0;
                    break_before = -1;
                    cjk_before = -1;
                    prev_cjk = false;
                }
            }
            else
            {
                prev_cjk = cjk;
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
                        &e->runs, e->spans);
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
        // clamp at the cache boundary too: a provider reporting a negative
        // advance would store it and collide with the -1 "not computed"
        // sentinel (a permanent cache miss on every later call). The
        // providers shipped here clamp already; this keeps the invariant
        // true for any GlyphProvider (W-6)
        if (advance_cache_ < 0)
        {
            advance_cache_ = 0;
        }
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

    // T-3: the provider for the code point at byte offset `i` of `data` —
    // the first covering style run's provider wins over the primary /
    // fallback chain. Runs apply only when `data` addresses this
    // widget's own text_ (advance_of/draw_text_at stay generic helpers);
    // a covering run without a provider leaves the base chain in charge.
    // nullptr means "not covered" — the pen keeps its position.
    const GlyphProvider *Widget::pick_provider(const char *data,
                                               const int i) const
    {
        if (ext_ != nullptr && !ext_->runs.empty() && !text_.empty())
        {
            const char *const begin = text_.data();
            if (data >= begin && data <= begin + text_.size())
            {
                const int idx = static_cast<int>(data - begin) + i;
                if (const text_style_run *const r =
                        covering_run(ext_->runs, idx);
                    r != nullptr && r->provider != nullptr)
                {
                    return r->provider;
                }
            }
        }
        const char *p = data + i;
        const char32_t cp = decode_utf8_next(p);
        const GlyphProvider *const primary = primary_provider();
        if (primary != nullptr && primary->covers(cp))
        {
            return primary;
        }
        if (bitmap_fallback_->covers(cp))
        {
            return bitmap_fallback_.get();
        }
        return nullptr;
    }

    int Widget::advance_of(const char *data, const int len) const
    {
        if (len <= 0)
        {
            return 0;
        }
        // letter-spacing (P-2a): per covered code point, trailing code
        // point included. The zero-spacing grouped path below stays, so
        // plain text keeps one measure call per provider run
        const int letter =
            (ext_ != nullptr && ext_->has_text != 0) ? ext_->letter_px : 0;
        const char *const end = data + len;
        // reused decode scratch: a warm call never allocates (the §8
        // warm-draw rule)
        thread_local std::u32string scratch;
        int total = 0;
        const char *p = data;
        while (p < end)
        {
            // decode one code point, resolve its provider (T-3: style
            // runs first); nullptr means "not covered" — zero advance
            const int start = static_cast<int>(p - data);
            const char32_t first = decode_utf8_next(p, end);
            const GlyphProvider *cur = pick_provider(data, start);
            if (cur == nullptr)
            {
                continue;  // the pen keeps its position
            }
            if (letter > 0)
            {
                total += cur->measure(&first, 1).width + letter;
                continue;
            }
            // group the following code points that share this provider
            // into one measure call
            scratch.clear();
            scratch.push_back(first);
            while (p < end)
            {
                const int next_idx = static_cast<int>(p - data);
                const char *q = p;
                const char32_t cp = decode_utf8_next(q, end);
                if (pick_provider(data, next_idx) != cur)
                {
                    break;
                }
                scratch.push_back(cp);
                p = q;
            }
            total += cur->measure(scratch.data(),
                                  static_cast<int>(scratch.size()))
                         .width;
        }
        return total;
    }

    void Widget::draw_text_at(core::Graphics &area, const char *data, const int len,
                              const int x, const int y) const
    {
        draw_text_at(area, data, len, x, y, effective_text_color());
    }

    void Widget::draw_text_at(core::Graphics &area, const char *data, const int len,
                              const int x, const int y, const core::Color &color) const
    {
        // one run of glyphs at (x0, y0): the provider-run fast path
        // when tracking is off, per-code-point pen (advance + spacing)
        // when on. Uncovered code points keep the pen position either way
        const int letter =
            (ext_ != nullptr && ext_->has_text != 0) ? ext_->letter_px : 0;
        const bool is_bold = bold();
        const core::Color shadow =
            has_text_shadow() ? ext_->shadow_color : core::Color{};
        const int sh_dx =
            has_text_shadow() ? static_cast<int>(ext_->shadow_dx) : 0;
        const int sh_dy =
            has_text_shadow() ? static_cast<int>(ext_->shadow_dy) : 0;
        const char *const end = data + len;
        // reused decode scratch: a warm call never allocates (the §8
        // warm-draw rule)
        thread_local std::u32string scratch;
        const auto pass = [&](const int i0, const int i1, const int x0,
                              const int y0, const core::Color &c)
        {
            // code points in the byte span [i0, i1) at (x0, y0)
            const char *const pend = data + i1;
            const char *p = data + i0;
            if (letter <= 0)
            {
                int pen = 0;
                while (p < pend)
                {
                    const int start = static_cast<int>(p - data);
                    const char32_t first = decode_utf8_next(p, pend);
                    const GlyphProvider *cur = pick_provider(data, start);
                    if (cur == nullptr)
                    {
                        continue;  // the pen keeps its position
                    }
                    // group the following code points that share this
                    // provider into one write call
                    scratch.clear();
                    scratch.push_back(first);
                    while (p < pend)
                    {
                        const int next_idx = static_cast<int>(p - data);
                        const char *q = p;
                        const char32_t cp = decode_utf8_next(q, pend);
                        if (pick_provider(data, next_idx) != cur)
                        {
                            break;
                        }
                        scratch.push_back(cp);
                        p = q;
                    }
                    cur->write(area, scratch.data(),
                               static_cast<int>(scratch.size()), x0 + pen, y0, c);
                    pen += cur->measure(scratch.data(),
                                        static_cast<int>(scratch.size()))
                               .width;
                }
                return;
            }
            int pen = 0;
            while (p < pend)
            {
                const int start = static_cast<int>(p - data);
                const char32_t cp = decode_utf8_next(p, pend);
                const GlyphProvider *const pr = pick_provider(data, start);
                if (pr != nullptr)
                {
                    pr->write(area, &cp, 1, x0 + pen, y0, c);
                    pen += pr->measure(&cp, 1).width + letter;
                }
            }
        };

        // T-3: [0, len) splits at style-run boundaries — each part draws
        // with its run's color and bold (the provider rides the
        // per-code-point pick); uncovered code points keep the base
        // style. Runs never change the pen math, only what a part looks
        // like. Parts end on code-point boundaries by construction (the
        // walk steps whole sequences).
        const bool has_runs = ext_ != nullptr && !ext_->runs.empty();
        const auto run_at = [&](const int i) -> const text_style_run *
        {
            if (!has_runs)
            {
                return nullptr;
            }
            const char *const begin = text_.data();
            if (data < begin || data > begin + text_.size())
            {
                return nullptr;  // foreign data: no run context applies
            }
            return covering_run(ext_->runs, static_cast<int>(data - begin) + i);
        };
        const auto for_each_part = [&](const auto &fn)
        {
            int i = 0;
            while (i < len)
            {
                const text_style_run *const r = run_at(i);
                int j = i;
                const char *pp = data + i;
                while (pp < end)
                {
                    const char *q = pp;
                    (void)decode_utf8_next(q, end);
                    const int next_i = static_cast<int>(q - data);
                    if (next_i < len && run_at(next_i) != r)
                    {
                        j = next_i;  // the next code point starts a new part
                        break;
                    }
                    j = next_i;
                    pp = q;
                }
                core::Color c = color;
                bool b = is_bold;
                if (r != nullptr)
                {
                    if (r->color.has_value())
                    {
                        c = *r->color;
                    }
                    b = is_bold || r->bold;
                }
                // the part's pen starts where the preceding code points
                // ended: the prefix advance uses the same picks and
                // batching as the per-part pen math, so the seams stay
                // exact
                fn(i, j, x + advance_of(data, i), c, b);
                i = j;
            }
        };

        // the bitmap provider plots without blending, so a translucent
        // pass color (the title's rgba shadow, a translucent run color)
        // needs the enable here; the TTF providers self-enable (their
        // own save/restore nests)
        bool run_alpha = false;
        if (has_runs)
        {
            for (const text_style_run &r : ext_->runs)
            {
                if (r.color.has_value() && r.color->a() < 255)
                {
                    run_alpha = true;
                    break;
                }
            }
        }
        const bool blend = color.a() < 255 || run_alpha ||
                           (has_text_shadow() && shadow.a() < 255);
        const bool bak = area.is_alpha_enabled();
        if (blend)
        {
            area.enable_alpha(true);
        }
        // shadow first (spacing + double-strike included), then the
        // face; bold adds a +1px second pass (P-2a double-strike, per
        // part for run bold). Every part's shadow lands before any face
        // pass, so the paint order matches the no-runs path exactly
        if (has_text_shadow())
        {
            for_each_part([&](const int i0, const int i1, const int px,
                              const core::Color &, const bool b)
                          {
                pass(i0, i1, px + sh_dx, y + sh_dy, shadow);
                if (b)
                {
                    pass(i0, i1, px + sh_dx + 1, y + sh_dy, shadow);
                } });
        }
        for_each_part([&](const int i0, const int i1, const int px,
                          const core::Color &c, const bool b)
                      {
            pass(i0, i1, px, y, c);
            if (b)
            {
                pass(i0, i1, px + 1, y, c);
            } });
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
