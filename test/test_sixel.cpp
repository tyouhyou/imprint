// Sixel presenter tests (P4 demo target): structural checks of the DCS
// stream — header/footer, palette definitions, band separators, RLE —
// and byte-identical determinism, the same discipline as test_gif.
// Palette-percentage expectations follow the build's Color::depth (the
// test_pixel_traits pattern): at 16bpp the accessors read expanded
// (bits << 3), so a 255 source reads back 248 and encodes as 97%.
#include "test.hpp"

#include <string>
#include <vector>

#include "logging.hpp"
#include "shell/sixel.hpp"

using namespace zb::ui;

namespace
{
    std::string encode_color4(core::Color a, core::Color b)
    {
        core::Color buf[8] = {a, b, a, b, a, b, a, b};  // 4x2
        return zb::shell::sixel::encode_frame(buf, 4, 2);
    }

    // collects the LW lines one encode_frame call emits
    struct log_probe
    {
        std::vector<std::string> messages;
        log_probe()
        {
            zb::Logging::set_log_handle(
                [this](const zb::Logging_Level level, const std::string &msg)
                {
                    if (level == zb::Logging_Level::warn)
                    {
                        messages.push_back(msg);
                    }
                });
        }
        ~log_probe() { zb::Logging::set_log_handle(); }
        log_probe(const log_probe &) = delete;
        log_probe &operator=(const log_probe &) = delete;
    };

    // 5-bit-exact fixture colors: channels of the form 8*v (+ k, k < 8)
    // read back at 16bpp as an injective function of v, so distinct
    // (r,g,b) 5-bit triples stay distinct at both depths. low_blue has
    // the blue channel at 0, high_blue at 255 (red/green shifted +1) --
    // the two families are disjoint at both depths via the blue channel.
    // i ↦ (i & 31, (i >> 5) & 31) is injective for i < 1024.
    core::Color low_blue(const int i)
    {
        return core::Color::from(static_cast<uint8_t>(8 * (i & 31)),
                                 static_cast<uint8_t>(8 * ((i >> 5) & 31)), 0,
                                 255);
    }

    core::Color high_blue(const int i)
    {
        return core::Color::from(static_cast<uint8_t>(8 * (i & 31) + 1),
                                 static_cast<uint8_t>(8 * ((i >> 5) & 31) + 1),
                                 255, 255);
    }

    // ---- minimal decode of the presenter's own DCS output ----

    struct reg_def
    {
        int r;
        int g;
        int b;
        bool operator==(const reg_def &o) const { return r == o.r && g == o.g && b == o.b; }
        bool operator!=(const reg_def &o) const { return !(*this == o); }
    };

    // one band: the register definitions in emission order and, per
    // register, the per-column 6-bit masks (RLE expanded)
    struct parsed_band
    {
        std::vector<reg_def> regs;
        std::vector<std::vector<int>> masks;  // [register][column]
    };

    // digits at [i, to), or -1 when there is no number there; a run
    // longer than 9 digits cannot fit an int, so it reports -1 too —
    // the presenter never emits a number that long
    std::size_t parse_num(const std::string &s, std::size_t i, const std::size_t to,
                          int &out)
    {
        int v = 0;
        int digits = 0;
        while (i < to && s[i] >= '0' && s[i] <= '9')
        {
            if (++digits <= 9)
            {
                v = v * 10 + (s[i] - '0');
            }
            ++i;
        }
        out = (digits > 0 && digits <= 9) ? v : -1;
        return i;
    }

    // parses the band segment [from, to): '#' reg ';2;' R ';' G ';' B
    // followed by that register's column data (raw cells or '!len cell'
    // RLE) up to the next '#'. EXPECTs document the format; every read
    // is bounds-guarded so a malformed stream fails the test instead of
    // reading out of bounds. Every pass covers every column (zero masks
    // emit as '?'), so each '#' restarts the column cursor.
    parsed_band parse_band(const std::string &s, const std::size_t from,
                           const std::size_t to, const int width)
    {
        parsed_band band;
        std::size_t i = from;
        std::size_t reg = 0;
        int col = 0;
        bool open = false;  // a register pass is open
        while (i < to)
        {
            if (s[i] == '#')
            {
                if (open)
                {
                    EXPECT(col == width);  // the previous pass covered all columns
                }
                int n = -1;
                i = parse_num(s, i + 1, to, n);
                EXPECT(n >= 0 && n == static_cast<int>(band.regs.size()));
                if (n < 0 || static_cast<std::size_t>(n) != band.regs.size() ||
                    i + 3 > to || s.compare(i, 3, ";2;") != 0)
                {
                    break;  // malformed
                }
                i += 3;
                reg_def def{-1, -1, -1};
                bool ok = true;
                i = parse_num(s, i, to, def.r);
                ok = ok && def.r >= 0 && i < to && s[i] == ';';
                i = parse_num(s, i + 1, to, def.g);
                ok = ok && def.g >= 0 && i < to && s[i] == ';';
                i = parse_num(s, i + 1, to, def.b);
                ok = ok && def.b >= 0;
                EXPECT(ok);
                if (!ok)
                {
                    break;
                }
                band.regs.push_back(def);
                band.masks.emplace_back(static_cast<std::size_t>(width), 0);
                reg = band.regs.size() - 1;
                open = true;
                col = 0;
            }
            else if (s[i] == '$')
            {
                ++i;  // redraw separator between passes: nothing to decode
            }
            else if (s[i] == '!')
            {
                int len = -1;
                i = parse_num(s, i + 1, to, len);
                EXPECT(open && len >= 1 && i < to);
                if (!open || len < 1 || i >= to)
                {
                    break;
                }
                const int bits = s[i++] - '?';
                EXPECT(bits >= 0 && bits < 64);
                // a run overrunning the band is a stream the terminal
                // would reject: fail it, never truncate it silently
                EXPECT(col + len <= width);
                for (int k = 0; k < len && col < width; ++k)
                {
                    band.masks[reg][static_cast<std::size_t>(col++)] = bits;
                }
            }
            else
            {
                EXPECT(open);
                if (!open)
                {
                    break;
                }
                const int bits = s[i++] - '?';
                EXPECT(bits >= 0 && bits < 64);
                if (col < width)
                {
                    band.masks[reg][static_cast<std::size_t>(col++)] = bits;
                }
            }
        }
        if (open)
        {
            EXPECT(col == width);  // the last pass covered all columns
        }
        return band;
    }

    // splits the DCS body into band segments and parses each ('-' appears
    // only as the band separator in this stream: data chars are '?' + 0..63)
    std::vector<parsed_band> parse_frame(const std::string &s, const int width)
    {
        std::vector<parsed_band> bands;
        const std::size_t body = s.find('#');
        EXPECT(body != std::string::npos);
        if (body == std::string::npos)
        {
            return bands;
        }
        const std::size_t foot = s.size() - 2;  // the ESC \ footer
        std::size_t from = body;
        for (std::size_t i = body; i < foot; ++i)
        {
            if (s[i] == '-')
            {
                bands.push_back(parse_band(s, from, i, width));
                from = i + 1;
            }
        }
        bands.push_back(parse_band(s, from, foot, width));
        return bands;
    }

    int popcount6(int m)
    {
        int n = 0;
        while (m != 0)
        {
            n += m & 1;
            m >>= 1;
        }
        return n;
    }

    // Full-coverage lock for the quantized frames (uniform-column
    // fixtures): every pixel written exactly once, every column covered
    // by exactly one register whose mask holds all the row bits.
    void expect_full_coverage(const parsed_band &band, const int width, const int rows)
    {
        long bits = 0;
        for (int x = 0; x < width; ++x)
        {
            int owner = -1;
            for (std::size_t r = 0; r < band.masks.size(); ++r)
            {
                const int m = band.masks[r][static_cast<std::size_t>(x)];
                EXPECT(m >= 0 && m < 64);
                bits += popcount6(m);
                if (m != 0)
                {
                    EXPECT(owner == -1);
                    owner = static_cast<int>(r);
                }
            }
            EXPECT(owner >= 0);  // the column has register coverage
            if (owner >= 0)
            {
                EXPECT(band.masks[static_cast<std::size_t>(owner)]
                                 [static_cast<std::size_t>(x)] == (1 << rows) - 1);
            }
        }
        EXPECT(bits == static_cast<long>(width) * rows);
    }
}

int test_sixel()
{
    // one color, one band: raster attrs + one palette + data + ST
    {
        core::Color buf[16] = {};
        for (auto &c : buf)
        {
            c = core::Color::from(255, 0, 0, 255);
        }
        const std::string s = zb::shell::sixel::encode_frame(buf, 4, 4);
        EXPECT(s.substr(0, 3) == "\x1bPq");
        EXPECT(s.find("\"1;1;4;4") != std::string::npos);
        // 32bpp: 255 -> 100; 16bpp: 255 truncates to the 5-bit 31,
        // reads back 248 -> 97
        EXPECT(s.find(core::Color::depth == 32 ? "#0;2;100;0;0" : "#0;2;97;0;0") != std::string::npos);
        EXPECT(s.substr(s.size() - 2) == "\x1b\\");
        // 4 columns of the same mask (all 4 band rows red -> bits 15 ->
        // '?'+15 = 'N'), run of 4 collapses to !4N
        EXPECT(s.find("!4N") != std::string::npos);
        // no band separator within one band
        EXPECT(s.find('-') == std::string::npos);
    }

    // two colors in one band: two registers, '$' between the redraw
    // passes, zero-mask columns still emitted ('?')
    {
        const std::string s = encode_color4(core::Color::from(255, 0, 0, 255),
                                            core::Color::from(0, 0, 255, 255));
        // same depth split as the one-color case (red then blue register)
        EXPECT(s.find(core::Color::depth == 32 ? "#0;2;100;0;0" : "#0;2;97;0;0") != std::string::npos);
        EXPECT(s.find(core::Color::depth == 32 ? "#1;2;0;0;100" : "#1;2;0;0;97") != std::string::npos);
        EXPECT(s.find('$') != std::string::npos);
    }

    // 7 rows: two bands -> exactly one '-' separator
    {
        core::Color buf[28] = {};
        for (auto &c : buf)
        {
            c = core::Color::from(0, 255, 0, 255);
        }
        const std::string s = zb::shell::sixel::encode_frame(buf, 4, 7);
        EXPECT(s.find('-') != std::string::npos);
        std::size_t seps = 0;
        for (const char ch : s)
        {
            seps += (ch == '-') ? 1 : 0;
        }
        EXPECT(seps == 1);
    }

    // determinism: the same buffer encodes byte-identically
    {
        const std::string a = encode_color4(core::Color::from(1, 2, 3, 255),
                                            core::Color::from(4, 5, 6, 255));
        const std::string b = encode_color4(core::Color::from(1, 2, 3, 255),
                                            core::Color::from(4, 5, 6, 255));
        EXPECT(a == b);
        EXPECT(!a.empty());
    }

    // X-13, recast by FI-10: a band over the 250-register budget no
    // longer drops colors — the whole frame switches to the frame-level
    // deterministic median-cut palette. Quantization is normal
    // operation: no diagnostic fires (the old one-LW-per-frame latch is
    // gone), and the stream stays complete and byte-identical across
    // encodes.
    // Depth-aware fixture as before: 16bpp quantizes a channel to 5
    // bits, so a gray ramp holds only 32 distinct colors and would never
    // overflow — the distinctness has to come from channel spread to be
    // depth-neutral (the same trap as the palette-percentage expectations
    // above).
    {
        constexpr int w = 260;  // > 250 distinct colors in one band
        constexpr int h = 6;
        std::vector<core::Color> buf(static_cast<std::size_t>(w) * h);
        for (int i = 0; i < w; ++i)
        {
            // channel triples that stay distinct after the 16bpp 5-bit
            // quantization too (r*3, g*5, b*9 mod 256: 256 distinct
            // triples at both depths, so the band overflows in every
            // configuration)
            const auto r = static_cast<uint8_t>((i * 3) & 0xFF);
            const auto g = static_cast<uint8_t>((i * 5) & 0xFF);
            const auto b = static_cast<uint8_t>((i * 9) & 0xFF);
            for (int y = 0; y < h; ++y)
            {
                buf[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(i)] =
                    core::Color::from(r, g, b, 255);
            }
        }
        log_probe probe;
        const std::string s = zb::shell::sixel::encode_frame(buf.data(), w, h);
        // quantization is normal operation, not a warning: zero LW
        EXPECT(probe.messages.empty());
        // the frame is still well formed and still deterministic
        EXPECT(s.substr(0, 3) == "\x1bPq");
        EXPECT(s.substr(s.size() - 2) == "\x1b\\");
        const std::string again = zb::shell::sixel::encode_frame(buf.data(), w, h);
        EXPECT(s == again);
        // decode the quantized stream: the palette is present, capped at
        // the register budget, every channel reads back in the 0..100
        // sixel range, every column carries register coverage and every
        // pixel is written exactly once (zero dropped)
        const std::vector<parsed_band> bands = parse_frame(s, w);
        EXPECT(bands.size() == 1);
        EXPECT(bands[0].regs.size() == 250);  // 256 distinct colors: full budget
        for (const reg_def &d : bands[0].regs)
        {
            EXPECT(d.r >= 0 && d.r <= 100);
            EXPECT(d.g >= 0 && d.g <= 100);
            EXPECT(d.b >= 0 && d.b <= 100);
        }
        expect_full_coverage(bands[0], w, 6);
    }

    // FI-10 seamlessness: with the frame-level palette the same source
    // color maps to the same register on both sides of a band boundary.
    // Rows 0-5 hold 260 distinct low-blue colors (band 0 overflows the
    // register budget); row 6 holds color #0 plus 259 distinct high-blue
    // colors, so a per-band palette could not coincide with band 0's.
    {
        constexpr int w = 260;
        constexpr int h = 7;
        std::vector<core::Color> buf(static_cast<std::size_t>(w) * h);
        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < 6; ++y)
            {
                buf[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] =
                    low_blue(x);
            }
            // the shared color sits exactly on the band seam
            buf[static_cast<std::size_t>(6) * w + static_cast<std::size_t>(x)] =
                x == 0 ? low_blue(0) : high_blue(x);
        }
        log_probe probe;
        const std::string s = zb::shell::sixel::encode_frame(buf.data(), w, h);
        EXPECT(probe.messages.empty());
        const std::vector<parsed_band> bands = parse_frame(s, w);
        EXPECT(bands.size() == 2);
        // the frame-level palette is re-emitted identically in both bands
        // (a per-band palette regression would diverge here)
        EXPECT(bands[0].regs == bands[1].regs);
        EXPECT(bands[0].regs.size() == 250);
        expect_full_coverage(bands[0], w, 6);
        expect_full_coverage(bands[1], w, 1);
        // the shared color's register: same number on both sides of the
        // seam, all six band-0 rows in it, the single band-1 row in it
        int reg0 = -1;
        int reg1 = -1;
        for (std::size_t r = 0; r < bands[0].masks.size(); ++r)
        {
            if (bands[0].masks[r][0] != 0)
            {
                EXPECT(reg0 == -1);  // uniform column: one owning register
                reg0 = static_cast<int>(r);
            }
        }
        for (std::size_t r = 0; r < bands[1].masks.size(); ++r)
        {
            if (bands[1].masks[r][0] != 0)
            {
                EXPECT(reg1 == -1);
                reg1 = static_cast<int>(r);
            }
        }
        EXPECT(reg0 >= 0 && reg0 == reg1);
        const std::string again = zb::shell::sixel::encode_frame(buf.data(), w, h);
        EXPECT(s == again);
    }

    // FI-10 overflow boundary, low side: exactly 250 distinct colors per
    // band stay on the exact per-band path — each band keeps its own
    // first-seen palette, so the same register number decodes to
    // DIFFERENT colors in the two bands (a frame-level palette would
    // re-emit identical definitions, and a drop path would leave holes).
    {
        constexpr int w = 250;
        constexpr int h = 12;  // two full bands
        std::vector<core::Color> buf(static_cast<std::size_t>(w) * h);
        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < 6; ++y)
            {
                buf[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] =
                    low_blue(x);
            }
            for (int y = 6; y < 12; ++y)
            {
                buf[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] =
                    high_blue(x);
            }
        }
        log_probe probe;
        const std::string s = zb::shell::sixel::encode_frame(buf.data(), w, h);
        EXPECT(probe.messages.empty());
        const std::vector<parsed_band> bands = parse_frame(s, w);
        EXPECT(bands.size() == 2);
        EXPECT(bands[0].regs.size() == 250);
        EXPECT(bands[1].regs.size() == 250);
        // per-band first-seen palettes: register 0 is each band's first
        // column color ((0,0,0) vs blue-255 after the 0..100 encode; the
        // 16bpp readback expands 255's 5 bits to 248 -> 97, the same
        // depth split as the palette-percentage expectations above)
        EXPECT(bands[0].regs[0].r == 0 && bands[0].regs[0].g == 0 &&
               bands[0].regs[0].b == 0);
        EXPECT(bands[1].regs[0].r == 0 && bands[1].regs[0].g == 0 &&
               bands[1].regs[0].b == (core::Color::depth == 32 ? 100 : 97));
        EXPECT(bands[0].regs != bands[1].regs);
        expect_full_coverage(bands[0], w, 6);
        expect_full_coverage(bands[1], w, 6);
        const std::string again = zb::shell::sixel::encode_frame(buf.data(), w, h);
        EXPECT(s == again);
    }

    // FI-10 overflow boundary, high side: 251 distinct colors in a band
    // tip the frame onto the quantized path — palette capped at 250
    // registers and still zero dropped pixels (the old drop path would
    // leave the 251st color's pixels unwritten).
    {
        constexpr int w = 251;
        constexpr int h = 6;
        std::vector<core::Color> buf(static_cast<std::size_t>(w) * h);
        for (int x = 0; x < w; ++x)
        {
            for (int y = 0; y < h; ++y)
            {
                buf[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] =
                    low_blue(x);
            }
        }
        log_probe probe;
        const std::string s = zb::shell::sixel::encode_frame(buf.data(), w, h);
        EXPECT(probe.messages.empty());
        const std::vector<parsed_band> bands = parse_frame(s, w);
        EXPECT(bands.size() == 1);
        EXPECT(bands[0].regs.size() == 250);
        expect_full_coverage(bands[0], w, 6);
        const std::string again = zb::shell::sixel::encode_frame(buf.data(), w, h);
        EXPECT(s == again);
    }

    // X-13: a frame within the budget logs nothing at all
    {
        log_probe probe;
        const std::string s = encode_color4(core::Color::from(255, 0, 0, 255),
                                            core::Color::from(0, 0, 255, 255));
        EXPECT(probe.messages.empty());
        EXPECT(s.find('$') != std::string::npos);
    }

    return test::report("sixel");
}
