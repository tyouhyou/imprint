// Sixel presenter tests (P4 demo target): structural checks of the DCS
// stream — header/footer, palette definitions, band separators, RLE —
// and byte-identical determinism, the same discipline as test_gif.
// Palette-percentage expectations follow the build's Color::depth (the
// test_pixel_traits pattern): at 16bpp the accessors read expanded
// (bits << 3), so a 255 source reads back 248 and encodes as 97%.
#include "test.hpp"

#include <string>

#include "shell/sixel.hpp"

using namespace zb::ui;

namespace
{
    std::string encode_color4(core::Color a, core::Color b)
    {
        core::Color buf[8] = {a, b, a, b, a, b, a, b};  // 4x2
        return zb::shell::sixel::encode_frame(buf, 4, 2);
    }
}

int test_sixel()
{
    // one color, one band: raster attrs + one palette + data + ST
    {
        core::Color buf[16] = {};
        for (auto &c : buf)
        {
            c = core::Color::from(255, 0, 0, 255);
        }
        const std::string s = zb::shell::sixel::encode_frame(buf, 4, 4);
        EXPECT(s.substr(0, 3) == "\x1bPq");
        EXPECT(s.find("\"1;1;4;4") != std::string::npos);
        // 32bpp: 255 -> 100; 16bpp: 255 truncates to the 5-bit 31,
        // reads back 248 -> 97
        EXPECT(s.find(core::Color::depth == 32 ? "#0;2;100;0;0" : "#0;2;97;0;0")
               != std::string::npos);
        EXPECT(s.substr(s.size() - 2) == "\x1b\\");
        // 4 columns of the same mask (all 4 band rows red -> bits 15 ->
        // '?'+15 = 'N'), run of 4 collapses to !4N
        EXPECT(s.find("!4N") != std::string::npos);
        // no band separator within one band
        EXPECT(s.find('-') == std::string::npos);
    }

    // two colors in one band: two registers, '$' between the redraw
    // passes, zero-mask columns still emitted ('?')
    {
        const std::string s = encode_color4(core::Color::from(255, 0, 0, 255),
                                            core::Color::from(0, 0, 255, 255));
        // same depth split as the one-color case (red then blue register)
        EXPECT(s.find(core::Color::depth == 32 ? "#0;2;100;0;0" : "#0;2;97;0;0")
               != std::string::npos);
        EXPECT(s.find(core::Color::depth == 32 ? "#1;2;0;0;100" : "#1;2;0;0;97")
               != std::string::npos);
        EXPECT(s.find('$') != std::string::npos);
    }

    // 7 rows: two bands -> exactly one '-' separator
    {
        core::Color buf[28] = {};
        for (auto &c : buf)
        {
            c = core::Color::from(0, 255, 0, 255);
        }
        const std::string s = zb::shell::sixel::encode_frame(buf, 4, 7);
        EXPECT(s.find('-') != std::string::npos);
        std::size_t seps = 0;
        for (const char ch : s)
        {
            seps += (ch == '-') ? 1 : 0;
        }
        EXPECT(seps == 1);
    }

    // determinism: the same buffer encodes byte-identically
    {
        const std::string a = encode_color4(core::Color::from(1, 2, 3, 255),
                                            core::Color::from(4, 5, 6, 255));
        const std::string b = encode_color4(core::Color::from(1, 2, 3, 255),
                                            core::Color::from(4, 5, 6, 255));
        EXPECT(a == b);
        EXPECT(!a.empty());
    }

    return test::report("sixel");
}
