/*
 * Implementation of png file processing with the vendored stb_image /
 * stb_image_write codecs (third_party/stb, see third_party/README.md).
 *
 * replaced libpng (2026-08, backlog batch D): decode happens in one shot
 * (stb has no row-streaming), so the file is fully decoded before
 * image_loaded() is called; the Image API and its callback contract
 * (image_loaded -> one read_row per scanline, RGBA) are unchanged.
 */

#ifdef USE_PNG

#include "image.hpp"
#include "logging.hpp"
#include "stb_image.h"
#include "stb_image_write.h"
#include <climits>
#include <cstring>

#define PNG_SIG_BYTES 8

static const unsigned char kPngSignature[PNG_SIG_BYTES] = {137, 80, 78, 71, 13, 10, 26, 10};

using namespace zb;
using namespace zb::ui;
using namespace zb::ui::core;

int Image::read_png_file(
    const std::string file_name,
    on_loading_image image_loaded,
    on_reading_image_row read_row)
{
    FILE *infile = fopen(file_name.c_str(), "rb");
    if (!infile)
    {
        LE << "Openning png file failed. (" << file_name << ")";
        return 1;
    }

    unsigned char magicbuf[PNG_SIG_BYTES];
    bool sig_ok = PNG_SIG_BYTES == fread(magicbuf, 1, PNG_SIG_BYTES, infile) &&
                  0 == std::memcmp(magicbuf, kPngSignature, PNG_SIG_BYTES);
    fclose(infile);
    if (!sig_ok)
    {
        LE << "The specified file is not a PNG file. (" << file_name << ")";
        return 2;
    }

    int img_width = 0, img_height = 0, comp = 0;
    unsigned char *data = stbi_load(file_name.c_str(), &img_width, &img_height, &comp, 4);
    if (!data)
    {
        LE << "Decoding png file failed. (" << file_name << ") reason: " << stbi_failure_reason();
        return 3;
    }
    if (img_width <= 0 || img_height <= 0)
    {
        LE << "PNG file reports zero width/height, refusing to process. (" << file_name << ")";
        stbi_image_free(data);
        return 4;
    }

    auto img_width_u = static_cast<uint32_t>(img_width);
    auto img_height_u = static_cast<uint32_t>(img_height);
    auto row_stride = img_width_u * 4;  // stb always yields RGBA with req_comp=4
    image_info iminf{
        img_width_u,
        img_height_u,
        row_stride,
        4};
    if (!image_loaded(iminf))
    {
        LE << "Error occurred when set image info in image_loaded callback.";
        stbi_image_free(data);
        return 5;
    }

    auto row_pointer = data;
    for (uint32_t i = 0; i < img_height_u; i++)
    {
        if (!read_row(row_pointer))
        {
            LE << "Error occurred when processing image row data.";
            stbi_image_free(data);
            return 6;
        }
        row_pointer += row_stride;
    }

    stbi_image_free(data);
    return 0;
}

namespace
{
    // the row callbacks are arbitrary host code: a throw on the way out
    // (bad_alloc from a caller's push_back, say) must still free the stbi
    // decode buffer — the buffer is released on every exit path of the
    // memory-variant readers, exceptions included
    struct stbi_decode_buffer
    {
        explicit stbi_decode_buffer(unsigned char *p)
            : data(p) {}
        ~stbi_decode_buffer()
        {
            if (data != nullptr)
            {
                stbi_image_free(data);
            }
        }
        stbi_decode_buffer(const stbi_decode_buffer &) = delete;
        stbi_decode_buffer &operator=(const stbi_decode_buffer &) = delete;
        unsigned char *data;
    };
}  // namespace

int Image::read_png_memory(
    const uint8_t *bytes,
    size_t n,
    on_loading_image image_loaded,
    on_reading_image_row read_row)
{
    if (!bytes || n == 0)
    {
        LE << "Reading png from memory failed: empty buffer.";
        return 1;
    }

    bool sig_ok = n >= PNG_SIG_BYTES &&
                  0 == std::memcmp(bytes, kPngSignature, PNG_SIG_BYTES);
    if (!sig_ok)
    {
        LE << "The specified memory buffer is not a PNG image.";
        return 2;
    }

    int img_width = 0, img_height = 0, comp = 0;
    // stb's length parameter is int-bounded; the clamp cannot truncate a
    // decodable stream (stb consumes only what the image needs)
    auto len = n > static_cast<size_t>(INT_MAX)
                 ? INT_MAX
                 : static_cast<int>(n);
    unsigned char *data = stbi_load_from_memory(bytes, len, &img_width, &img_height, &comp, 4);
    stbi_decode_buffer buffer{data};
    if (!data)
    {
        LE << "Decoding png from memory failed. reason: " << stbi_failure_reason();
        return 3;
    }
    if (img_width <= 0 || img_height <= 0)
    {
        LE << "PNG image reports zero width/height, refusing to process.";
        return 4;
    }

    auto img_width_u = static_cast<uint32_t>(img_width);
    auto img_height_u = static_cast<uint32_t>(img_height);
    auto row_stride = img_width_u * 4;  // stb always yields RGBA with req_comp=4
    image_info iminf{
        img_width_u,
        img_height_u,
        row_stride,
        4};
    if (!image_loaded(iminf))
    {
        LE << "Error occurred when set image info in image_loaded callback.";
        return 5;
    }

    auto row_pointer = data;
    for (uint32_t i = 0; i < img_height_u; i++)
    {
        if (!read_row(row_pointer))
        {
            LE << "Error occurred when processing image row data.";
            return 6;
        }
        row_pointer += row_stride;
    }

    return 0;
}

int Image::read_png(
    Graphics &g,
    const std::string &file_name,
    int start_x,
    int start_y)
{
    std::vector<Color> png_img;
    image_info png_inf{};
    auto png_loaded = [&png_inf](image_info &im)
    {
        png_inf = im;
        LD << "png_inf width=" << png_inf.image_width << "; height=" << png_inf.image_height << "; comp=" << png_inf.row_stride / png_inf.image_width;
        return true;
    };
    auto png_read_row = [&png_inf, &png_img](unsigned char *buf)
    {
        // ca/cr/cg/cb: `g` is the enclosing read's Graphics target
        uint8_t ca = 0, cr = 0, cg = 0, cb = 0;
        for (int i = 0; i < static_cast<int>(png_inf.image_width); i++)
        {
            cr = *buf++;
            cg = *buf++;
            cb = *buf++;
            ca = *buf++;
            png_img.push_back(Color::from(cr, cg, cb, ca));
        }
        return true;
    };
    auto rst = read_png_file(file_name, png_loaded, png_read_row);
    if (0 != rst)
    {
        LE << "Error in reading png file. error code : " << rst;
        return rst;
    }
    g.draw_image(png_img.data(), static_cast<int>(png_inf.image_width), static_cast<int>(png_inf.image_height), static_cast<int>(png_inf.image_width), start_x, start_y);

    return 0;
}

int Image::write_png_file(
    const std::string file_name,
    const image_info &img_inf,
    on_writing_image_row write_row)
{
    auto img_width = img_inf.image_width;
    auto img_height = img_inf.image_height;
    const auto comps = img_inf.color_components;
    if (img_width == 0 || img_height == 0)
    {
        LE << "PNG write request with zero width/height.";
        return 1;
    }
    if (comps != 3 && comps != 4)
    {
        LE << "PNG write request with unsupported component count "
           << static_cast<int>(comps) << " (3 = RGB, 4 = RGBA).";
        return 5;
    }

    // stb has no streaming writer: collect the rows first, then hand the
    // whole buffer to stbi_write_png. The declared color_components (3 =
    // RGB, 4 = RGBA) decides both the row stride and the encoded channel
    // count -- the info fields are the row contract, not decoration.
    auto row_stride = img_width * comps;
    std::vector<unsigned char> image_data(static_cast<size_t>(row_stride) * img_height);
    std::vector<unsigned char> row(row_stride);
    for (uint32_t i = 0; i < img_height; i++)
    {
        row.clear();
        if (!write_row(row))
        {
            LE << "Error occurred while write data to png file.";
            return 2;
        }
        if (row.size() < row_stride)
            row.resize(row_stride, 0x00);
        std::memcpy(image_data.data() + static_cast<size_t>(i) * row_stride, row.data(), row_stride);
    }

    FILE *outfile = fopen(file_name.c_str(), "wb");
    if (!outfile)
    {
        LE << "Failed openning png file " << file_name << ").";
        return 3;
    }
    fclose(outfile);

    if (0 == stbi_write_png(file_name.c_str(), static_cast<int>(img_width), static_cast<int>(img_height),
                            static_cast<int>(comps), image_data.data(), static_cast<int>(row_stride)))
    {
        // stbi_failure_reason belongs to the READ side (stb_image); it
        // says nothing about a write failure, so it is not reported here
        LE << "Writing png file failed. (" << file_name << ")";
        return 4;
    }
    return 0;
}

int Image::write_png(
    Graphics &g,
    const std::string &file_name)
{
    auto cur_row = 0;
    auto gsize = g.size();
    auto row_bit_stride = gsize.width * 4;  // rgba format

    std::vector<unsigned char> row(static_cast<std::size_t>(row_bit_stride));
    image_info img_inf{
        static_cast<uint32_t>(gsize.width),
        static_cast<uint32_t>(gsize.height),
        static_cast<uint32_t>(row_bit_stride),
        4};

    auto dat = static_cast<const Color *>(g.data());
    auto write_row = [&](std::vector<unsigned char> &row_out)
    {
        for (int i = 0; i < gsize.width; i++)
        {
            if (cur_row >= gsize.height)
                break;

            auto cd = dat[cur_row * gsize.width + i];
            row_out.push_back(cd.r());
            row_out.push_back(cd.g());
            row_out.push_back(cd.b());
#if COLOR_DEPTH == 16
            // color16's single alpha bit must expand to full opacity, not
            // the byte 0/1: a 16bpp screenshot was ~fully transparent
            row_out.push_back(cd.a() != 0 ? 0xFF : 0x00);
#else
            // 32bpp alpha is already a full byte: keep it, PNG is lossless
            row_out.push_back(cd.a());
#endif
        }
        cur_row++;
        return true;
    };

    auto rst = write_png_file(file_name, img_inf, write_row);
    if (0 != rst)
    {
        LE << "capture to png failed. (error code: " << rst << "; file name: " << file_name << ")";
    }
    return rst;
}

#endif