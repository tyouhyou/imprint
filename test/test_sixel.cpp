// Sixel presenter tests (P4 demo target): structural checks of the DCS
// stream — header/footer, palette definitions, band separators, RLE —
// and byte-identical determinism, the same discipline as test_gif.
// Palette-percentage expectations follow the build's Color::depth (the
// test_pixel_traits pattern): at 16bpp the accessors read expanded
// (bits << 3), so a 255 source reads back 248 and encodes as 97%.
#include "test.hpp"

#include <string>
#include <vector>

#include "logging.hpp"
#include "shell/sixel.hpp"

using namespace zb::ui;

namespace
{
    std::string encode_color4(core::Color a, core::Color b)
    {
        core::Color buf[8] = {a, b, a, b, a, b, a, b};  // 4x2
        return zb::shell::sixel::encode_frame(buf, 4, 2);
    }

    // collects the LW lines one encode_frame call emits
    struct log_probe
    {
        std::vector<std::string> messages;
        log_probe()
        {
            zb::Logging::set_log_handle(
                [this](const zb::Logging_Level level, const std::string &msg)
                {
                    if (level == zb::Logging_Level::warn)
                    {
                        messages.push_back(msg);
                    }
                });
        }
        ~log_probe() { zb::Logging::set_log_handle(); }
        log_probe(const log_probe &) = delete;
        log_probe &operator=(const log_probe &) = delete;
    };
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
        EXPECT(s.find(core::Color::depth == 32 ? "#0;2;100;0;0" : "#0;2;97;0;0") != std::string::npos);
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
        EXPECT(s.find(core::Color::depth == 32 ? "#0;2;100;0;0" : "#0;2;97;0;0") != std::string::npos);
        EXPECT(s.find(core::Color::depth == 32 ? "#1;2;0;0;100" : "#1;2;0;0;97") != std::string::npos);
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

    // X-13, recast by FI-10: a band over the 250-register budget no
    // longer drops colors — the whole frame switches to the frame-level
    // deterministic median-cut palette. Quantization is normal
    // operation: no diagnostic fires (the old one-LW-per-frame latch is
    // gone), and the stream stays complete and byte-identical across
    // encodes.
    // Depth-aware fixture as before: 16bpp quantizes a channel to 5
    // bits, so a gray ramp holds only 32 distinct colors and would never
    // overflow — the distinctness has to come from channel spread to be
    // depth-neutral (the same trap as the palette-percentage expectations
    // above).
    {
        constexpr int w = 260;  // > 250 distinct colors in one band
        constexpr int h = 6;
        std::vector<core::Color> buf(static_cast<std::size_t>(w) * h);
        for (int i = 0; i < w; ++i)
        {
            // channel triples that stay distinct after the 16bpp 5-bit
            // quantization too (r*3, g*5, b*9 mod 256: 256 distinct
            // triples at both depths, so the band overflows in every
            // configuration)
            const auto r = static_cast<uint8_t>((i * 3) & 0xFF);
            const auto g = static_cast<uint8_t>((i * 5) & 0xFF);
            const auto b = static_cast<uint8_t>((i * 9) & 0xFF);
            for (int y = 0; y < h; ++y)
            {
                buf[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(i)] =
                    core::Color::from(r, g, b, 255);
            }
        }
        log_probe probe;
        const std::string s = zb::shell::sixel::encode_frame(buf.data(), w, h);
        // quantization is normal operation, not a warning: zero LW
        EXPECT(probe.messages.empty());
        // the frame is still well formed and still deterministic
        EXPECT(s.substr(0, 3) == "\x1bPq");
        EXPECT(s.substr(s.size() - 2) == "\x1b\\");
        const std::string again = zb::shell::sixel::encode_frame(buf.data(), w, h);
        EXPECT(s == again);
    }

    // X-13: a frame within the budget logs nothing at all
    {
        log_probe probe;
        const std::string s = encode_color4(core::Color::from(255, 0, 0, 255),
                                            core::Color::from(0, 0, 255, 255));
        EXPECT(probe.messages.empty());
        EXPECT(s.find('$') != std::string::npos);
    }

    return test::report("sixel");
}
