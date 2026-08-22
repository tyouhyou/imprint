#pragma once

#include "color_config.hpp"

#if COLOR_DEPTH == 32
#include "color32.hpp"
#elif COLOR_DEPTH == 16
#include "color16.hpp"
#endif

namespace zb::ui::core
{
    constexpr int ImColor_Depth = COLOR_DEPTH;
    typedef MAKECOLORTYPENAME(COLOR_DEPTH) Color;

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