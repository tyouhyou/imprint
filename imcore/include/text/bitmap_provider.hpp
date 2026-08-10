#pragma once

#include "glyph_provider.hpp"

namespace zb::ui
{
    /*
     * Glyph provider backed by the built-in 5x7 bitmap glyphs. Covers the
     * printable range 32..95 plus lowercase letters (rendered uppercase),
     * never depends on FreeType, and is the universal fallback provider
     * for widgets (see docs/code-contract.md section 2.4). Uncovered
     * code units advance zero width.
     */
    class BitmapProvider : public GlyphProvider
    {
    public:
        bool covers(const char16_t ch) const override;
        text_metrics measure(const char16_t *str, const int len) const override;
        text_metrics line_metrics() const override;
        void write(core::Graphics &g, const char16_t *str, const int len,
                   const int x, const int y, const core::Color &color) const override;
    };
}  // namespace zb::ui
