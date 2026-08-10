/* class to process image.
 *
 * It reads/writes data from/to image files.
 * Only jpeg are supported.
 * (png, bmp are on their way)
 */
#pragma once

#include <stdio.h>
#include <stdint.h>
#include <functional>
#include <vector>
#include <string>
#include "../core/graphics.hpp"

namespace zb::ui
{
    typedef struct _image_info
    {
        uint32_t image_width;
        uint32_t image_height;
        uint32_t row_stride;       // bytes number of one row
        uint32_t color_components; // color channel number
    } image_info;

    /*
     * called when image file loaded in memory.
     */
    using on_loading_image = std::function<bool(image_info &)>;

    /*
     * function(jpeg_row_buffer)
     * called when reading one row of jpeg image.
     */
    using on_reading_image_row = std::function<bool(unsigned char *)>;

    /*
     * function(row_buffer)
     */
    using on_writing_image_row = std::function<bool(std::vector<unsigned char> &)>;

    class Image
    {
    public:
        Image() = default;

#if defined(USE_JPEG)

        int read_jpeg_file(
            const std::string file_name,
            on_loading_image file_loaded,
            on_reading_image_row read_row);

        int read_jpg(
            core::Graphics &g,
            const std::string &file_name,
            const int &start_x,
            const int &start_y);

        /*
         * write image data to jpeg
         * support jpeg having 24bit (rgb) color-face
         */
        int write_jpeg_file(
            const std::string file_name,
            const image_info &img_inf,
            on_writing_image_row write_row,
            const int &quality = 90);

        int write_jpeg(
            core::Graphics &g,
            const std::string &file_name,
            const int &quality = 90);

#endif

#if defined(USE_PNG)

        int read_png_file(
            const std::string file_name,
            on_loading_image image_loaded,
            on_reading_image_row read_row);

        int read_png(
            core::Graphics &g,
            const std::string &file_name,
            const int &start_x,
            const int &start_y);

        int write_png_file(
            const std::string file_name,
            const image_info &img_inf,
            on_writing_image_row write_row);

        int write_png(
            core::Graphics &g,
            const std::string &file_name);

#endif
    };
}