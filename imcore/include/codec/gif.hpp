#pragma once

/*
 * Minimal GIF89a writer (codec family, Batch V-0: promoted from the
 * showcase recorder). No external dependencies: a fixed 216-color
 * web-safe cube as the global palette (UI color fields quantize with
 * no visible banding), one full frame per call, standard
 * variable-width LZW, infinite loop.
 *
 * Input frames are the build's pixel type (core::Color, width*height,
 * row-major, pitch = width) — the channel values go through the
 * 8-bit-normalized accessors (A-19), so 16bpp builds quantize to the
 * palette through the same code path as 32bpp. The alpha channel is
 * ignored (GIF has no alpha; the recorder captures opaque frames).
 *
 * File output matches the codec family (read_png_file takes paths);
 * the frame *pacing* — when to capture and when to stop — is host
 * glue and stays out of here (tool-placement rule, ARCHITECTURE.md 2).
 */

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "core/color.hpp"

namespace zb::ui
{
    /*
     * Optimal-palette support (P-2: the fixed 216-cube bands on smooth
     * ramps): a two-pass flow for recorders that know their frame set —
     * collect the frames' colors, build a median-cut palette, then write
     * with it. The default constructor keeps the web-safe cube. The
     * median cut is deterministic (sorted buckets, tie-broken splits),
     * so the same frames always yield the same palette and the same
     * bytes — the test_gif guarantee carries over.
     */
    struct GifPalette
    {
        // RGB triples, count entries; count is a power-of-two-friendly
        // value <= 256 and the writer pads the GCT to 256
        std::uint8_t rgb[768] = {};
        std::size_t count = 0;
    };

    class GifPaletteBuilder
    {
    public:
        /* collect one frame's colors (weight 1 each pixel) */
        void add_frame(const core::Color *pixels, std::size_t n);
        /* median-cut palette over everything collected, <= max_colors */
        GifPalette palette(std::size_t max_colors = 256) const;

    private:
        std::vector<std::uint32_t> colors_;  // packed 0x00RRGGBB, sorted
        std::vector<std::uint32_t> counts_;  // parallel weight table
    };

    class GifWriter
    {
    public:
        GifWriter(const char *path, std::size_t width, std::size_t height,
                  std::size_t delay_cs);
        /* custom global palette (the optimal-palette two-pass flow) */
        GifWriter(const char *path, std::size_t width, std::size_t height,
                  std::size_t delay_cs, const GifPalette &palette);
        ~GifWriter() { close(); }

        GifWriter(const GifWriter &) = delete;
        GifWriter &operator=(const GifWriter &) = delete;

        /* pixels: width*height core::Color values, pitch = width */
        void add_frame(const core::Color *pixels);

        void close();

        [[nodiscard]] std::size_t frames() const { return frames_; }

        void write_header(const GifPalette *pal);

    private:
        /* null = the built-in web-safe cube; custom = exact-hash then
         * nearest-match mapping over the caller's palette */
        const GifPalette *palette_ = nullptr;
        std::ofstream out_;
        std::size_t width_;
        std::size_t height_;
        std::size_t delay_cs_;
        std::size_t frames_ = 0;
        bool closed_ = false;
        mutable std::unordered_map<std::uint32_t, uint8_t> nearest_;
    };
}
