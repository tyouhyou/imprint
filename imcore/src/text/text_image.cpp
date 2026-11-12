#include "text_image.hpp"

#include <string>

#include "bitmap_provider.hpp"
#include "utf8.hpp"

namespace zb::ui
{
    zb::SharedPtr<core::Graphics> make_text_image(
        const char *text,
        const int width, const int height,
        const core::Color &foreground,
        const core::Color &background)
    {
        return make_text_image(text, width, height, foreground, background,
                               nullptr, false);
    }

    zb::SharedPtr<core::Graphics> make_text_image(
        const char *text,
        const int width, const int height,
        const core::Color &foreground,
        const core::Color &background,
        const GlyphProvider *provider, const bool left_align)
    {
        auto g = core::Graphics::make_ptr(
            static_cast<uint32_t>(width > 0 ? width : 0),
            static_cast<uint32_t>(height > 0 ? height : 0));
        g->fill(background);

        if (nullptr == text || *text == '\0')
        {
            return g;
        }

        static const BitmapProvider bitmap_fallback;
        const GlyphProvider &font =
            (provider != nullptr) ? *provider : bitmap_fallback;
        // one decode pass: the stored/API currency is UTF-8, the provider
        // seam takes code points
        std::u32string cps;
        for (const char *p = text; *p != '\0';)
        {
            cps.push_back(decode_utf8_next(p));
        }
        const auto m = font.measure(cps.data(), static_cast<int>(cps.size()));
        const int x0 = left_align ? 0 : (width - m.width) / 2;
        const int y0 = (height - m.height) / 2 + m.ascent;  // baseline
        font.write(*g, cps.data(), static_cast<int>(cps.size()), x0, y0,
                   foreground);
        return g;
    }
}  // namespace zb::ui
