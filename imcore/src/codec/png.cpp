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

    auto img_width_u = (uint32_t)img_width;
    auto img_height_u = (uint32_t)img_height;
    auto row_stride = img_width_u * 4; // stb always yields RGBA with req_comp=4
    image_info iminf
    {
        img_width_u,
        img_height_u,
        row_stride,
        4
    };
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

int Image::read_png(
    Graphics &g,
    const std::string &file_name,
    const int &start_x,
    const int &start_y)
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
        uint8_t a = 0, r = 0, g = 0, b = 0;
        for (int i = 0; i < (int)png_inf.image_width; i++)
        {
            r = *buf++;
            g = *buf++;
            b = *buf++;
            a = *buf++;
            png_img.push_back(Color::from(r, g, b, a));
        }
        return true;
    };
    auto rst = read_png_file(file_name, png_loaded, png_read_row);
    if (0 != rst)
    {
        LE << "Error in reading png file. error code : " << rst;
        return rst;
    }
    g.draw_image(png_img.data(), png_inf.image_width, png_inf.image_height, png_inf.image_width, start_x, start_y);

    return 0;
}

int Image::write_png_file(
    const std::string file_name,
    const image_info &img_inf,
    on_writing_image_row write_row)
{
    auto img_width = img_inf.image_width;
    auto img_height = img_inf.image_height;
    if (img_width == 0 || img_height == 0)
    {
        LE << "PNG write request with zero width/height.";
        return 1;
    }

    // stb has no streaming writer: collect the rows first (RGBA, 4 comp),
    // then hand the whole buffer to stbi_write_png.
    auto row_stride = img_width * 4;
    std::vector<unsigned char> image_data((size_t)row_stride * img_height);
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
        std::memcpy(image_data.data() + (size_t)i * row_stride, row.data(), row_stride);
    }

    FILE *outfile = fopen(file_name.c_str(), "wb");
    if (!outfile)
    {
        LE << "Failed openning png file " << file_name << ").";
        return 3;
    }
    fclose(outfile);

    if (0 == stbi_write_png(file_name.c_str(), (int)img_width, (int)img_height, 4, image_data.data(), (int)row_stride))
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
    auto row_bit_stride = gsize.width * 4; // rgba format

    std::vector<unsigned char> row(row_bit_stride);
    image_info img_inf{
        (uint32_t)gsize.width,
        (uint32_t)gsize.height,
        (uint32_t)row_bit_stride,
        4};

    auto dat = (Color *)g.data();
    auto write_row = [&](std::vector<unsigned char> &row)
    {
        for (int i = 0; i < gsize.width; i++)
        {
            if (cur_row >= gsize.height)
                break;

            auto cd = dat[cur_row * gsize.width + i];
            row.push_back(cd.rgb.r);
            row.push_back(cd.rgb.g);
            row.push_back(cd.rgb.b);
#if COLOR_DEPTH == 16
            // color16's single alpha bit must expand to full opacity, not
            // the byte 0/1: a 16bpp screenshot was ~fully transparent
            row.push_back(cd.rgb.a != 0 ? 0xFF : 0x00);
#else
            // 32bpp alpha is already a full byte: keep it, PNG is lossless
            row.push_back(cd.rgb.a);
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