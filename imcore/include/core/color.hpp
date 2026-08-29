#pragma once

#include "pixel_traits.hpp"

namespace zb::ui::core
{
    constexpr int ImColor_Depth = COLOR_DEPTH;

    // the build's pixel type: the COLOR_DEPTH x RGB_MODEL x ENDIAN
    // matrix selected as compile-time traits (A-19, ARCHITECTURE.md §4.4)
    using Color = basic_color<IM_PIXEL_TRAITS(RGB_MODEL, ENDIAN)>;

    namespace colors
    {
        // from(), not {}: a value-initialized Color is fully transparent
        // (pixel == 0, alpha 0). Black written that way made every default
        // border/text pixel transparent on hosts that honor the alpha byte
        // (python/pygame RGBA, wasm canvas); desktop shells ignore it, so
        // the bug was invisible there
        const static Color Black = Color::from(0, 0, 0);
        const static Color White = Color::from(255, 255, 255);
        const static Color Red = Color::from(255, 0, 0);
        const static Color Green = Color::from(0, 255, 0);
        const static Color Blue = Color::from(0, 0, 255);
    };
}
