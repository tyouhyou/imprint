#include "font.hpp"
#include "logging.hpp"

using namespace std;
using namespace zb;
using namespace zb::ui;
using namespace zb::ui::core;

Font::~Font()
{
    if (nullptr != this->face)
    {
        FT_Done_Face(this->face);
    }
    if (nullptr != this->library)
    {
        FT_Done_FreeType(library);
    }
}

Font::Font(
    const string &font_file,
    int font_face_idx)
{
    LD << "Open font file : " << font_file;

    // guards own the handles until the constructor has fully succeeded;
    // any throw below releases them (no leaked FT_Library/FT_Face)
    FTLibraryGuard library_guard;
    FTFaceGuard face_guard;

    FT_Error err = FT_Init_FreeType(&library_guard.p);
    if (FT_Err_Ok != err)
    {
        LE << "Initiating ft library failed. (err = " << err << ")";
        throw Font::error("Initiating ft library failed.");
    }

    err = FT_New_Face(library_guard.p, font_file.c_str(), font_face_idx, &face_guard.p);
    if (FT_Err_Unknown_File_Format == err)
    {
        LE << "The specified font file was not found. (err = " << err << ")";
        throw Font::error("The specified font file was not found.");
    }
    else if (FT_Err_Ok != err)
    {
        LE << "The specified font file is broken, or cannot be opened. (err = " << err << ")";
        throw Font::error("The specified font file is broken, or cannot be opened.");
    }

    // both handles are valid now: adopt them; the guards stay armed until
    // every remaining throw-capable call has succeeded (a throw here would
    // otherwise leak the handles -- ~Font() never runs on a failed
    // constructor)
    library = library_guard.p;
    face = face_guard.p;

    set_char_size_in_px(default_font_size_px);

    // constructor succeeded: release the guards' ownership
    library_guard.p = nullptr;
    face_guard.p = nullptr;
}

Font &Font::set_char_size_in_pt(
    int pt,
    int canvas_width,
    int canvas_height)
{
    if (FT_Err_Ok != FT_Set_Char_Size(face, 0, pt * inch_to_point, canvas_width, canvas_height))
    {
        auto msg = "Failed in setting character size in point.";
        LE << msg;
        throw Font::error(msg);
    }
    return *this;
}

Font &Font::set_char_size_in_px(
    int px)
{
    if (FT_Err_Ok != FT_Set_Pixel_Sizes(face, 0, px))
    {
        auto msg = "Failed in setting character size in pixel.";
        LE << msg;
        throw Font::error(msg);
    }
    return *this;
}

text_metrics Font::measure(
    const char16_t *str,
    int str_len) const
{
    int width = 0;
    for (int i = 0; i < str_len; i++)
    {
        // no FT_LOAD_RENDER: load the outline/metrics without rasterizing
        if (FT_Err_Ok == FT_Load_Char(face, str[i], FT_LOAD_DEFAULT))
        {
            // advance is a 26.6 fixed-point value (1/64 pixel)
            width += face->glyph->advance.x / 64;
        }
    }
    return {
        width,
        (face->size->metrics.height) / 64,
        (face->size->metrics.ascender) / 64};
}

text_metrics Font::line_metrics() const
{
    return {0, (face->size->metrics.height) / 64, (face->size->metrics.ascender) / 64};
}

bool Font::covers(const char16_t ch) const
{
    return FT_Get_Char_Index(face, ch) != 0;
}

const Font &Font::write(
    const char16_t *str,
    int str_len,
    int start_x,
    int start_y,
    on_read_word draw_fun) const
{
    if (nullptr == str || str_len <= 0 || !draw_fun)
    {
        return *this;
    }

    int next_pen_x = start_x,
        next_pen_y = start_y;
    for (int i = 0; i < str_len; i++)
    {
        draw_character(str[i], next_pen_x, next_pen_y, draw_fun);
    }

    return *this;
}

const Font &Font::write(
    Graphics &g,
    const char16_t *str,
    int str_len,
    int start_x,
    int start_y,
    const Color &front_color) const
{
    auto fun = [&](unsigned char *img,
                   int w,
                   int h,
                   int p,
                   int x,
                   int y)
    {
        draw_alphamap(g, img, w, h, p, x, y, front_color);
    };
    write(str, str_len, start_x, start_y, fun);

    return *this;
}

// TODO: at this time being, unicode only. provide a converter?
void Font::draw_character(
    const char16_t &chr,
    int &next_pen_x,
    int &next_pen_y,
    on_read_word draw_fun) const
{
    if (FT_Err_Ok != FT_Load_Char(face, chr, FT_LOAD_RENDER))
    {
        LW << "Character " << (unsigned)chr << " cannot be rendered because the font file does not contain this character.";
        return;
    }

    draw_fun(face->glyph->bitmap.buffer,
             face->glyph->bitmap.width,
             face->glyph->bitmap.rows,
             face->glyph->bitmap.pitch,
             next_pen_x + face->glyph->bitmap_left,
             next_pen_y - face->glyph->bitmap_top);

    // advance is a 26.6 fixed-point value (1/64 pixel), not a point(=1/72 inch).
    next_pen_x += face->glyph->advance.x / 64;
    next_pen_y += face->glyph->advance.y / 64;
}

void Font::draw_alphamap(
    Graphics &g,
    const unsigned char *img, // array of pixel alpha channel values (8 bit).
    int img_width,
    int img_height,
    int img_row_stride,
    int start_x,
    int start_y,
    const Color &front_color) const
{
    // reuse the map buffer across glyphs: reserve once, then resize without
    // allocation on the hot path (A-8, 2026-08-28)
    const auto len = img_width * img_height;
    if (_alphamap_color_map.capacity() < static_cast<size_t>(len))
    {
        _alphamap_color_map.reserve(len);
    }
    _alphamap_color_map.resize(len);
    for (int i = 0; i < len; i++)
    {
        Color c = front_color;
        c.rgb.a = img[i];
        _alphamap_color_map[i] = c;
    }
    auto bak_alpha = g.is_alpha_enabled();
    g.enable_alpha(true);
    g.draw_image(_alphamap_color_map.data(), img_width, img_height, img_row_stride, start_x, start_y);
    g.enable_alpha(bak_alpha);
}
bool FreeTypeProvider::covers(const char16_t ch) const
{
    return font_ != nullptr && font_->covers(ch);
}

text_metrics FreeTypeProvider::measure(const char16_t *str, const int len) const
{
    return font_ != nullptr ? font_->measure(str, len) : text_metrics{};
}

text_metrics FreeTypeProvider::line_metrics() const
{
    return font_ != nullptr ? font_->line_metrics() : text_metrics{};
}

void FreeTypeProvider::write(core::Graphics &g, const char16_t *str, const int len,
                             const int x, const int y, const core::Color &color) const
{
    if (font_ != nullptr)
    {
        font_->write(g, str, len, x, y, color);
    }
}
