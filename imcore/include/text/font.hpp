#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include <functional>
#include <string>
#include <vector>
#include "../core/graphics.hpp"
#include "glyph_provider.hpp"

namespace zb::ui
{
    /*
     * void func(bmp_buf, bmp_width, bmp_height, bmp_pitch, start_x, start_y)
     */
    using on_read_word = std::function<void(unsigned char *, const int &, const int &, const int &, const int &, const int &)>;

    // TODO: If needed, cache all the Font before use them to save memory and performance.
    //       Make I/F for that case.
    // TODO: set transformation, set encode(shift-jis etc.), default to unicode/utf-8
    // TODO: charater orentation.  at this time being, vertically from left to right only.
    // TODO: string measure -> height, width etc.
    //       https://freetype.org/freetype2/docs/glyphs/glyphs-2.html

    class Font
    {
    public:
        class error : public std::exception
        {
        public:
            error(const std::string msg) noexcept
            {
                this->msg = msg;
            }

            const char *what() noexcept
            {
                return msg.c_str();
            }

        private:
            std::string msg;
        };

    public:
        Font(
            const std::string &font_file,
            const int &font_face_idx = 0) noexcept(false);

        ~Font();

        Font &set_char_size_in_pt(
            const int &pt,
            const int &canvas_width,
            const int &canvas_height) noexcept(false);

        Font &set_char_size_in_px(
            const int &px) noexcept(false);

        /*
         * Measures a string without rendering it: the total advance width
         * plus the font's line height and ascent at the current char size.
         * Used for text alignment (see zb::ui::Label).
         */
        text_metrics measure(
            const char16_t *str,
            const int &str_len) const;

        /* line height / ascent only, without scanning a string */
        text_metrics line_metrics() const;

        /* whether the font has a glyph for the code unit (0 -> not covered) */
        bool covers(const char16_t ch) const;

        const Font &write(
            const char16_t *str,
            const int &str_len,
            const int &start_x,
            const int &start_y,
            on_read_word draw_fun) const;

        // TODO: str_len should be calculate automatically
        const Font &write(
            core::Graphics &g,
            const char16_t *str,
            const int &str_len,
            const int &start_x,
            const int &start_y,
            const core::Color &front_color = core::colors::White) const;

    private:
        Font() = delete;
        Font(const Font &) = delete;

        const int inch_to_point = 72;
        const int default_font_size_px = 16;

        // at this time being, just for unicode only
        void draw_character(
            const char16_t &str,
            int &advance_x,
            int &advance_y,
            on_read_word draw_fun) const;

        void draw_alphamap(
            core::Graphics &g,
            const unsigned char *img, // array of pixel alpha channel values (8 bit).
            const int &img_width,
            const int &img_height,
            const int &img_row_stride,
            const int &start_x,
            const int &start_y,
            const core::Color &front_color) const;

        // TODO: make one set of objects for one Font into a struct,
        //       and multiple sets can be loaded into a map by load()
        //       load(set_name, font_file, idx ...)

        // RAII for the FreeType handles: a constructor that throws mid-way
        // must not leak (the destructor never runs on a half-constructed
        // object, so the guards do the releasing instead)
        struct FTLibraryGuard
        {
            FT_Library p = nullptr;
            ~FTLibraryGuard()
            {
                if (p)
                {
                    FT_Done_FreeType(p);
                }
            }
        };
        struct FTFaceGuard
        {
            FT_Face p = nullptr;
            ~FTFaceGuard()
            {
                if (p)
                {
                    FT_Done_Face(p);
                }
            }
        };

        FT_Library library = nullptr;
        FT_Face face = nullptr;

        // reused across draw_alphamap calls to avoid a heap allocation per
        // glyph; mutable because Font::write is const. Font is not
        // thread-safe anyway (FT_Load_Char mutates the face)
        mutable std::vector<core::Color> _alphamap_color_map;
    };

    /*
     * GlyphProvider adapter over a Font (USE_FONT builds only, see
     * docs/code-contract.md section 2.4). A widget uses it as its primary
     * provider and falls back to BitmapProvider for code units that are
     * not covered by the font. Never throws (FreeType glyph load failures
     * are skipped silently).
     */
    class FreeTypeProvider : public GlyphProvider
    {
    public:
        explicit FreeTypeProvider(const Font *font) : font_(font) {}

        bool covers(const char16_t ch) const override;
        text_metrics measure(const char16_t *str, const int len) const override;
        text_metrics line_metrics() const override;
        void write(core::Graphics &g, const char16_t *str, const int len,
                   const int x, const int y, const core::Color &color) const override;

    private:
        const Font *font_;
    };
}