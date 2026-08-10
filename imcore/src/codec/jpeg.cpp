/*
 * Implementation of jpeg file processing with libjpeg.
 *
 * reference
 * https://dev.w3.org/Amaya/libjpeg/example.c
 */

#ifdef USE_JPEG

#include "image.hpp"
#include "logging.hpp"
#include <memory>
extern "C"
{
#include "jpeglib.h" // TODO: change to libjpeg-turbo?
#include <setjmp.h>
}

using namespace zb;
using namespace zb::ui;
using namespace zb::ui::core;

namespace
{
    /*
     * RAII for a libjpeg compression struct. Safe here: the write path has
     * no setjmp/longjmp (the default error handler exits the process), so
     * the destructor is never skipped.
     */
    struct JpegCompressGuard
    {
        jpeg_compress_struct cinfo{};
        jpeg_error_mgr jerr{};

        JpegCompressGuard()
        {
            cinfo.err = jpeg_std_error(&jerr);
            jpeg_create_compress(&cinfo);
        }
        ~JpegCompressGuard()
        {
            jpeg_destroy_compress(&cinfo);
        }
    };
}

int Image::write_jpeg(
    Graphics &g,
    const std::string &file_name,
    const int &quality)
{
    auto cur_row = 0;
    auto gsize = g.size();
    auto row_bit_stride = gsize.width * 3; // at this time being, 24bit jpeg only
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

    auto rst = write_jpeg_file(file_name, img_inf, write_row);
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
    JpegCompressGuard guard;
    auto &cinfo = guard.cinfo;

    std::unique_ptr<FILE, int (*)(FILE *)> outfile(fopen(file_name.c_str(), "wb"), &fclose);
    if (!outfile)
    {
        LE << "can't open " << file_name;
        return 1;
    }
    jpeg_stdio_dest(&cinfo, outfile.get());

    cinfo.image_width = img_inf.image_width;
    cinfo.image_height = img_inf.image_height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    auto row_stride = img_inf.image_width * img_inf.color_components;

    std::vector<JSAMPLE> image_buffer(row_stride);
    while (cinfo.next_scanline < cinfo.image_height)
    {
        image_buffer.clear();
        if (!write_row(image_buffer))
        {
            LE << "Error occurred while write data to jpeg file.";
            return 2;
        }
        // guarantee the buffer is at least row_stride long before (de)compression reads it
        if (image_buffer.size() < (size_t)row_stride)
            image_buffer.resize((size_t)row_stride);
        JSAMPROW row_pointer[1] = {image_buffer.data()};
        (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    image_buffer.clear();

    jpeg_finish_compress(&cinfo);

    return 0;
}

extern "C"
{
    typedef struct _my_error_mgr
    {
        struct jpeg_error_mgr pub;
        jmp_buf setjmp_buffer;
    } my_error_mgr, *my_error_ptr;

    METHODDEF(void)
    my_error_exit(j_common_ptr cinfo)
    {
        my_error_ptr myerr = (my_error_ptr)cinfo->err;
        (*cinfo->err->output_message)(cinfo);
        longjmp(myerr->setjmp_buffer, 1);
    }
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
        if (3 != jpg_inf.color_components)
        {
            LE << "Only 24bit RGB image is supported at this time being. (Color components: " << jpg_inf.color_components << ")";
            return false;
        }

        uint8_t r = 0, g = 0, b = 0;
        for (int i = 0; i < jpg_inf.image_width; i++)
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
    struct jpeg_decompress_struct cinfo;
    my_error_mgr jerr;

    FILE *infile;
    JSAMPARRAY buffer;
    uint32_t row_stride;

    if ((infile = fopen(file_name.c_str(), "rb")) == NULL)
    {
        LE << "can't open " << file_name;
        return 1;
    }

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jpeg_create_decompress(&cinfo);
    bool cinfo_alive = true;

    // longjmp must not skip C++ destructors, so cleanup is explicit here;
    // every exit path goes through this single release point
    auto cleanup = [&]()
    {
        if (cinfo_alive)
        {
            cinfo_alive = false;
            jpeg_destroy_decompress(&cinfo);
        }
        fclose(infile);
        infile = nullptr;
    };

    if (setjmp(jerr.setjmp_buffer))
    {
        cleanup();
        LE << "unexpected error.";
        return -1;
    }

    jpeg_stdio_src(&cinfo, infile);
    (void)jpeg_read_header(&cinfo, TRUE);

    (void)jpeg_start_decompress(&cinfo);
    row_stride = cinfo.output_width * cinfo.output_components;
    LD << "image width=" << cinfo.output_width << "; height=" << cinfo.output_height << "; Color components=" << cinfo.output_components;

    image_info iminf{
        cinfo.output_width,
        cinfo.output_height,
        row_stride,
        (uint32_t)cinfo.output_components};
    if (!image_loaded(iminf))
    {
        LE << "Error occurred when set image info in image_loaded callback.";
        cleanup();
        return 2;
    }

    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    while (cinfo.output_scanline < cinfo.output_height)
    {
        (void)jpeg_read_scanlines(&cinfo, buffer, 1);
        if (!read_row(buffer[0]))
        {
            LE << "Error occurred when processing image row data.";
            cleanup();
            return 3;
        }
    }

    (void)jpeg_finish_decompress(&cinfo);
    cleanup();
    return 0;
}

#endif // defined(use_jpeg)