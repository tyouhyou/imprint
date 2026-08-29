#pragma once

#include "../core/graphics.hpp"

namespace zb::ui
{
    /*
     * Metrics of a rendered text run (see docs/code-contract.md section
     * 2.4): width is the total advance of the string, height the line
     * height, ascent the distance from the baseline to the top.
     */
    struct text_metrics
    {
        int width = 0;
        int height = 0;
        int ascent = 0;
    };

    /*
     * Glyph provider: renders UTF-16 text runs onto a Graphics surface.
     * This is the single seam between widgets and the text rendering
     * backends, so a widget can fall back from its primary provider
     * (FreeType) to the built-in 5x7 bitmap glyphs for uncovered
     * characters (see Widget::draw_text and BitmapProvider).
     *
     * Implementations are hot path: they must never throw.
     */
    class GlyphProvider
    {
    public:
        virtual ~GlyphProvider() = default;

        /* whether this provider has a glyph for the given code unit */
        virtual bool covers(const char16_t ch) const = 0;

        /* advance width / line height / ascent of the whole run */
        virtual text_metrics measure(const char16_t *str, const int len) const = 0;

        /*
         * Line height and ascent without scanning the string (width is 0).
         * Cheaper than measure() on providers whose metrics cost a glyph
         * load per code unit (e.g. FreeType); used for vertical alignment.
         */
        virtual text_metrics line_metrics() const = 0;

        /*
         * Draws the run with the baseline of the first line at (x, y),
         * matching Font::write. Out-of-bounds pixels are clipped by
         * Graphics::draw_pixel, so no bounds check is needed here.
         */
        virtual void write(core::Graphics &g, const char16_t *str, const int len,
                           const int x, const int y, const core::Color &color) const = 0;
    };

    /*
     * Process-wide default glyph provider (code-contract 2.4, plan 2):
     * a widget with no explicit set_glyph_provider() uses it before the
     * 5x7 bitmap fallback. Empty by default (bitmap-only rendering);
     * the desktop shells install the rasterized platform font at startup
     * under USE_FONT_SIZE. Pass an empty pointer to restore the default.
     */
    void set_default_glyph_provider(const zb::SharedPtr<GlyphProvider> &provider);
    const zb::SharedPtr<GlyphProvider> &default_glyph_provider();
}  // namespace zb::ui
