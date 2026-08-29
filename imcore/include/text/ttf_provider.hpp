#pragma once

#include "glyph_provider.hpp"

namespace zb::ui
{
    /*
     * Glyph provider backed by the build-time rasterized TTF subset
     * (batch S2; code-contract 2.4, plan 2). Stateless and shareable.
     * Widgets opt in through set_glyph_provider (the same selection
     * point as USE_FONT's set_font); the default stays the 5x7 bitmap
     * rendering. Units absent from the table report uncovered, so the
     * fallback chain hands them to the bitmap provider. Coverage bytes
     * are drawn as the foreground color's alpha (zero allocation in
     * write).
     */
    class TtfSubsetProvider : public GlyphProvider
    {
    public:
        bool covers(const char16_t ch) const override;
        text_metrics measure(const char16_t *str, const int len) const override;
        text_metrics line_metrics() const override;
        void write(core::Graphics &g, const char16_t *str, const int len,
                   const int x, const int y, const core::Color &color) const override;
    };

    /*
     * The process-wide shared provider instance (leaked on purpose, the
     * A-4.1 pattern, so static-lifetime widgets never touch a dead
     * provider). Install with Widget::set_glyph_provider.
     */
    zb::SharedPtr<GlyphProvider> ttf_subset_provider();
}  // namespace zb::ui
