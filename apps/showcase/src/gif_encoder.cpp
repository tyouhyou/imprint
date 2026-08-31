#include "gif_encoder.hpp"

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace zb::app::showcase
{
    namespace
    {
        void u16(std::ostream &out, const std::size_t v)
        {
            out.put(static_cast<char>(v & 0xFF));
            out.put(static_cast<char>((v >> 8) & 0xFF));
        }

        /* nearest entry of the 6x6x6 cube (values are multiples of 51) */
        uint8_t palette_index(const uint8_t r, const uint8_t g, const uint8_t b)
        {
            const auto q = [](const uint8_t v) -> uint8_t
            {
                return static_cast<uint8_t>((v + 25) / 51);
            };
            return static_cast<uint8_t>(q(r) * 36 + q(g) * 6 + q(b));
        }

        /* GIF variable-width LZW: LSB-first bit packing, codes 9..12 bits,
           clear when the next code would not fit (the decoder mirrors the
           width bump when its own dictionary reaches (1<<size)-1) */
        std::vector<uint8_t> lzw_encode(const uint8_t *pixels, const std::size_t n)
        {
            std::vector<uint8_t> bytes;
            bytes.reserve(n / 2 + 16);

            uint32_t bit_buf = 0;
            int bit_count = 0;
            const auto emit = [&](const uint16_t code, const int size)
            {
                bit_buf |= static_cast<uint32_t>(code) << bit_count;
                bit_count += size;
                while (bit_count >= 8)
                {
                    bytes.push_back(static_cast<uint8_t>(bit_buf & 0xFF));
                    bit_buf >>= 8;
                    bit_count -= 8;
                }
            };

            std::unordered_map<uint32_t, uint16_t> dict;
            dict.reserve(4096 * 2);
            const uint16_t CLEAR = 256;
            const uint16_t EOI = 257;
            int code_size = 9;
            uint16_t next = 258;
            const auto reset = [&]
            {
                dict.clear();
                next = 258;
                code_size = 9;
            };

            emit(CLEAR, code_size);
            if (n == 0)
            {
                emit(EOI, code_size);
                return bytes;
            }

            uint16_t prefix = pixels[0];
            for (std::size_t i = 1; i < n; ++i)
            {
                const uint8_t b = pixels[i];
                const uint32_t key = (static_cast<uint32_t>(prefix) << 8) | b;
                if (const auto it = dict.find(key); it != dict.end())
                {
                    prefix = it->second;
                    continue;
                }
                emit(prefix, code_size);
                dict[key] = next;
                ++next;
                if (next == (1u << code_size) + 1)
                {
                    // the just-assigned index filled the current width: the
                    // decoder widens when its next free slot would not fit,
                    // i.e. one code later than the assignment
                    if (code_size < 12)
                    {
                        ++code_size;
                    }
                    else
                    {
                        emit(CLEAR, code_size);
                        reset();
                    }
                }
                prefix = b;
            }
            emit(prefix, code_size);
            emit(EOI, code_size);
            if (bit_count > 0)
            {
                bytes.push_back(static_cast<uint8_t>(bit_buf & 0xFF));
            }
            return bytes;
        }

        void write_sub_blocks(std::ofstream &out, const std::vector<uint8_t> &bytes)
        {
            std::size_t i = 0;
            while (i < bytes.size())
            {
                const std::size_t chunk = (bytes.size() - i > 255) ? 255 : bytes.size() - i;
                out.put(static_cast<char>(chunk));
                out.write(reinterpret_cast<const char *>(bytes.data() + i),
                          static_cast<std::streamsize>(chunk));
                i += chunk;
            }
            out.put('\0');
        }
    }

    GifWriter::GifWriter(const char *path, const std::size_t width,
                         const std::size_t height, const std::size_t delay_cs)
        : out_(path, std::ios::binary), width_(width), height_(height), delay_cs_(delay_cs)
    {
        out_.write("GIF89a", 6);
        u16(out_, width_);
        u16(out_, height_);
        out_.put(static_cast<char>(0xF7));  // GCT present, 8 bits/primary, 256 entries
        out_.put('\0');                     // background color index
        out_.put('\0');                     // aspect ratio
        for (std::size_t i = 0; i < 256; ++i)
        {
            const std::size_t r = i / 36, g = (i / 6) % 6, b = i % 6;
            out_.put(static_cast<char>(r * 51));
            out_.put(static_cast<char>(g * 51));
            out_.put(static_cast<char>(b * 51));
        }

        // NETSCAPE2.0: loop forever
        out_.put('\x21');
        out_.put('\xFF');
        out_.put('\x0B');
        out_.write("NETSCAPE2.0", 11);
        out_.put('\x03');
        out_.put('\x01');
        u16(out_, 0);
        out_.put('\0');
    }

    void GifWriter::add_frame(const uint8_t *bgra)
    {
        std::vector<uint8_t> indexed(static_cast<std::size_t>(width_ * height_));
        for (std::size_t i = 0; i < indexed.size(); ++i)
        {
            indexed[i] = palette_index(bgra[i * 4 + 2], bgra[i * 4 + 1], bgra[i * 4]);
        }

        // graphic control extension: delay, no transparency, disposal keep
        out_.put('\x21');
        out_.put('\xF9');
        out_.put('\x04');
        out_.put('\x04');
        u16(out_, delay_cs_);
        out_.put('\0');  // transparent color index (unused)
        out_.put('\0');  // block terminator

        // image descriptor: full frame
        out_.put(',');
        u16(out_, 0);
        u16(out_, 0);
        u16(out_, width_);
        u16(out_, height_);
        out_.put('\0');

        out_.put(static_cast<char>(8));  // LZW minimum code size
        write_sub_blocks(out_, lzw_encode(indexed.data(), indexed.size()));
        ++frames_;
    }

    void GifWriter::close()
    {
        if (closed_)
        {
            return;
        }
        out_.put(';');  // trailer
        closed_ = true;
    }
}
