/*
 * Build-time TTF glyph subset rasterizer (batch S2, code-contract 2.4).
 *
 * Usage:
 *   ttf_subset <font.ttf> <pixel_size> <codepoints.txt> <out.hpp>
 *
 * Reads the sorted code-unit list (decimal, one per line, produced by
 * font_subset.py --codepoints-out), rasterizes each unit the font
 * contains at the given pixel size with the vendored stb_truetype, and
 * writes ttf_glyphs.hpp — the runtime table consumed by
 * TtfSubsetProvider. Code units missing from the font are warned and
 * skipped (they fall back to the 5x7 bitmap provider at runtime).
 * Fatal errors (unreadable font/list, not a font) fail the build.
 *
 * Host tool only: like ui_embed, it runs in host builds; embedded
 * targets keep the 5x7 bitmap path.
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace
{
    struct Entry
    {
        uint32_t cp;
        uint16_t advance;
        uint8_t width;
        uint8_t height;
        int16_t xoff;
        int16_t yoff;
        uint32_t alpha_off;
    };

    std::vector<uint8_t> read_file(const char *path)
    {
        std::vector<uint8_t> bytes;
        FILE *f = std::fopen(path, "rb");
        if (f == nullptr)
        {
            return bytes;
        }
        std::fseek(f, 0, SEEK_END);
        const long len = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (len > 0)
        {
            bytes.resize(static_cast<size_t>(len));
            if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size())
            {
                bytes.clear();
            }
        }
        std::fclose(f);
        return bytes;
    }

    bool write_atomic(const char *path, const std::string &body)
    {
        const std::string tmp = std::string(path) + ".tmp";
        FILE *f = std::fopen(tmp.c_str(), "wb");
        if (f == nullptr)
        {
            return false;
        }
        const bool written = std::fwrite(body.data(), 1, body.size(), f) == body.size();
        std::fclose(f);
        if (!written)
        {
            std::remove(tmp.c_str());
            return false;
        }
        // skip the replace when nothing changed (no rebuild churn)
        const std::vector<uint8_t> old = read_file(path);
        const bool same = old.size() == body.size() &&
                          std::memcmp(old.data(), body.data(), old.size()) == 0;
        if (same)
        {
            std::remove(tmp.c_str());
            return true;
        }
        // Windows rename does not replace an existing target: drop the
        // old header first (a build tool; the brief non-atomic window
        // costs nothing)
        std::remove(path);
        return std::rename(tmp.c_str(), path) == 0;
    }
}  // namespace

int main(const int argc, const char **argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "usage: ttf_subset <font.ttf> <pixel_size> <codepoints.txt> <out.hpp>\n");
        return 1;
    }
    const char *font_path = argv[1];
    const int pixel_size = std::atoi(argv[2]);
    const char *cp_path = argv[3];
    const char *out_path = argv[4];
    if (pixel_size < 4 || pixel_size > 128)
    {
        std::fprintf(stderr, "ttf_subset: pixel size out of range [4..128]: %d\n", pixel_size);
        return 1;
    }

    const std::vector<uint8_t> font_bytes = read_file(font_path);
    if (font_bytes.empty())
    {
        std::fprintf(stderr, "ttf_subset: cannot read font: %s\n", font_path);
        return 1;
    }
    stbtt_fontinfo font;
    const int offset = stbtt_GetFontOffsetForIndex(font_bytes.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&font, font_bytes.data(), offset))
    {
        std::fprintf(stderr, "ttf_subset: not a usable font: %s\n", font_path);
        return 1;
    }

    const float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(pixel_size));
    int ascent = 0, descent = 0, linegap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &linegap);
    const int ascent_px = static_cast<int>(std::llround(ascent * scale));
    const int descent_px = static_cast<int>(std::llround(descent * scale));  // negative
    const int line_height = ascent_px - descent_px;

    // the code-unit list (already sorted by the generator)
    std::vector<uint32_t> units;
    {
        FILE *f = std::fopen(cp_path, "r");
        if (f == nullptr)
        {
            std::fprintf(stderr, "ttf_subset: cannot read codepoint list: %s\n", cp_path);
            return 1;
        }
        unsigned long v;
        while (std::fscanf(f, "%lu", &v) == 1)
        {
            if (v >= 32 && v <= 0xFFFF)
            {
                units.push_back(static_cast<uint32_t>(v));
            }
        }
        std::fclose(f);
    }

    std::vector<Entry> entries;
    std::vector<uint8_t> alpha;
    for (const uint32_t cp : units)
    {
        const int glyph = stbtt_FindGlyphIndex(&font, static_cast<int>(cp));
        if (glyph == 0)
        {
            std::fprintf(stderr, "ttf_subset: U+%04X not in %s; skipped (bitmap fallback)\n",
                         cp, font_path);
            continue;
        }
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&font, glyph, &advance, &lsb);

        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char *bmp = stbtt_GetCodepointBitmap(
            &font, scale, scale, static_cast<int>(cp), &w, &h, &xoff, &yoff);

        Entry e;
        e.cp = cp;
        e.advance = static_cast<uint16_t>(std::llround(advance * scale));
        e.width = static_cast<uint8_t>(w);
        e.height = static_cast<uint8_t>(h);
        e.xoff = static_cast<int16_t>(xoff);
        e.yoff = static_cast<int16_t>(yoff);
        e.alpha_off = static_cast<uint32_t>(alpha.size());
        if (bmp != nullptr)
        {
            alpha.insert(alpha.end(), bmp, bmp + static_cast<size_t>(w) * h);
            STBTT_free(bmp, nullptr);
        }
        entries.push_back(e);
    }

    std::string out;
    out += "// GENERATED by tools/ttf_subset -- do not edit.\n";
    out += "// font: " + std::string(font_path) + " @ " + std::to_string(pixel_size) + "px, ";
    out += std::to_string(entries.size()) + " glyphs (code-contract 2.4, plan 2)\n";
    out += "#pragma once\n\n#include <cstdint>\n\nnamespace zb::ui\n{\n";
    out += "    struct TtfGlyph\n    {\n";
    out += "        char16_t ch;\n";
    out += "        uint16_t advance;  // pen advance, px\n";
    out += "        uint8_t width, height;\n";
    out += "        int16_t xoff, yoff;  // bitmap top-left relative to (pen, baseline)\n";
    out += "        uint32_t alpha_off;  // into kTtfGlyphAlpha\n";
    out += "    };\n\n";
    out += "    inline constexpr int kTtfPixelSize = " + std::to_string(pixel_size) + ";\n";
    out += "    inline constexpr int kTtfAscent = " + std::to_string(ascent_px) + ";\n";
    out += "    // positive distance from the baseline to the bottom\n";
    out += "    inline constexpr int kTtfDescent = " + std::to_string(-descent_px) + ";\n";
    out += "    inline constexpr int kTtfLineHeight = " + std::to_string(line_height) + ";\n\n";
    out += "    // sorted by ch (binary search)\n";
    out += "    inline constexpr TtfGlyph kTtfGlyphs[] = {\n";
    char line[192];
    for (const Entry &e : entries)
    {
        std::snprintf(line, sizeof line,
                      "        {static_cast<char16_t>(0x%04X), %u, %u, %u, %d, %d, %u},\n",
                      e.cp, e.advance, e.width, e.height, e.xoff, e.yoff, e.alpha_off);
        out += line;
    }
    out += "    };\n\n";
    out += "    inline constexpr size_t kTtfGlyphCount =\n";
    out += "        sizeof(kTtfGlyphs) / sizeof(kTtfGlyphs[0]);\n\n";
    out += "    inline constexpr uint8_t kTtfGlyphAlpha[] = {\n       ";
    for (size_t i = 0; i < alpha.size(); ++i)
    {
        char byte[8];
        std::snprintf(byte, sizeof byte, " 0x%02X,", alpha[i]);
        out += byte;
        if ((i + 1) % 16 == 0)
        {
            out += "\n       ";
        }
    }
    out += "\n    };\n}\n";

    if (!write_atomic(out_path, out))
    {
        std::fprintf(stderr, "ttf_subset: cannot write %s\n", out_path);
        return 1;
    }
    return 0;
}
