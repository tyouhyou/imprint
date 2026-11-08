#pragma once

#include "../core/graphics.hpp"
#include "../core/ptr.hpp"
#include "../text/glyph_provider.hpp"

namespace zb::ui
{
    /*
     * Renders a text into a width x height surface, colored foreground on
     * background, centered both ways. The caller keeps the returned surface
     * (zb::SharedPtr) alive as long as widgets reference its pixels.
     *
     * Text is UTF-8 (see docs/code-contract.md section 2) and is rendered
     * with the built-in 5x7 bitmap glyphs (ASCII uppercase letters, digits
     * and a few punctuation marks; lowercase letters are rendered as
     * uppercase, other code points are skipped).
     *
     * i18n: the glyph table only covers 32..95 (plus a-z via uppercasing).
     * To add more characters, extend kGlyphs in bitmap_provider.cpp and
     * BitmapProvider::covers(). For real fonts, install a dedicated
     * GlyphProvider (build-time subset or runtime TTF, contract 2.4) --
     * the built-in bitmap path stays usable on embedded targets.
     */
    zb::SharedPtr<core::Graphics> make_text_image(
        const char *text,
        const int width, const int height,
        const core::Color &foreground,
        const core::Color &background);

    /*
     * The provider/alignment form (P1-10, the studio client's CJK rows):
     * `provider` (null = the built-in 5x7 bitmap, byte-identical to the
     * 5-arg form) renders the text; `left_align` pins x to 0 instead of
     * the horizontal center. Vertical baseline centering is kept for
     * both. ListBox rows use this with the widget's installed glyph
     * provider (FI-12) -- CJK row bodies render through the runtime TTF
     * and stream rows read left-aligned like every chat surface.
     */
    zb::SharedPtr<core::Graphics> make_text_image(
        const char *text,
        const int width, const int height,
        const core::Color &foreground,
        const core::Color &background,
        const GlyphProvider *provider, bool left_align);
}  // namespace zb::ui
