#pragma once

#include <cstdint>

#include "core/color.hpp"

namespace zb::raster
{
    /*
     * The raster module renders into plain uint32 pixel words, not into
     * zb::ui::core::Color: a per-pixel Color round trip is exactly the
     * overhead a software rasterizer cannot afford (measured -- see
     * fps docs/DESIGN.md F9).
     *
     * Packing therefore has to know where the build put each channel.
     * Those byte offsets are compile-time constants of the library's
     * pixel traits, reachable through the public IM_PIXEL_TRAITS macro --
     * the same macro Color itself is defined from, so the two can never
     * drift apart. (Probing a Color at runtime would work too, but
     * Color::from is not constexpr, and the static_assert below catches a
     * build that changes the pixel model out from under this file.)
     */
    namespace detail
    {
        using ImTraits = zb::ui::core::IM_PIXEL_TRAITS(RGB_MODEL, ENDIAN);

        constexpr int kRByte = ImTraits::r_byte;
        constexpr int kGByte = ImTraits::g_byte;
        constexpr int kBByte = ImTraits::b_byte;
        constexpr int kAByte = ImTraits::a_byte;
    }

    // The scene path is uint32-only, and the page hands these bytes
    // straight to ImageData, so the build must be 32bpp with channels in
    // memory order {r,g,b,a}. The consuming build selects RGB_MODEL=rgba32
    // for exactly this reason; fail loudly rather than silently packing
    // BGRA and shipping a swapped-color picture.
    static_assert(sizeof(zb::ui::core::Color) == 4 && detail::ImTraits::depth == 32,
                  "the raster module needs a 32bpp pixel");
    static_assert(detail::kRByte == 0 && detail::kGByte == 1 &&
                      detail::kBByte == 2 && detail::kAByte == 3,
                  "the raster module expects RGB_MODEL=rgba32 with "
                  "ENDIAN=le (memory order {r,g,b,a})");

    inline constexpr uint32_t pack(const int r, const int g, const int b,
                                   const int a = 255)
    {
        return (static_cast<uint32_t>(r & 255) << (8 * detail::kRByte)) |
               (static_cast<uint32_t>(g & 255) << (8 * detail::kGByte)) |
               (static_cast<uint32_t>(b & 255) << (8 * detail::kBByte)) |
               (static_cast<uint32_t>(a & 255) << (8 * detail::kAByte));
    }

    inline constexpr int unpack_r(const uint32_t p)
    {
        return static_cast<int>((p >> (8 * detail::kRByte)) & 0xFFu);
    }
    inline constexpr int unpack_g(const uint32_t p)
    {
        return static_cast<int>((p >> (8 * detail::kGByte)) & 0xFFu);
    }
    inline constexpr int unpack_b(const uint32_t p)
    {
        return static_cast<int>((p >> (8 * detail::kBByte)) & 0xFFu);
    }
    inline constexpr int unpack_a(const uint32_t p)
    {
        return static_cast<int>((p >> (8 * detail::kAByte)) & 0xFFu);
    }

    // a gradient endpoint: kept as channels so a ramp can be evaluated
    // with the same integer arithmetic the lib's lerp_color uses
    struct Rgba
    {
        int r = 0, g = 0, b = 0, a = 255;
    };

    /*
     * Channel-wise integer lerp, bit-for-bit the lib's lerp_color
     * (imcore/src/core/graphics.cpp): from.c*(steps-i) + to.c*i, then one
     * truncating division per channel. Reproducing it exactly is what
     * lets the scene re-implement fill_gradient and still match the old
     * frame pixel for pixel.
     */
    inline constexpr Rgba lerp_rgba(const Rgba &from, const Rgba &to, const int i,
                                    const int steps)
    {
        if (steps <= 0)
        {
            return from;
        }
        return Rgba{(from.r * (steps - i) + to.r * i) / steps,
                    (from.g * (steps - i) + to.g * i) / steps,
                    (from.b * (steps - i) + to.b * i) / steps,
                    (from.a * (steps - i) + to.a * i) / steps};
    }

    inline constexpr uint32_t pack(const Rgba &c)
    {
        return pack(c.r, c.g, c.b, c.a);
    }
} // namespace zb::raster