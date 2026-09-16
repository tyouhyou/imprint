#include "widget.hpp"

#include <algorithm>

#include "text/utf8.hpp"

namespace zb::ui
{
    namespace
    {
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
            if (!g.damage_intersects(abs.x, abs.y, size.width, size.height))
            {
                return;
            }
        }

        // clip_safe restricts g's draw area to this widget and restores it
        // on scope exit; no allocation per widget per frame
        auto area = g.clip_safe(position.x, position.y, size.width, size.height);
        if (!area)
        {
            return;
        }

        draw_background(g);
        draw_at(g);
    }

    void Widget::draw_background(core::Graphics &area) const
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
        // shadow colors blend too (P-2e); inset bands always blend
        // (the falloff manufactures translucency even from opaque
        // base colors — without this the radius-0 aliased outlines
        // overwrite raw), outer silhouettes blend when translucent
        // or blurred (soft bands)
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
        // shadow feathers over its blur radius: the core dims as
        // 2/(blur+2) and up to `blur` outlines keep halving outward
        // (capped where the steps turn invisible), so a large blur fades
        // before the widget edge instead of ending in a hard wall —
        // the model500 knob's square bottom was the full-alpha core of
        // `0 3px 6px` with only two halo steps. blur==0 keeps the hard
        // silhouette exactly. Wireframe skips shadows (bones).
        const bool shadows =
            area.get_render_mode() == core::Graphics::render_mode::full;
        if (shadows && ext_ != nullptr)
        {
            for (int i = 0; i < ext_->n_sh_out && i < 2; ++i)
            {
                const shadow_spec &sh = ext_->sh_out[i];
                const int x0 = sh.ox - sh.spread;
                const int y0 = sh.oy - sh.spread;
                const int x1 = s.width - 1 + sh.ox + sh.spread;
                const int y1 = s.height - 1 + sh.oy + sh.spread;
                if (sh.blur > 0 && sh.c.a() > 0)
                {
                    // feathered core + halving halo over the blur radius;
                    // the core dims as 2/(blur+2) (rounded), so a large
                    // blur fades before the widget edge instead of ending
                    // in a hard wall. Binary depths apply the same half
                    // rule to the dimmed core (kept only while it still
                    // covers half the base alpha); the first halo stays
                    // solid and the rest drop, as before.
                    const int base_a = static_cast<int>(sh.c.a());
                    const int core_a =
                        (base_a * 2 + (sh.blur + 2) / 2) / (sh.blur + 2);
                    core::Color core = sh.c;
                    if constexpr (core::Color::per_channel_blend)
                    {
                        core.set_a(static_cast<uint8_t>(core_a));
                    }
                    else
                    {
                        core.set_a(2 * core_a >= base_a ? sh.c.a() : 0);
                    }
                    area.fill_round_rect_aa(x0, y0, x1, y1,
                                            radius + sh.spread, core);
                    const int bands = sh.blur < 6 ? sh.blur : 6;
                    int step_a = static_cast<int>(core.a());
                    for (int k = 1; k <= bands; ++k)
                    {
                        core::Color soft = sh.c;
                        if constexpr (core::Color::per_channel_blend)
                        {
                            step_a /= 2;
                            soft.set_a(static_cast<uint8_t>(step_a));
                        }
                        else if (k > 1)
                        {
                            soft.set_a(0);
                        }
                        area.draw_round_rect_aa(x0 - k, y0 - k, x1 + k, y1 + k,
                                                radius + sh.spread + k, soft);
                        if (step_a == 0 && core::Color::per_channel_blend)
                        {
                            break;
                        }
                    }
                }
                else
                {
                    area.fill_round_rect_aa(x0, y0, x1, y1,
                                            radius + sh.spread, sh.c);
                }
            }
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
        // inset shadows (P-2e): bands hug the sides picked by the
        // offset sign (zero offset = neither side — the blur spill on
        // the centered axis is dropped so circles keep their round
        // silhouette; the all-sides (0,0) case rides shrinking
        // outlines, radius-aware), alpha falling off inward; single
        // sides ride chord-clipped lines
        if (shadows && ext_ != nullptr)
        {
            for (int k = 0; k < ext_->n_sh_in && k < 2; ++k)
            {
                const shadow_spec &sh = ext_->sh_in[k];
                const int bands = sh.blur <= 0 ? 1 : sh.blur;
                const int base = dress_.border_w + sh.spread;
                const bool all = (sh.ox == 0 && sh.oy == 0);
                const bool left = sh.ox > 0;
                const bool right = sh.ox < 0;
                const bool top = sh.oy > 0;
                const bool bottom = sh.oy < 0;
                // band alpha: exact falloff on 32bpp; on binary depths
                // the base alpha already reads 0/1, so bands keep or
                // drop by the half-coverage rule (plot_aa precedent)
                auto band_color = [&](const int i) {
                    core::Color c = sh.c;
                    if (bands > 1)
                    {
                        if constexpr (core::Color::per_channel_blend)
                        {
                            c.set_a(static_cast<uint8_t>(sh.c.a() *
                                                         (bands - i) / bands));
                        }
                        else if ((bands - i) * 2 < bands)
                        {
                            c.set_a(0);
                        }
                    }
                    return c;
                };
                if (all)
                {
                    for (int i = 0; i < bands; ++i)
                    {
                        const int o = base + i;
                        if (o * 2 >= s.width || o * 2 >= s.height)
                        {
                            break;
                        }
                        area.draw_round_rect_aa(
                            o, o, s.width - 1 - o, s.height - 1 - o,
                            radius > o ? radius - o : 0, band_color(i));
                    }
                    continue;
                }
                // paintable x-span of a band row (rounded corners cut)
                auto row_span = [&](const int row, int &lx, int &rx) {
                    lx = 0;
                    rx = s.width - 1;
                    if (radius <= 0)
                    {
                        return;
                    }
                    int dy = 0;
                    if (row < radius)
                    {
                        dy = radius - row;
                    }
                    else if (row > s.height - 1 - radius)
                    {
                        dy = row - (s.height - 1 - radius);
                    }
                    if (dy > 0)
                    {
                        const int dx = core::Graphics::corner_chord(radius, dy);
                        lx = radius - dx;
                        rx = s.width - 1 - radius + dx;
                    }
                };
                // paintable y-span of a band column (transposed)
                auto col_span = [&](const int col, int &ty, int &by) {
                    ty = 0;
                    by = s.height - 1;
                    if (radius <= 0)
                    {
                        return;
                    }
                    int dx = 0;
                    if (col < radius)
                    {
                        dx = radius - col;
                    }
                    else if (col > s.width - 1 - radius)
                    {
                        dx = col - (s.width - 1 - radius);
                    }
                    if (dx > 0)
                    {
                        const int dy = core::Graphics::corner_chord(radius, dx);
                        ty = radius - dy;
                        by = s.height - 1 - radius + dy;
                    }
                };
                // AA fringe on a chord-cut band end (the fill corner
                // formula): the pixel just outside the span blends by
                // coverage in the band color, so the falloff alpha
                // stacks (binary depths inherit the half rule)
                auto row_fringe = [&](const int row, const int lx, const int rx,
                                      const core::Color &c) {
                    if (radius <= 0)
                    {
                        return;
                    }
                    int dy = 0;
                    if (row < radius)
                    {
                        dy = radius - row;
                    }
                    else if (row > s.height - 1 - radius)
                    {
                        dy = row - (s.height - 1 - radius);
                    }
                    if (dy <= 0)
                    {
                        return;
                    }
                    const int dx = core::Graphics::corner_chord(radius, dy);
                    const int64_t t = 1LL * radius * radius - 1LL * dy * dy;
                    const int frac8 = static_cast<int>((t - 1LL * dx * dx) * 255 /
                                                       (2LL * dx + 1));
                    if (frac8 > 0)
                    {
                        area.plot_aa(lx - 1, row, frac8, c);
                        area.plot_aa(rx + 1, row, frac8, c);
                    }
                };
                auto col_fringe = [&](const int col, const int ty, const int by,
                                      const core::Color &c) {
                    if (radius <= 0)
                    {
                        return;
                    }
                    int dx = 0;
                    if (col < radius)
                    {
                        dx = radius - col;
                    }
                    else if (col > s.width - 1 - radius)
                    {
                        dx = col - (s.width - 1 - radius);
                    }
                    if (dx <= 0)
                    {
                        return;
                    }
                    const int dy = core::Graphics::corner_chord(radius, dx);
                    const int64_t t = 1LL * radius * radius - 1LL * dx * dx;
                    const int frac8 = static_cast<int>((t - 1LL * dy * dy) * 255 /
                                                       (2LL * dy + 1));
                    if (frac8 > 0)
                    {
                        area.plot_aa(col, ty - 1, frac8, c);
                        area.plot_aa(col, by + 1, frac8, c);
                    }
                };
                if (top)
                {
                    for (int i = 0; i < bands; ++i)
                    {
                        const int row = base + i;
                        if (row >= s.height)
                        {
                            break;
                        }
                        int lx = 0;
                        int rx = 0;
                        row_span(row, lx, rx);
                        const core::Color bc = band_color(i);
                        area.draw_line(lx, row, rx, row, bc);
                        row_fringe(row, lx, rx, bc);
                    }
                }
                if (bottom)
                {
                    for (int i = 0; i < bands; ++i)
                    {
                        const int row = s.height - 1 - base - i;
                        if (row < 0)
                        {
                            break;
                        }
                        int lx = 0;
                        int rx = 0;
                        row_span(row, lx, rx);
                        const core::Color bc = band_color(i);
                        area.draw_line(lx, row, rx, row, bc);
                        row_fringe(row, lx, rx, bc);
                    }
                }
                if (left)
                {
                    for (int i = 0; i < bands; ++i)
                    {
                        const int col = base + i;
                        if (col >= s.width)
                        {
                            break;
                        }
                        int ty = 0;
                        int by = 0;
                        col_span(col, ty, by);
                        const core::Color bc = band_color(i);
                        area.draw_line(col, ty, col, by, bc);
                        col_fringe(col, ty, by, bc);
                    }
                }
                if (right)
                {
                    for (int i = 0; i < bands; ++i)
                    {
                        const int col = s.width - 1 - base - i;
                        if (col < 0)
                        {
                            break;
                        }
                        int ty = 0;
                        int by = 0;
                        col_span(col, ty, by);
                        const core::Color bc = band_color(i);
                        area.draw_line(col, ty, col, by, bc);
                        col_fringe(col, ty, by, bc);
                    }
                }
            }
        }
        if (blend)
        {
            area.enable_alpha(bak);
        }
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
