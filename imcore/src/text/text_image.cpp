#include "text_image.hpp"

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
        const auto u16 = utf8_to_utf16(text);
        const auto m = font.measure(u16.data(), static_cast<int>(u16.size()));
        const int x0 = left_align ? 0 : (width - m.width) / 2;
        const int y0 = (height - m.height) / 2 + m.ascent;  // baseline
        font.write(*g, u16.data(), static_cast<int>(u16.size()), x0, y0,
                   foreground);
        return g;
    }
}  // namespace zb::ui
