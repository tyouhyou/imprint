#include "test.hpp"

#include <cstring>
#include <vector>

#include "core/pixel_convert.hpp"

using namespace zb::ui;

// A-1 presentation seam: the row converter is the only place the
// internal buffer becomes a panel format (contract: code-contract S3)
int test_pixel_convert()
{
    // per-format pixel width
    EXPECT(core::panel_pixel_bytes(core::panel_format::native) == sizeof(core::Color));
    EXPECT(core::panel_pixel_bytes(core::panel_format::bgr565) == 2);

    // native: a byte-exact copy of the internal row
    {
        core::Color row[3] = {core::Color::from(1, 2, 3),
                              core::Color::from(250, 100, 50),
                              core::colors::White};
        std::vector<uint8_t> buf(sizeof(row), 0);
        EXPECT(core::convert_row(core::panel_format::native, row, 3, buf.data(), buf.size()) == sizeof(row));
        EXPECT(0 == std::memcmp(row, buf.data(), sizeof(row)));
    }

    // bgr565: pure channels pack to the textbook 565 words
    {
        core::Color row[3] = {core::Color::from(255, 0, 0),
                              core::Color::from(0, 255, 0),
                              core::Color::from(0, 0, 255)};
        uint8_t buf[6] = {0};
        EXPECT(core::convert_row(core::panel_format::bgr565, row, 3, buf, sizeof(buf)) == 6);
        // red 0xF800, green 0x07E0, blue 0x001F; little-endian bytes
        EXPECT(buf[0] == 0x00 && buf[1] == 0xF8);
        EXPECT(buf[2] == 0xE0 && buf[3] == 0x07);
        EXPECT(buf[4] == 0x1F && buf[5] == 0x00);
    }

    // bgr565: a mid gray keeps its per-channel quantized value
    {
        core::Color row[1] = {core::Color::from(128, 128, 128)};
        uint8_t buf[2] = {0};
        EXPECT(core::convert_row(core::panel_format::bgr565, row, 1, buf, sizeof(buf)) == 2);
        const int px = buf[0] | (buf[1] << 8);
        EXPECT((px >> 11) == (128 >> 3));
        EXPECT(((px >> 5) & 0x3F) == (128 >> 2));
        EXPECT((px & 0x1F) == (128 >> 3));
    }

    // silent rejection: short capacity or zero count writes nothing
    {
        core::Color row[2] = {core::colors::White, core::colors::Black};
        uint8_t buf[2] = {0xAA, 0xAA};
        EXPECT(core::convert_row(core::panel_format::bgr565, row, 2, buf, 3) == 0);
        EXPECT(buf[0] == 0xAA && buf[1] == 0xAA);  // untouched
        EXPECT(core::convert_row(core::panel_format::bgr565, row, 0, buf, sizeof(buf)) == 0);
        EXPECT(core::convert_row(core::panel_format::bgr565, nullptr, 2, buf, sizeof(buf)) == 0);
    }

    return test::report("pixel_convert");
}
