#pragma once

/*
 * Compile-time pixel model (A-19, ARCHITECTURE.md §4.4).
 *
 * The COLOR_DEPTH x RGB_MODEL x ENDIAN build matrix is expressed as
 * pixel traits: one traits type per format, selected at compile time
 * by token-pasting the build options. Color = basic_color<Traits>,
 * exactly one instantiation per build, runtime-neutral.
 *
 * Traits carry the bit depth, the channel placement, and the blend
 * policy:
 *  - 32bpp formats place their channels at BYTE OFFSETS inside the
 *    pixel word — the endian-neutral memory byte order (A-11: the
 *    combo alias names are endian-relative; reason about byte order);
 *  - the 16bpp format (abgr1555, XBBBBBGGGGGRRRRR) places them as bit
 *    shifts/masks;
 *  - per_channel_blend: 8-bit source-over at 32bpp; binary opacity at
 *    16bpp (a single alpha bit).
 *
 * Channel accessors are 8-bit normalized on both depths: 16bpp 5-bit
 * channels read expanded (bits << 3) and write truncated (v >> 3); the
 * single alpha bit reads 0/1 and writes v > 0 — the exact semantics
 * the pre-A-19 channel proxies had.
 */

#include <stdint.h>

namespace zb::ui::core
{
    namespace pixel_traits_detail
    {
        // a 32bpp channel layout: R/G/B/A byte offsets in memory order
        template<int RByte, int GByte, int BByte, int AByte>
        struct layout32
        {
            static constexpr int depth = 32;
            using pixel_t = uint32_t;
            static constexpr bool per_channel_blend = true;
            static constexpr int r_byte = RByte;
            static constexpr int g_byte = GByte;
            static constexpr int b_byte = BByte;
            static constexpr int a_byte = AByte;
        };

        using bytes_bgra = layout32<2, 1, 0, 3>;  // memory {b, g, r, a}
        using bytes_argb = layout32<1, 2, 3, 0>;  // memory {a, r, g, b}
        using bytes_abgr = layout32<3, 2, 1, 0>;  // memory {a, b, g, r}
        using bytes_rgba = layout32<0, 1, 2, 3>;  // memory {r, g, b, a}
    }  // namespace pixel_traits_detail

    // combo aliases: IM_PIXEL_TRAITS(RGB_MODEL, ENDIAN) token-pastes to
    // one of these. The memory byte order per combo mirrors the pre-A-19
    // byte structs (color32.hpp history):
    //   _argb32_be_t {b,g,r,a} = bgra32_le / argb32_be
    //   _bgra32_t    {a,r,g,b} = bgra32_be / argb32_le
    //   _rgba32_t    {a,b,g,r} = rgba32_be / abgr32_le / rgba8888_be
    //   _abgr32_t    {r,g,b,a} = abgr32_be / rgba32_le / rgba8888_le
    using im_pixel_traits_bgra32_le = pixel_traits_detail::bytes_bgra;
    using im_pixel_traits_argb32_be = pixel_traits_detail::bytes_bgra;
    using im_pixel_traits_bgra32_be = pixel_traits_detail::bytes_argb;
    using im_pixel_traits_argb32_le = pixel_traits_detail::bytes_argb;
    using im_pixel_traits_rgba32_be = pixel_traits_detail::bytes_abgr;
    using im_pixel_traits_abgr32_le = pixel_traits_detail::bytes_abgr;
    using im_pixel_traits_rgba8888_be = pixel_traits_detail::bytes_abgr;
    using im_pixel_traits_abgr32_be = pixel_traits_detail::bytes_rgba;
    using im_pixel_traits_rgba32_le = pixel_traits_detail::bytes_rgba;
    using im_pixel_traits_rgba8888_le = pixel_traits_detail::bytes_rgba;

    // the single 16bpp format: abgr1555, word XBBBBBGGGGGRRRRR
    struct im_pixel_traits_abgr_1555
    {
        static constexpr int depth = 16;
        using pixel_t = uint16_t;
        static constexpr bool per_channel_blend = false;  // single alpha bit
        static constexpr int r_shift = 0;
        static constexpr int g_shift = 5;
        static constexpr int b_shift = 10;
        static constexpr int a_shift = 15;
        static constexpr pixel_t r_mask = 0x001F;
        static constexpr pixel_t g_mask = 0x03E0;
        static constexpr pixel_t b_mask = 0x7C00;
        static constexpr pixel_t a_mask = 0x8000;
    };

#define _IM_PIXEL_TRAITS(model, endian) im_pixel_traits_##model##_##endian
#define IM_PIXEL_TRAITS(model, endian) _IM_PIXEL_TRAITS(model, endian)

    /*
     * The pixel type. A plain pixel word plus 8-bit-normalized channel
     * accessors; `from()` is the only construction entry. Value-initial
     * (`Color{}`) is fully transparent (pixel == 0).
     */
    template<typename Traits>
    union basic_color
    {
        using pixel_t = typename Traits::pixel_t;
        static constexpr int depth = Traits::depth;
        static constexpr bool per_channel_blend = Traits::per_channel_blend;

        pixel_t pixel = 0;
        // endian-neutral byte view of the pixel word (32bpp channel
        // placement). Sized to the pixel word, NOT hardcoded 4: the color
        // object must be exactly depth/8 bytes (pre-A-19 contract; the NDS
        // shell's dmaCopy counts depth/8-per-pixel bytes)
        uint8_t bytes[sizeof(pixel_t)];  // 32bpp only: endian-neutral byte view

        operator pixel_t() const { return pixel; }
        void operator=(const pixel_t c) { pixel = c; }
        void operator=(const basic_color &c) { pixel = c.pixel; }
        basic_color operator|(const basic_color &c) const
        {
            basic_color r{};
            r.pixel = static_cast<pixel_t>(pixel | c.pixel);
            return r;
        }

        [[nodiscard]] uint8_t r() const
        {
            if constexpr (depth == 32)
            {
                return bytes[Traits::r_byte];
            }
            else
            {
                return static_cast<uint8_t>(((pixel >> Traits::r_shift) & 0x1F) << 3);
            }
        }
        void set_r(const uint8_t v)
        {
            if constexpr (depth == 32)
            {
                bytes[Traits::r_byte] = v;
            }
            else
            {
                pixel = static_cast<pixel_t>((pixel & static_cast<pixel_t>(~Traits::r_mask)) |
                                             static_cast<pixel_t>((v >> 3) << Traits::r_shift));
            }
        }

        [[nodiscard]] uint8_t g() const
        {
            if constexpr (depth == 32)
            {
                return bytes[Traits::g_byte];
            }
            else
            {
                return static_cast<uint8_t>(((pixel >> Traits::g_shift) & 0x1F) << 3);
            }
        }
        void set_g(const uint8_t v)
        {
            if constexpr (depth == 32)
            {
                bytes[Traits::g_byte] = v;
            }
            else
            {
                pixel = static_cast<pixel_t>((pixel & static_cast<pixel_t>(~Traits::g_mask)) |
                                             static_cast<pixel_t>((v >> 3) << Traits::g_shift));
            }
        }

        [[nodiscard]] uint8_t b() const
        {
            if constexpr (depth == 32)
            {
                return bytes[Traits::b_byte];
            }
            else
            {
                return static_cast<uint8_t>(((pixel >> Traits::b_shift) & 0x1F) << 3);
            }
        }
        void set_b(const uint8_t v)
        {
            if constexpr (depth == 32)
            {
                bytes[Traits::b_byte] = v;
            }
            else
            {
                pixel = static_cast<pixel_t>((pixel & static_cast<pixel_t>(~Traits::b_mask)) |
                                             static_cast<pixel_t>((v >> 3) << Traits::b_shift));
            }
        }

        [[nodiscard]] uint8_t a() const
        {
            if constexpr (depth == 32)
            {
                return bytes[Traits::a_byte];
            }
            else
            {
                return (pixel & Traits::a_mask) ? 1 : 0;
            }
        }
        void set_a(const uint8_t v)
        {
            if constexpr (depth == 32)
            {
                bytes[Traits::a_byte] = v;
            }
            else
            {
                pixel = static_cast<pixel_t>(v > 0 ? pixel | Traits::a_mask
                                                   : pixel & static_cast<pixel_t>(~Traits::a_mask));
            }
        }

        [[nodiscard]] static basic_color from(const uint8_t r, const uint8_t g,
                                              const uint8_t b, const uint8_t a = 0xFF)
        {
            basic_color c{};
            c.set_r(r);
            c.set_g(g);
            c.set_b(b);
            c.set_a(a);
            return c;
        }
    };

    // the color object IS the pixel word: hosts size buffers and count
    // presentation bytes as depth/8 per pixel (e.g. the NDS dmaCopy)
    static_assert(sizeof(basic_color<im_pixel_traits_abgr_1555>) == 2);
    static_assert(sizeof(basic_color<pixel_traits_detail::bytes_bgra>) == 4);
}  // namespace zb::ui::core
