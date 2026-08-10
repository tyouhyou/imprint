#pragma once

#include "../core/graphics.hpp"
#include "../core/ptr.hpp"

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
     * BitmapProvider::covers(). For real fonts, use the Font class
     * (USE_FONT) or a dedicated GlyphProvider -- the built-in bitmap path
     * stays usable on embedded targets without FreeType.
     */
    zb::SharedPtr<core::Graphics> make_text_image(
        const char *text,
        const int width, const int height,
        const core::Color &foreground,
        const core::Color &background);
}  // namespace zb::ui
