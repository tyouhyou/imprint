// Sixel presenter implementation (see shell/sixel.hpp). Format: DCS
// `Pq` + raster attributes + per-band palette definitions + per-register
// column bitmaps + `ESC \`. Bit 0 of a data byte is the TOP row of the
// 6-row band; data chars are `?` + the 6-bit mask.
//
// Two encodings share the emission loop (FI-10): while every band fits
// the 250-register cap the frame goes out on the exact per-band path,
// byte for byte the historical stream; when any band would overflow,
// the whole frame switches to a frame-level deterministic median-cut
// palette and no pixel is dropped.
#include "shell/sixel.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/color.hpp"

namespace zb::shell::sixel
{
    namespace
    {
        constexpr int kBandRows = 6;
        constexpr std::size_t kMaxRegisters = 250;  // leave room below 256

        void append_uint(std::string &s, const int v)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", v);
            s += buf;
        }

        // the 0..100 sixel color channels (deterministic rounding)
        int channel100(const uint8_t v)
        {
            return (v * 100 + 127) / 255;
        }

        // RLE: runs of 4+ identical data cells collapse to `!len char`
        void append_run(std::string &s, const int len, const char ch)
        {
            if (len >= 4)
            {
                s += '!';
                append_uint(s, len);
                s += ch;
            }
            else
            {
                for (int i = 0; i < len; ++i)
                {
                    s += ch;
                }
            }
        }

        void append_header(std::string &s, const int width, const int height)
        {
            s += "\x1bPq";
            s += "\"1;1;";
            append_uint(s, width);
            s += ';';
            append_uint(s, height);
        }

        // packed rgb identity of a color (the alpha byte is not part of
        // it -- the exact path compares r/g/b only); 24 bits, so a frame
        // holds at most 2^24 distinct colors
        std::uint32_t color_key(const zb::ui::core::Color &c)
        {
            return (static_cast<std::uint32_t>(c.r()) << 16) |
                   (static_cast<std::uint32_t>(c.g()) << 8) |
                   static_cast<std::uint32_t>(c.b());
        }

        // One band of the stream: per-register color definitions, each
        // followed by its per-column mask pass ('$' redraw between the
        // passes). Shared by both encodings: the exact path passes
        // pixel_reg == nullptr (membership is exact color equality
        // against the band-local palette), the quantized path passes
        // the per-pixel register map against the frame-level palette.
        void emit_band(std::string &s, const zb::ui::core::Color *pixels,
                       const int width, const int y0, const int band_h,
                       const std::vector<zb::ui::core::Color> &regs,
                       const std::uint8_t *pixel_reg)
        {
            for (std::size_t reg = 0; reg < regs.size(); ++reg)
            {
                if (reg > 0)
                {
                    s += '$';  // redraw over the same band
                }
                s += '#';
                append_uint(s, static_cast<int>(reg));
                s += ";2;";
                append_uint(s, channel100(regs[reg].r()));
                s += ';';
                append_uint(s, channel100(regs[reg].g()));
                s += ';';
                append_uint(s, channel100(regs[reg].b()));

                // one column mask per pixel column: bit b set when the
                // pixel at (x, y0+b) carries this register's color. A
                // zero mask still emits ('?' = all off) -- the cursor
                // must advance through every column or the redraw
                // passes (the '$' return) misalign.
                int run_len = 0;
                char run_ch = 0;
                for (int x = 0; x <= width; ++x)
                {
                    char ch = 0;
                    if (x < width)
                    {
                        int bits = 0;
                        for (int b = 0; b < band_h; ++b)
                        {
                            const zb::ui::core::Color c =
                                pixels[(y0 + b) * width + x];
                            const bool hit =
                                pixel_reg != nullptr
                                    ? pixel_reg[(static_cast<std::size_t>(y0 + b) *
                                                 static_cast<std::size_t>(width)) +
                                                static_cast<std::size_t>(x)] == reg
                                    : (c.r() == regs[reg].r() &&
                                       c.g() == regs[reg].g() &&
                                       c.b() == regs[reg].b());
                            if (hit)
                            {
                                bits |= 1 << b;
                            }
                        }
                        ch = static_cast<char>('?' + bits);
                    }
                    // ch == 0 at x == width flushes the tail run
                    if (ch != 0 && ch == run_ch)
                    {
                        ++run_len;
                    }
                    else
                    {
                        if (run_len > 0)
                        {
                            append_run(s, run_len, run_ch);
                        }
                        run_ch = ch;
                        run_len = ch != 0 ? 1 : 0;
                    }
                }
            }
        }

        // ---- exact per-band encoding (the historical byte stream) ----

        // Emits the frame with a first-seen palette per band. Returns
        // false when a band would exceed the register budget; the
        // partial stream is discarded and the caller restarts the whole
        // frame on the quantized path below. Nothing is dropped and
        // nothing is logged: quantization is normal operation (FI-10),
        // not a warning.
        bool encode_exact(const zb::ui::core::Color *pixels, const int width,
                          const int height, std::string &s)
        {
            append_header(s, width, height);

            std::vector<zb::ui::core::Color> regs;
            for (int y0 = 0; y0 < height; y0 += kBandRows)
            {
                if (y0 > 0)
                {
                    s += '-';  // next sixel band
                }
                const int band_h =
                    (height - y0) < kBandRows ? (height - y0) : kBandRows;

                // palette: distinct band colors in first-seen order
                regs.clear();
                for (int y = y0; y < y0 + band_h; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        const zb::ui::core::Color c = pixels[y * width + x];
                        bool found = false;
                        for (const zb::ui::core::Color &r : regs)
                        {
                            if (r.r() == c.r() && r.g() == c.g() && r.b() == c.b())
                            {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            if (regs.size() == kMaxRegisters)
                            {
                                // the band would exceed the register
                                // budget: restart the whole frame on the
                                // quantized path
                                return false;
                            }
                            regs.push_back(c);
                        }
                    }
                }

                emit_band(s, pixels, width, y0, band_h, regs, nullptr);
            }
            s += "\x1b\\";
            return true;
        }

        // ---- frame-level median-cut fallback (FI-10) ----

        // one distinct frame color: first-seen scan order + pixel weight
        struct frame_color
        {
            zb::ui::core::Color color;
            std::size_t count;
        };

        // a median-cut bucket: distinct-color table entries, the total
        // pixel weight, the weighted per-channel sums and the per-channel
        // extents over the entries
        struct bucket
        {
            std::vector<std::size_t> entries;
            std::size_t pixels = 0;
            std::uint64_t sum_r = 0;
            std::uint64_t sum_g = 0;
            std::uint64_t sum_b = 0;
            int lo_r = 0;
            int hi_r = 0;
            int lo_g = 0;
            int hi_g = 0;
            int lo_b = 0;
            int hi_b = 0;
        };

        int channel_of(const zb::ui::core::Color &c, const int axis)
        {
            switch (axis)
            {
            case 0:
                return c.r();
            case 1:
                return c.g();
            default:
                return c.b();
            }
        }

        // the bucket's color-domain width: the widest channel range
        int extent(const bucket &b)
        {
            int e = b.hi_r - b.lo_r;
            if (b.hi_g - b.lo_g > e)
            {
                e = b.hi_g - b.lo_g;
            }
            if (b.hi_b - b.lo_b > e)
            {
                e = b.hi_b - b.lo_b;
            }
            return e;
        }

        // (re)computes a bucket's weight, sums and extents from its entries
        void measure_bucket(const std::vector<frame_color> &colors, bucket &b)
        {
            b.pixels = 0;
            b.sum_r = 0;
            b.sum_g = 0;
            b.sum_b = 0;
            b.lo_r = 255;
            b.hi_r = 0;
            b.lo_g = 255;
            b.hi_g = 0;
            b.lo_b = 255;
            b.hi_b = 0;
            for (const std::size_t e : b.entries)
            {
                const frame_color &fc = colors[e];
                const int r = fc.color.r();
                const int g = fc.color.g();
                const int bl = fc.color.b();
                b.pixels += fc.count;
                b.sum_r += static_cast<std::uint64_t>(fc.count) *
                           static_cast<std::uint64_t>(r);
                b.sum_g += static_cast<std::uint64_t>(fc.count) *
                           static_cast<std::uint64_t>(g);
                b.sum_b += static_cast<std::uint64_t>(fc.count) *
                           static_cast<std::uint64_t>(bl);
                if (r < b.lo_r)
                {
                    b.lo_r = r;
                }
                if (r > b.hi_r)
                {
                    b.hi_r = r;
                }
                if (g < b.lo_g)
                {
                    b.lo_g = g;
                }
                if (g > b.hi_g)
                {
                    b.hi_g = g;
                }
                if (bl < b.lo_b)
                {
                    b.lo_b = bl;
                }
                if (bl > b.hi_b)
                {
                    b.hi_b = bl;
                }
            }
        }

        // whole-frame distinct colors in first-seen scan order with pixel
        // counts; src_index receives each pixel's table index (bounded by
        // the 24-bit color space, so it fits uint32_t)
        void scan_frame_colors(const zb::ui::core::Color *pixels, const int width,
                               const int height, std::vector<frame_color> &colors,
                               std::vector<std::uint32_t> &src_index)
        {
            // pure memo: the map only dedups, order comes from the vector
            std::unordered_map<std::uint32_t, std::size_t> first_seen;
            colors.clear();
            src_index.clear();
            src_index.reserve(static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height));
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const zb::ui::core::Color c =
                        pixels[(static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(width)) +
                               static_cast<std::size_t>(x)];
                    const std::uint32_t key = color_key(c);
                    const auto it = first_seen.find(key);
                    if (it == first_seen.end())
                    {
                        first_seen.emplace(key, colors.size());
                        src_index.push_back(static_cast<std::uint32_t>(colors.size()));
                        colors.push_back({c, 1});
                    }
                    else
                    {
                        ++colors[it->second].count;
                        src_index.push_back(static_cast<std::uint32_t>(it->second));
                    }
                }
            }
        }

        // deterministic median cut over the frame's distinct colors
        // (FI-10 design note): repeatedly split the heaviest bucket that
        // still holds two distinct colors -- ties by widest color extent,
        // then earliest bucket order -- on its widest channel (ties
        // R > G > B) at the weighted median of the axis-sorted entries
        // (equal axis values keep first-seen order). A bucket's
        // representative is its integer weighted average, rounded half
        // up. Stops at 250 buckets or when nothing splittable remains.
        // Register k is bucket k's representative; the left child takes
        // the parent's slot, the right child is appended, so bucket order
        // is creation order.
        std::vector<zb::ui::core::Color> median_cut_palette(
            const std::vector<frame_color> &colors)
        {
            std::vector<zb::ui::core::Color> palette;
            if (colors.empty())
            {
                return palette;  // degenerate frame: no registers
            }

            std::vector<bucket> buckets;
            bucket all;
            all.entries.resize(colors.size());
            for (std::size_t i = 0; i < colors.size(); ++i)
            {
                all.entries[i] = i;
            }
            measure_bucket(colors, all);
            buckets.push_back(std::move(all));

            while (buckets.size() < kMaxRegisters)
            {
                std::size_t best = buckets.size();  // "none" sentinel
                for (std::size_t i = 0; i < buckets.size(); ++i)
                {
                    if (buckets[i].entries.size() < 2)
                    {
                        continue;  // under 2 distinct colors: terminal
                    }
                    if (best == buckets.size())
                    {
                        best = i;
                    }
                    else if (buckets[i].pixels > buckets[best].pixels ||
                             (buckets[i].pixels == buckets[best].pixels &&
                              extent(buckets[i]) > extent(buckets[best])))
                    {
                        best = i;
                    }
                }
                if (best == buckets.size())
                {
                    break;  // nothing splittable remains
                }

                // split buckets[best] on its widest channel at the
                // weighted median of the axis-sorted entries
                bucket &b = buckets[best];
                int axis = 0;
                int widest = b.hi_r - b.lo_r;
                if (b.hi_g - b.lo_g > widest)
                {
                    axis = 1;
                    widest = b.hi_g - b.lo_g;
                }
                if (b.hi_b - b.lo_b > widest)
                {
                    axis = 2;
                }
                const std::size_t n = b.entries.size();
                std::sort(b.entries.begin(), b.entries.end(),
                          [&colors, axis](const std::size_t a, const std::size_t c)
                          {
                              const int va = channel_of(colors[a].color, axis);
                              const int vc = channel_of(colors[c].color, axis);
                              if (va != vc)
                              {
                                  return va < vc;
                              }
                              return a < c;  // equal axis values: first-seen order
                          });
                std::size_t split = n - 1;
                std::size_t acc = 0;
                for (std::size_t i = 0; i < n; ++i)
                {
                    acc += colors[b.entries[i]].count;
                    if (acc * 2 >= b.pixels)
                    {
                        split = i;
                        break;
                    }
                }
                if (split > n - 2)
                {
                    split = n - 2;  // the right half keeps one entry
                }

                bucket right;
                right.entries.assign(
                    b.entries.begin() + static_cast<std::ptrdiff_t>(split + 1),
                    b.entries.end());
                b.entries.resize(split + 1);
                measure_bucket(colors, b);
                measure_bucket(colors, right);
                buckets.push_back(std::move(right));  // b is dead here
            }

            palette.reserve(buckets.size());
            for (const bucket &b : buckets)
            {
                const std::size_t total = b.pixels;  // >= 1 by construction
                // integer weighted average, rounded half up; the average
                // of 0..255 channels cannot exceed 255
                const auto rep = [total](const std::uint64_t sum)
                {
                    return static_cast<uint8_t>((sum + total / 2) / total);
                };
                palette.push_back(
                    zb::ui::core::Color::from(rep(b.sum_r), rep(b.sum_g),
                                              rep(b.sum_b), 255));
            }
            return palette;
        }

        // frame-level quantized encoding: one deterministic median-cut
        // palette for the whole frame (no band seams), every pixel mapped
        // to its nearest register by squared distance, equal distances
        // keeping the lower register. No pixel is dropped, nothing logged.
        std::string encode_quantized(const zb::ui::core::Color *pixels,
                                     const int width, const int height)
        {
            std::vector<frame_color> colors;
            std::vector<std::uint32_t> src_index;
            scan_frame_colors(pixels, width, height, colors, src_index);

            const std::vector<zb::ui::core::Color> regs =
                median_cut_palette(colors);

            // nearest register per distinct color: registers are scanned
            // in order and replaced only by a strictly closer one, so the
            // tie keeps the lower register; a pure per-color memo cannot
            // change the mapping. Registers are capped at kMaxRegisters
            // (250), so a register index fits a byte.
            std::vector<std::uint8_t> reg_of(colors.size(), 0);
            for (std::size_t i = 0; i < colors.size(); ++i)
            {
                std::uint8_t best = 0;
                int best_d = 0;
                for (std::size_t r = 0; r < regs.size(); ++r)
                {
                    const int dr = colors[i].color.r() - regs[r].r();
                    const int dg = colors[i].color.g() - regs[r].g();
                    const int db = colors[i].color.b() - regs[r].b();
                    const int d = dr * dr + dg * dg + db * db;
                    if (r == 0 || d < best_d)
                    {
                        best_d = d;
                        best = static_cast<std::uint8_t>(r);
                    }
                }
                reg_of[i] = best;
            }

            std::vector<std::uint8_t> pixel_reg(src_index.size());
            for (std::size_t i = 0; i < src_index.size(); ++i)
            {
                pixel_reg[i] = reg_of[src_index[i]];
            }

            std::string s;
            append_header(s, width, height);
            for (int y0 = 0; y0 < height; y0 += kBandRows)
            {
                if (y0 > 0)
                {
                    s += '-';  // next sixel band
                }
                const int band_h =
                    (height - y0) < kBandRows ? (height - y0) : kBandRows;
                emit_band(s, pixels, width, y0, band_h, regs, pixel_reg.data());
            }
            s += "\x1b\\";
            return s;
        }
    }

    std::string encode_frame(const void *buffer, const int width, const int height)
    {
        auto *pixels = static_cast<const zb::ui::core::Color *>(
            const_cast<void *>(buffer));

        // byte compatibility first (FI-10): while every band fits the
        // register budget the frame goes out on the exact per-band path,
        // identical to the historical stream. Any band over the budget
        // switches the WHOLE frame to the deterministic median-cut
        // palette: no band seams, no dropped pixels, no diagnostic.
        std::string s;
        if (encode_exact(pixels, width, height, s))
        {
            return s;
        }
        return encode_quantized(pixels, width, height);
    }

    void write_frame(const void *buffer, const int width, const int height)
    {
        const std::string s = encode_frame(buffer, width, height);
        std::fwrite(s.data(), 1, s.size(), stdout);
        std::fflush(stdout);
    }
}
