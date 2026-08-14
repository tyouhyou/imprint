/*
 * Implementation of jpeg file processing with the vendored stb_image /
 * stb_image_write codecs (third_party/stb, see third_party/README.md).
 *
 * replaced libjpeg (2026-08, backlog batch D): decode happens in one shot
 * (stb has no row-streaming), read yields 24bit RGB (3 comp, the contract
 * this file already enforced); the Image API and its callback contract
 * (image_loaded -> one read_row per scanline) are unchanged.
 */

#ifdef USE_JPEG

#include "image.hpp"
#include "logging.hpp"
#include "stb_image.h"
#include "stb_image_write.h"
#include <cstring>

using namespace zb;
using namespace zb::ui;
using namespace zb::ui::core;

int Image::write_jpeg(
    Graphics &g,
    const std::string &file_name,
    const int &quality)
{
    auto cur_row = 0;
    auto gsize = g.size();
    auto row_bit_stride = gsize.width * 3; // 24bit jpeg only
    image_info img_inf{
        (uint32_t)gsize.width,
        (uint32_t)gsize.height,
        (uint32_t)row_bit_stride,
        3};

    std::vector<unsigned char> row(row_bit_stride);

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
        }
        cur_row++;
        return true;
    };

    auto rst = write_jpeg_file(file_name, img_inf, write_row, quality);
    if (0 != rst)
    {
        LE << "writing jpeg file failed. ( error code :" << rst << "; jpeg file : " << file_name << ")";
    }

    return rst;
}

int Image::write_jpeg_file(
    const std::string file_name,
    const image_info &img_inf,
    on_writing_image_row write_row,
    const int &quality)
{
    auto img_width = img_inf.image_width;
    auto img_height = img_inf.image_height;
    if (img_width == 0 || img_height == 0)
    {
        LE << "JPEG write request with zero width/height.";
        return 1;
    }

    // stb has no streaming writer: collect the rows first (RGB, 3 comp),
    // then hand the whole buffer to stbi_write_jpg.
    auto row_stride = img_width * 3;
    std::vector<unsigned char> image_data((size_t)row_stride * img_height);
    std::vector<unsigned char> row(row_stride);
    for (uint32_t i = 0; i < img_height; i++)
    {
        row.clear();
        if (!write_row(row))
        {
            LE << "Error occurred while write data to jpeg file.";
            return 2;
        }
        if (row.size() < row_stride)
            row.resize(row_stride, 0x00);
        std::memcpy(image_data.data() + (size_t)i * row_stride, row.data(), row_stride);
    }

    FILE *outfile = fopen(file_name.c_str(), "wb");
    if (!outfile)
    {
        LE << "can't open " << file_name;
        return 3;
    }
    fclose(outfile);

    auto q = quality;
    if (q < 1)
        q = 1;
    else if (q > 100)
        q = 100;

    if (0 == stbi_write_jpg(file_name.c_str(), (int)img_width, (int)img_height, 3, image_data.data(), q))
    {
        LE << "Writing jpeg file failed. (" << file_name << ")";
        return 4;
    }
    return 0;
}

int Image::read_jpg(Graphics &g, const std::string &file_name, const int &start_x, const int &start_y)
{
    std::vector<Color> jpg_img;
    image_info jpg_inf{};
    auto jpg_loaded = [&](image_info &im)
    {
        jpg_inf = im;
        LD << "jpg_inf width=" << jpg_inf.image_width << "; height=" << jpg_inf.image_height << "; comp=" << jpg_inf.color_components;
        return true;
    };
    auto jpg_read_row = [&jpg_inf, &jpg_img](unsigned char *buf)
    {
        uint8_t r = 0, g = 0, b = 0;
        for (int i = 0; i < (int)jpg_inf.image_width; i++)
        {
            r = *buf++;
            g = *buf++;
            b = *buf++;
            jpg_img.push_back(Color::from(r, g, b));
        }
        return true;
    };

    auto rst = read_jpeg_file(file_name, jpg_loaded, jpg_read_row);
    if (0 != rst)
    {
        LE << "Error in reading jpeg file. error code : " << rst;
        return rst;
    }

    g.draw_image(jpg_img.data(), jpg_inf.image_width, jpg_inf.image_height, jpg_inf.image_width, start_x, start_y);
    return 0;
}

int Image::read_jpeg_file(
    const std::string file_name,
    on_loading_image image_loaded,
    on_reading_image_row read_row)
{
    // SOI marker (0xFF 0xD8) check for a clearer "not a jpeg" error
    FILE *infile = fopen(file_name.c_str(), "rb");
    if (!infile)
    {
        LE << "can't open " << file_name;
        return 1;
    }
    unsigned char magicbuf[2] = {0, 0};
    bool soi_ok = 2 == fread(magicbuf, 1, 2, infile) && magicbuf[0] == 0xFF && magicbuf[1] == 0xD8;
    fclose(infile);
    if (!soi_ok)
    {
        LE << "The specified file is not a JPEG file. (" << file_name << ")";
        return 2;
    }

    int img_width = 0, img_height = 0, comp = 0;
    unsigned char *data = stbi_load(file_name.c_str(), &img_width, &img_height, &comp, 3);
    if (!data)
    {
        LE << "Decoding jpeg file failed. (" << file_name << ") reason: " << stbi_failure_reason();
        return 3;
    }
    if (img_width <= 0 || img_height <= 0)
    {
        LE << "JPEG file reports zero width/height, refusing to process. (" << file_name << ")";
        stbi_image_free(data);
        return 4;
    }

    auto img_width_u = (uint32_t)img_width;
    auto img_height_u = (uint32_t)img_height;
    auto row_stride = img_width_u * 3; // stb yields RGB with req_comp=3
    image_info iminf{
        img_width_u,
        img_height_u,
        row_stride,
        3};
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

#endif // defined(use_jpeg)