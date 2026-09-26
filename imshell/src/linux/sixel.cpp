// Sixel presenter implementation (see shell/sixel.hpp). Format: DCS
// `Pq` + raster attributes + per-band palette definitions + per-register
// column bitmaps + `ESC \`. Bit 0 of a data byte is the TOP row of the
// 6-row band; data chars are `?` + the 6-bit mask.
#include "shell/sixel.hpp"

#include <cstdio>
#include <string>
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
    }

    std::string encode_frame(const void *buffer, const int width, const int height)
    {
        auto *pixels = static_cast<const zb::ui::core::Color *>(
            const_cast<void *>(buffer));
        std::string s;
        s += "\x1bPq";
        s += "\"1;1;";
        append_uint(s, width);
        s += ';';
        append_uint(s, height);

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
                    if (!found && regs.size() < kMaxRegisters)
                    {
                        regs.push_back(c);
                    }
                }
            }

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
                // zero mask still emits ('?' = all off) — the cursor
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
                            if (c.r() == regs[reg].r() && c.g() == regs[reg].g() &&
                                c.b() == regs[reg].b())
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
        s += "\x1b\\";
        return s;
    }

    void write_frame(const void *buffer, const int width, const int height)
    {
        const std::string s = encode_frame(buffer, width, height);
        std::fwrite(s.data(), 1, s.size(), stdout);
        std::fflush(stdout);
    }
}
