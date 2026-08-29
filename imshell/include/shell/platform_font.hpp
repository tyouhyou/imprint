#pragma once

#if defined(IMCORE_USE_FONT_SIZE) && defined(IMCORE_HAS_TTF_SUBSET)
#include "text/ttf_provider.hpp"
#endif

namespace zb::shell
{
    /*
     * Platform default font (code-contract 2.4, plan 2, USE_FONT_SIZE):
     * every desktop shell calls this before make_app() so widgets without
     * an explicit glyph provider render the rasterized system font at
     * TTF_PIXEL_SIZE instead of the 5x7 bitmap. OFF builds compile to
     * nothing; the test battery never calls it, so its text assertions
     * stay on the deterministic 5x7 even in a USE_FONT_SIZE=ON build.
     */
    inline void install_platform_font()
    {
#if defined(IMCORE_USE_FONT_SIZE) && defined(IMCORE_HAS_TTF_SUBSET)
        zb::ui::set_default_glyph_provider(zb::ui::ttf_subset_provider());
#endif
    }
}  // namespace zb::shell
