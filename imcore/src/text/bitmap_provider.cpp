#include "bitmap_provider.hpp"

#include <cstdint>

namespace zb::ui
{
    namespace
    {
        constexpr int glyph_width = 5;
        constexpr int glyph_height = 7;
        constexpr int glyph_advance = 6;  // width + 1 pixel spacing

        /*
         * Built-in 5x7 glyphs indexed by (ascii - 32), covering the
         * printable range 32..95 (space through '_'). Bit 4 of a row is the
         * leftmost pixel. Unsupported characters are blank rows.
         * i18n: to add more characters, append glyphs here and extend the
         * index range in BitmapProvider::covers().
         */
        const uint8_t kGlyphs[64][glyph_height] = {
            // 32 ' '
            {},
            // 33 '!'
            {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100},
            // 34 '"'  35 '#'  36 '$'  37 '%'  38 '&'  39 '\''
            {}, {}, {}, {}, {}, {},
            // 40 '('  41 ')'  42 '*'  43 '+'
            {}, {}, {}, {},
            // 44 ','
            {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b01000},
            // 45 '-'
            {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000},
            // 46 '.'
            {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b00000},
            // 47 '/'
            {},
            // 48 '0' .. 57 '9'
            {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},
            {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
            {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
            {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110},
            {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
            {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
            {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
            {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
            {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
            {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100},
            // 58 ':'
            {0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000},
            // 59 ';'  60 '<'  61 '='  62 '>'  63 '?'
            {}, {}, {}, {},
            {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100},
            // 64 '@'
            {},
            // 65 'A' .. 90 'Z'
            {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},  // A
            {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110},  // B
            {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110},  // C
            {0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100},  // D
            {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111},  // E
            {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000},  // F
            {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111},  // G
            {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},  // H
            {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},  // I
            {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100},  // J
            {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001},  // K
            {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111},  // L
            {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001},  // M
            {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001},  // N
            {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},  // O
            {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000},  // P
            {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101},  // Q
            {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001},  // R
            {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110},  // S
            {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100},  // T
            {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},  // U
            {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100},  // V
            {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001},  // W
            {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001},  // X
            {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100},  // Y
            {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111},  // Z
            // 91 '['  92 '\'  93 ']'  94 '^'  95 '_'
            {}, {}, {}, {}, {},
        };

        // normalizes a code unit to the glyph-table index or -1
        int glyph_index(const char16_t ch)
        {
            char16_t cc = ch;
            if (cc >= u'a' && cc <= u'z')
            {
                cc = static_cast<char16_t>(cc - u'a' + u'A');
            }
            if (cc >= 32 && cc < 96)
            {
                return static_cast<int>(cc) - 32;
            }
            return -1;
        }
    }  // namespace

    bool BitmapProvider::covers(const char16_t ch) const
    {
        return glyph_index(ch) >= 0;
    }

    text_metrics BitmapProvider::measure(const char16_t *str, const int len) const
    {
        text_metrics m;
        m.height = glyph_height;
        m.ascent = glyph_height;
        if (str == nullptr || len <= 0)
        {
            return m;
        }
        for (int i = 0; i < len; ++i)
        {
            if (glyph_index(str[i]) >= 0)
            {
                m.width += glyph_advance;
            }
        }
        return m;
    }

    text_metrics BitmapProvider::line_metrics() const
    {
        return {0, glyph_height, glyph_height};
    }

    void BitmapProvider::write(core::Graphics &g, const char16_t *str, const int len,
                               const int x, const int y, const core::Color &color) const
    {
        if (str == nullptr || len <= 0)
        {
            return;
        }
        // y is the baseline (see GlyphProvider): the glyph rows are above it
        const int top = y - glyph_height;
        int pen = x;
        for (int i = 0; i < len; ++i)
        {
            const int idx = glyph_index(str[i]);
            if (idx >= 0)
            {
                const uint8_t *glyph = kGlyphs[idx];
                for (int r = 0; r < glyph_height; ++r)
                {
                    const uint8_t bits = glyph[r];
                    if (bits == 0)
                    {
                        continue;
                    }
                    for (int c = 0; c < glyph_width; ++c)
                    {
                        if ((bits & (1u << (glyph_width - 1 - c))) != 0)
                        {
                            g.draw_pixel(pen + c, top + r, color);
                        }
                    }
                }
                pen += glyph_advance;
            }
            // uncovered code units advance zero width
        }
    }
}  // namespace zb::ui
