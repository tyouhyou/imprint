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

#include "core/color.hpp"

namespace zb::ui
{
    class GifWriter
    {
    public:
        GifWriter(const char *path, std::size_t width, std::size_t height,
                  std::size_t delay_cs);
        ~GifWriter() { close(); }

        GifWriter(const GifWriter &) = delete;
        GifWriter &operator=(const GifWriter &) = delete;

        /* pixels: width*height core::Color values, pitch = width */
        void add_frame(const core::Color *pixels);

        void close();

        [[nodiscard]] std::size_t frames() const { return frames_; }

    private:
        std::ofstream out_;
        std::size_t width_;
        std::size_t height_;
        std::size_t delay_cs_;
        std::size_t frames_ = 0;
        bool closed_ = false;
    };
}
