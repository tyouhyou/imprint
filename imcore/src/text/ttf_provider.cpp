#include "ttf_provider.hpp"

#include "ttf_glyphs.hpp"

namespace zb::ui
{
    namespace
    {
        // binary search over the code-point-sorted table
        const TtfGlyph *find_glyph(const char32_t cp)
        {
            int lo = 0;
            int hi = static_cast<int>(kTtfGlyphCount);
            while (lo < hi)
            {
                const int mid = (lo + hi) / 2;
                if (kTtfGlyphs[mid].ch < cp)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            if (lo < static_cast<int>(kTtfGlyphCount) && kTtfGlyphs[lo].ch == cp)
            {
                return &kTtfGlyphs[lo];
            }
            return nullptr;
        }
    }  // namespace

    bool TtfSubsetProvider::covers(const char32_t cp) const
    {
        return find_glyph(cp) != nullptr;
    }

    text_metrics TtfSubsetProvider::measure(const char32_t *str, const int len) const
    {
        text_metrics m;
        m.height = kTtfLineHeight;
        m.ascent = kTtfAscent;
        if (str == nullptr || len <= 0)
        {
            return m;
        }
        for (int i = 0; i < len; ++i)
        {
            if (const auto *g = find_glyph(str[i]))
            {
                m.width += g->advance;
            }
            // uncovered code points advance zero width
        }
        return m;
    }

    text_metrics TtfSubsetProvider::line_metrics() const
    {
        return {0, kTtfLineHeight, kTtfAscent};
    }

    void TtfSubsetProvider::write(core::Graphics &g, const char32_t *str, const int len,
                                  const int x, const int y, const core::Color &color) const
    {
        if (str == nullptr || len <= 0)
        {
            return;
        }
        // coverage rides the foreground color's alpha channel; the
        // rasterizer hard-clips per pixel (damage/clip safe)
        const auto bak = g.is_alpha_enabled();
        g.enable_alpha(true);
        int pen = x;
        for (int i = 0; i < len; ++i)
        {
            const TtfGlyph *glyph = find_glyph(str[i]);
            if (glyph == nullptr)
            {
                continue;
            }
            if (glyph->width > 0 && glyph->height > 0)
            {
                const uint8_t *a = kTtfGlyphAlpha + glyph->alpha_off;
                const int left = pen + glyph->xoff;
                const int top = y + glyph->yoff;  // y is the baseline
                for (int r = 0; r < glyph->height; ++r)
                {
                    for (int c = 0; c < glyph->width; ++c)
                    {
                        const uint8_t cov = a[r * glyph->width + c];
                        if (cov != 0)
                        {
                            g.draw_pixel(left + c, top + r,
                                         core::Color::from(color.r(), color.g(), color.b(), cov));
                        }
                    }
                }
            }
            pen += glyph->advance;
        }
        g.enable_alpha(bak);
    }

    zb::SharedPtr<GlyphProvider> ttf_subset_provider()
    {
        static zb::SharedPtr<GlyphProvider> *p =
            new zb::SharedPtr<GlyphProvider>(zb::make_shared<TtfSubsetProvider>());
        return *p;
    }
}  // namespace zb::ui
