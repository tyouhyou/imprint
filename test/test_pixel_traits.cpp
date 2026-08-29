#include "test.hpp"

#include "imcore.hpp"

using namespace zb::ui::core;

// Pixel traits (A-19): the compile-time format description keeps the
// pre-A-19 semantics — construction, 8-bit-normalized channel access,
// quantization, and the blend policy. Word-level pixel equality is
// already locked across the battery by the existing .pixel assertions.
int test_pixel_traits()
{
    // value-initial is fully transparent; from() is the construction entry
    {
        EXPECT(Color{}.pixel == 0);
        EXPECT(Color::from(0, 0, 0, 0).pixel == 0);
    }

    // channel accessors round-trip (8-bit normalized at every depth;
    // 248 is exact on both 8-bit channels and 5-bit channels)
    {
        const Color c = Color::from(248, 248, 248);
        EXPECT(c.r() == 248);
        EXPECT(c.g() == 248);
        EXPECT(c.b() == 248);
        EXPECT(c.a() == (Color::depth == 32 ? 255 : 1));

        const Color black = Color::from(0, 0, 0, 0);
        EXPECT(black.a() == 0);
    }

    // setters write through the same normalization
    {
        Color m{};
        m.set_r(248);
        m.set_g(248);
        m.set_b(248);
        m.set_a(255);
        EXPECT(m.r() == 248);
        EXPECT(m.g() == 248);
        EXPECT(m.b() == 248);
        EXPECT(m.a() == (Color::depth == 32 ? 255 : 1));
        m.set_a(0);
        EXPECT(m.a() == 0);
    }

    if (Color::depth == 32)
    {
        // 32bpp: channels are plain bytes; four distinct values land in
        // four distinct bytes of the pixel word (layout-agnostic probe)
        const Color c = Color::from(1, 2, 3, 4);
        const auto *b = reinterpret_cast<const uint8_t *>(&c.pixel);
        int hits_r = 0, hits_g = 0, hits_b = 0, hits_a = 0;
        for (int i = 0; i < 4; ++i)
        {
            hits_r += (b[i] == 1);
            hits_g += (b[i] == 2);
            hits_b += (b[i] == 3);
            hits_a += (b[i] == 4);
        }
        EXPECT(hits_r == 1 && hits_g == 1 && hits_b == 1 && hits_a == 1);
        EXPECT(c.r() == 1 && c.g() == 2 && c.b() == 3 && c.a() == 4);

        // blend policy: 8-bit source-over
        EXPECT(Color::per_channel_blend);
    }
    else
    {
        // 16bpp abgr1555: exact word packing, 5-bit quantization,
        // single alpha bit
        const auto word = [](const int r, const int g, const int b, const int a) -> uint16_t
        {
            return static_cast<uint16_t>(((a > 0 ? 1 : 0) << 15) |
                                         ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
        };
        EXPECT(Color::from(255, 255, 255, 255).pixel == word(255, 255, 255, 255));
        EXPECT(Color::from(0x8C, 0x41, 0x77, 0).pixel == word(0x8C, 0x41, 0x77, 0));

        // reads expand 5-bit channels (bits << 3); writes truncate
        EXPECT(Color::from(0x8C, 0, 0).r() == ((0x8C >> 3) << 3));
        EXPECT(Color::from(0, 0x41, 0).g() == ((0x41 >> 3) << 3));
        EXPECT(Color::from(0, 0, 0x77).b() == ((0x77 >> 3) << 3));
        EXPECT(Color::from(0, 0, 0, 5).a() == 1);

        // blend policy: binary opacity
        EXPECT(!Color::per_channel_blend);
    }

    // operator| merges pixel words (kept from the pre-A-19 surface)
    {
        const Color lo = Color::from(0, 0, 0, 0);
        const Color hi = Color::from(255, 255, 255, 255);
        EXPECT((lo | hi).pixel == hi.pixel);
    }

    return test::report("pixel_traits");
}
