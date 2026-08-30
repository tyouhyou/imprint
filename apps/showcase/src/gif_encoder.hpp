#pragma once

/*
 * Minimal GIF89a writer (S4 frame recorder). No external dependencies:
 * a fixed 216-color web-safe cube as the global palette (UI color
 * fields quantize with no visible banding), one full frame per call,
 * standard variable-width LZW, infinite loop.
 *
 * Input frames are 32bpp BGRA (the CanvasWindow buffer format on the
 * desktop); the alpha byte is ignored.
 */

#include <cstddef>
#include <cstdint>
#include <fstream>

namespace zb::app::showcase
{
    class GifWriter
    {
    public:
        GifWriter(const char *path, std::size_t width, std::size_t height,
                  std::size_t delay_cs);
        ~GifWriter() { close(); }

        GifWriter(const GifWriter &) = delete;
        GifWriter &operator=(const GifWriter &) = delete;

        /* pixels: width*height BGRA bytes, pitch = width*4 */
        void add_frame(const uint8_t *bgra);

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
