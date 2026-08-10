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
        auto g = core::Graphics::make_ptr(
            static_cast<uint32_t>(width > 0 ? width : 0),
            static_cast<uint32_t>(height > 0 ? height : 0));
        g->fill(background);

        if (nullptr == text || *text == '\0')
        {
            return g;
        }

        const auto u16 = utf8_to_utf16(text);
        const BitmapProvider provider;
        const auto m = provider.measure(u16.data(), static_cast<int>(u16.size()));
        const int x0 = (width - m.width) / 2;
        const int y0 = (height - m.height) / 2 + m.ascent;  // baseline
        provider.write(*g, u16.data(), static_cast<int>(u16.size()), x0, y0, foreground);
        return g;
    }
}  // namespace zb::ui
