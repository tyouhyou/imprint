/*
 * GIF89a writer implementation (codec/gif.hpp). Promoted from the
 * showcase frame recorder (Batch V-0, tool-placement rule): a pure
 * encode capability of the codec family, no framework state. The
 * block structure it emits is pinned by test_gif (signature, GCT,
 * NETSCAPE loop, per-frame GCE with its block terminator — the missing
 * terminator once made strict decoders reject the file, bb916e7 —
 * image descriptor, sub-block stream, trailer).
 */

#include "codec/gif.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace zb::ui
{
    namespace
    {
        void u16(std::ostream &out, const std::size_t v)
        {
            out.put(static_cast<char>(v & 0xFF));
            out.put(static_cast<char>((v >> 8) & 0xFF));
        }

        /* nearest entry of a custom palette: exact hash first, then a
         * deterministic linear scan (minimum distance, lowest index wins
         * ties); memoized per writer */
        uint8_t palette_index_of(
            std::unordered_map<std::uint32_t, uint8_t> &memo, const GifPalette &pal,
            const uint8_t r, const uint8_t g, const uint8_t b)
        {
            const std::uint32_t key = (static_cast<std::uint32_t>(r) << 16) |
                                      (static_cast<std::uint32_t>(g) << 8) | b;
            auto it = memo.find(key);
            if (it != memo.end())
            {
                return it->second;
            }
            std::size_t best = 0;
            long best_d = -1;
            for (std::size_t i = 0; i < pal.count; ++i)
            {
                const long dr = static_cast<long>(r) - pal.rgb[i * 3 + 0];
                const long dg = static_cast<long>(g) - pal.rgb[i * 3 + 1];
                const long db = static_cast<long>(b) - pal.rgb[i * 3 + 2];
                const long d = dr * dr + dg * dg + db * db;
                if (best_d < 0 || d < best_d)
                {
                    best_d = d;
                    best = i;
                }
            }
            memo.emplace(key, static_cast<uint8_t>(best));
            return static_cast<uint8_t>(best);
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

    // --- GifPaletteBuilder (median cut, deterministic) -----------------

    void GifPaletteBuilder::add_frame(const core::Color *pixels, const std::size_t n)
    {
        if (colors_.empty())
        {
            colors_.reserve(4096);
            counts_.reserve(4096);
        }
        for (std::size_t i = 0; i < n; ++i)
        {
            const core::Color &c = pixels[i];
            // 8-bit-normalized channels (A-19); the alpha byte is ignored
            const std::uint32_t key = (static_cast<std::uint32_t>(c.r()) << 16) |
                                      (static_cast<std::uint32_t>(c.g()) << 8) |
                                      c.b();
            // colors_ stays sorted (binary-search insert); the first
            // occurrence carries the weight
            auto it = std::lower_bound(colors_.begin(), colors_.end(), key);
            if (it != colors_.end() && *it == key)
            {
                ++counts_[static_cast<std::size_t>(it - colors_.begin())];
            }
            else
            {
                colors_.insert(it, key);
                counts_.insert(counts_.begin() +
                                   static_cast<std::size_t>(it - colors_.begin()),
                               1);
            }
        }
    }

    GifPalette GifPaletteBuilder::palette(const std::size_t max_colors) const
    {
        GifPalette out;
        if (colors_.empty())
        {
            return out;
        }
        // one bucket per distinct color; a bucket = [begin, end) over the
        // (sorted) color index range
        struct bucket
        {
            std::size_t begin;
            std::size_t end;
        };
        std::vector<bucket> buckets{{0, colors_.size()}};

        auto channel = [&](const std::uint32_t packed, const int ch) -> uint8_t
        {
            return static_cast<uint8_t>((packed >> ((2 - ch) * 8)) & 0xFF);
        };
        auto channel_range = [&](const bucket &b, const int ch)
        {
            uint8_t lo = 255;
            uint8_t hi = 0;
            for (std::size_t i = b.begin; i < b.end; ++i)
            {
                const uint8_t v = channel(colors_[i], ch);
                lo = (v < lo) ? v : lo;
                hi = (v > hi) ? v : hi;
            }
            return hi - lo;
        };
        auto bucket_weight = [&](const bucket &b)
        {
            std::size_t s = 0;
            for (std::size_t i = b.begin; i < b.end; ++i)
            {
                s += counts_[i];
            }
            return s;
        };

        const std::size_t target = max_colors < 1 ? 1 : (max_colors > 256 ? 256 : max_colors);
        while (buckets.size() < target)
        {
            // split the bucket with the widest channel range (ties: the
            // heavier, then the earliest) along its widest channel at the
            // weight median; unsplittable buckets stop the loop
            std::size_t best = buckets.size();
            int best_range = 0;
            std::size_t best_weight = 0;
            int best_ch = 0;
            for (std::size_t i = 0; i < buckets.size(); ++i)
            {
                const std::size_t n = buckets[i].end - buckets[i].begin;
                if (n < 2)
                {
                    continue;
                }
                for (int ch = 0; ch < 3; ++ch)
                {
                    const int r = channel_range(buckets[i], ch);
                    if (r == 0)
                    {
                        continue;
                    }
                    const std::size_t wgt = bucket_weight(buckets[i]);
                    if (r > best_range ||
                        (r == best_range && (wgt > best_weight ||
                                             (wgt == best_weight && best == buckets.size()))))
                    {
                        best = i;
                        best_range = r;
                        best_weight = wgt;
                        best_ch = ch;
                    }
                }
            }
            if (best == buckets.size())
            {
                break;
            }
            bucket &b = buckets[best];
            // weight median index within the bucket (channel-sorted
            // because colors_ is packed R>>G>>B: any channel prefix is
            // sorted within a fixed higher-prefix block)
            std::size_t acc = 0;
            const std::size_t half = bucket_weight(b) / 2 + (bucket_weight(b) & 1);
            std::size_t split = b.begin + 1;
            for (std::size_t i = b.begin; i < b.end; ++i)
            {
                acc += counts_[i];
                if (acc >= half)
                {
                    split = i + 1;
                    break;
                }
            }
            if (split <= b.begin || split >= b.end)
            {
                split = b.begin + (b.end - b.begin) / 2;
                if (split >= b.end)
                {
                    break;  // cannot split further
                }
            }
            bucket right{split, b.end};
            b.end = split;
            buckets.push_back(right);
            (void)best_ch;
        }

        // palette entry = the bucket's weighted average, buckets kept in
        // index order (deterministic; no re-sort needed)
        std::size_t count = 0;
        for (const bucket &b : buckets)
        {
            if (b.begin == b.end || count >= 256)
            {
                continue;
            }
            std::uint32_t sr = 0, sg = 0, sb = 0, w = 0;
            for (std::size_t i = b.begin; i < b.end; ++i)
            {
                const std::uint32_t c = colors_[i];
                const std::uint32_t k = counts_[i];
                sr += ((c >> 16) & 0xFF) * k;
                sg += ((c >> 8) & 0xFF) * k;
                sb += (c & 0xFF) * k;
                w += k;
            }
            out.rgb[count * 3 + 0] = static_cast<uint8_t>(sr / w);
            out.rgb[count * 3 + 1] = static_cast<uint8_t>(sg / w);
            out.rgb[count * 3 + 2] = static_cast<uint8_t>(sb / w);
            ++count;
        }
        out.count = count;
        return out;
    }

    GifWriter::GifWriter(const char *path, const std::size_t width,
                         const std::size_t height, const std::size_t delay_cs)
        : palette_(nullptr), out_(path, std::ios::binary), width_(width),
          height_(height), delay_cs_(delay_cs)
    {
        write_header(nullptr);
    }

    GifWriter::GifWriter(const char *path, const std::size_t width,
                         const std::size_t height, const std::size_t delay_cs,
                         const GifPalette &palette)
        : palette_(&palette), out_(path, std::ios::binary), width_(width),
          height_(height), delay_cs_(delay_cs)
    {
        write_header(&palette);
    }

    void GifWriter::write_header(const GifPalette *pal)
    {
        out_.write("GIF89a", 6);
        u16(out_, width_);
        u16(out_, height_);
        out_.put(static_cast<char>(0xF7));  // GCT present, 8 bits/primary, 256 entries
        out_.put('\0');                     // background color index
        out_.put('\0');                     // aspect ratio
        if (pal == nullptr)
        {
            for (std::size_t i = 0; i < 256; ++i)
            {
                const std::size_t r = i / 36, g = (i / 6) % 6, b = i % 6;
                out_.put(static_cast<char>(r * 51));
                out_.put(static_cast<char>(g * 51));
                out_.put(static_cast<char>(b * 51));
            }
        }
        else
        {
            std::size_t i = 0;
            for (; i < pal->count; ++i)
            {
                out_.put(static_cast<char>(pal->rgb[i * 3 + 0]));
                out_.put(static_cast<char>(pal->rgb[i * 3 + 1]));
                out_.put(static_cast<char>(pal->rgb[i * 3 + 2]));
            }
            // the GCT is padded to 256 (black)
            for (; i < 256; ++i)
            {
                out_.put('\0');
                out_.put('\0');
                out_.put('\0');
            }
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

    void GifWriter::add_frame(const core::Color *pixels)
    {
        std::vector<uint8_t> indexed(static_cast<std::size_t>(width_ * height_));
        for (std::size_t i = 0; i < indexed.size(); ++i)
        {
            // 8-bit-normalized accessors (A-19): correct at 32bpp and a
            // documented quantize at 16bpp; the alpha byte is ignored
            const core::Color &c = pixels[i];
            indexed[i] = palette_ != nullptr
                             ? palette_index_of(nearest_, *palette_, c.r(), c.g(), c.b())
                             : palette_index(c.r(), c.g(), c.b());
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
