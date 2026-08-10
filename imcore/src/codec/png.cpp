/*
 * Implementation of png file processing with libpng
 *
 * reference
 * http://www.libpng.org/pub/png/libpng-manual.html
 */

#ifdef USE_PNG

#include "image.hpp"
#include "logging.hpp"
extern "C"
{
#include "png.h"
}

#define PNG_SIG_BYTES 8

using namespace zb;
using namespace zb::ui;
using namespace zb::ui::core;

int Image::read_png_file(
    const std::string file_name,
    on_loading_image image_loaded,
    on_reading_image_row read_row)
{
    FILE *infile = nullptr;

    auto png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        LE << "Initialization of png read struct failed.";
        return 1;
    }

    png_infop png_info = nullptr;

    // longjmp must not skip C++ destructors, so cleanup is explicit here;
    // every exit path (including the png error longjmp) goes through this
    // single release point. png_destroy_read_struct tolerates a null info.
    auto cleanup = [&]()
    {
        if (png_ptr)
        {
            png_destroy_read_struct(&png_ptr, &png_info, nullptr);
        }
        if (infile)
        {
            fclose(infile);
            infile = nullptr;
        }
    };

    png_info = png_create_info_struct(png_ptr);
    if (!png_info)
    {
        cleanup();
        LE << "Initialization of png info struct failed.";
        return 2;
    }

    infile = fopen(file_name.c_str(), "rb");
    if (!infile)
    {
        cleanup();
        LE << "Openning png file failed.";
        return 3;
    }

    unsigned char magicbuf[PNG_SIG_BYTES];
    if (PNG_SIG_BYTES != fread(magicbuf, 1, PNG_SIG_BYTES, infile) ||
        0 != png_sig_cmp(magicbuf, 0, PNG_SIG_BYTES))
    {
        cleanup();
        LE << "The specified file is not a PNG file. (" << file_name << ")";
        return 4;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        cleanup();
        return -1;
    }

    png_init_io(png_ptr, infile);
    png_set_sig_bytes(png_ptr, PNG_SIG_BYTES);
    png_read_info(png_ptr, png_info);

    auto img_width = png_get_image_width(png_ptr, png_info);
    auto img_height = png_get_image_height(png_ptr, png_info);
    if (img_width == 0 || img_height == 0)
    {
        cleanup();
        LE << "PNG file reports zero width/height, refusing to process. (" << file_name << ")";
        return 5;
    }
    auto img_color_type = png_get_color_type(png_ptr, png_info);
    auto bit_depth = png_get_bit_depth(png_ptr, png_info);

    // input transformation, to RGBA

    if (bit_depth == 16)
    {
#if PNG_LIBPNG_VER >= 10504
        png_set_scale_16(png_ptr);
#else
        png_set_strip_16(png_ptr);
#endif
    }

    if (img_color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }

    if (img_color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_palette_to_rgb(png_ptr);
    }

    if (png_get_valid(png_ptr, png_info, PNG_INFO_tRNS))
    {
        png_set_tRNS_to_alpha(png_ptr);
    }

    if (img_color_type == PNG_COLOR_TYPE_GRAY ||
        img_color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
        png_set_gray_to_rgb(png_ptr);
    }

    if (img_color_type == PNG_COLOR_TYPE_RGB ||
        img_color_type == PNG_COLOR_TYPE_GRAY ||
        img_color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png_ptr, png_info);

    auto row_stride = (uint32_t)png_get_rowbytes(png_ptr, png_info); // bit number in one row
    auto components = row_stride / img_width;                        // 4, RGBA format
    image_info iminf
    {
        (uint32_t)img_width,
        (uint32_t)img_height,
        row_stride,
        (uint32_t)components
    };
    image_loaded(iminf);

    std::vector<png_byte> row_data(row_stride, 0x00);
    auto row_pointer = row_data.data();
    for (int i = 0; i < img_height; i++)
    {
        png_read_row(png_ptr, row_pointer, NULL);
        read_row(row_pointer);
    }

    png_read_end(png_ptr, NULL);
    cleanup();

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
        for (int i = 0; i < png_inf.image_width; i++)
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
    png_structp png_ptr = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        LE << "Failed creating png write struct.";
        return 1;
    }

    png_infop info_ptr = nullptr;
    FILE *fp = nullptr;

    // same longjmp-safe pattern as read_png_file: one cleanup point for all
    // exit paths (png_destroy_write_struct tolerates a null info_ptr)
    auto cleanup = [&]()
    {
        if (png_ptr)
        {
            png_destroy_write_struct(&png_ptr, &info_ptr);
        }
        if (fp)
        {
            fclose(fp);
            fp = nullptr;
        }
    };

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        cleanup();
        LE << "Failed creating png info struct.";
        return 2;
    }

    fp = fopen(file_name.c_str(), "wb");
    if (!fp)
    {
        cleanup();
        LE << "Failed openning png file " << file_name << ").";
        return 3;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        cleanup();
        return -1;
    }

    png_init_io(png_ptr, fp);

    // to RGBA
    png_set_IHDR(
        png_ptr,
        info_ptr,
        img_inf.image_width,
        img_inf.image_height,
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr);

    std::vector<png_byte> row(img_inf.row_stride);
    png_bytep row_pointer;
    for (int i = 0; i < img_inf.image_height; i++)
    {
        row.clear();
        if (!write_row(row))
        {
            LE << "Error occurred while write data to png file.";
            cleanup();
            return -2;
        }
        // guarantee the buffer is at least row_stride long before png_write_row reads it
        if (row.size() < img_inf.row_stride)
            row.resize(img_inf.row_stride, 0x00);
        row_pointer = row.data();
        png_write_row(png_ptr, row_pointer);
    }

    png_write_end(png_ptr, NULL);
    cleanup();

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
            row.push_back(cd.rgb.a);
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