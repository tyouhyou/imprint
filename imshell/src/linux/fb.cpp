#include <cstring>
#include <unistd.h>  
#include <stdio.h>  
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>  
#include "linux/fb.hpp"

FB::FB()
    : ffb(-1), buf(nullptr)
{
    // TODO: throw error
    init();
}

FB::~FB()
{
    dispose();
}

// TODO: for prototype sample. need pixel size, color format etc.
void FB::draw(char *b, int w, int h)
{
    if (nullptr == b || w <= 0 || h <= 0)
    {
        return;
    }

    // one memcpy per row instead of per pixel; clip to the screen so a
    // buffer larger than the display cannot overrun buf
    const int rows_to_copy = (h < screen_height) ? h : screen_height;
    const int cols_to_copy = (w < screen_width) ? w : screen_width;
    const int copy_bytes = cols_to_copy * bytes_per_pixel;

    for (int row = 0; row < rows_to_copy; row++)
    {
        std::memcpy(buf + row * screen_line_len, b + row * w * bytes_per_pixel, copy_bytes);
    }

    msync(buf, screen_line_len * rows_to_copy, 0);
}

int FB::init()
{
    // TODO: can be set to any fbx
    ffb = open("/dev/fb0", O_RDWR);
    if (ffb < 0)
    {
        printf("Error : Can not open framebuffer device\r\n");  
        return 1;
    }

    struct fb_fix_screeninfo finfo;
    if (ioctl(ffb, FBIOGET_FSCREENINFO, &finfo) < 0)
    {
        printf("Error : Can not read fix screen info\r\n");  
        return 2;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(ffb, FBIOGET_VSCREENINFO, &vinfo) < 0)
    {
        printf("Error : Can not read var screen info\r\n");  
        return 3;
    }

    //memcpy(&bk_vinfo, &vinfo, sizeof(struct fb_var_screeninfo));
    bits_per_pixel = vinfo.bits_per_pixel;// TODO: set or use it to determine color
    bytes_per_pixel = bits_per_pixel / 8;
    printf("bits_per_pixel : %d\r\n", bits_per_pixel);
    printf("bytes_per_pixel : %d\r\n", bytes_per_pixel);

    if (ioctl(ffb, FBIOPUT_VSCREENINFO, &vinfo) < 0)
    {
        return 4;
    }

    if (ioctl(ffb, FBIOGET_FSCREENINFO, &finfo))
    {
        return 5;
    }

    screen_width = vinfo.xres;           // in pixels
    screen_height = vinfo.yres;          // in pixels
    screen_line_len = finfo.line_length; // in bytes
    screen_mem_len = screen_line_len * screen_height;   // in bytes

    printf("screen_width : %d\r\n", (int)screen_width);
    printf("screen_height : %d\r\n", (int)screen_height);
    printf("screen_line_len : %d\r\n", (int)screen_line_len);

    buf = (char *)mmap(0,
                       screen_mem_len,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       ffb,
                       0);

    if (MAP_FAILED == buf)
    {
        return 6;
    }

    return 0;
}

int FB::dispose()
{
    if (buf != nullptr)
    {
        if (0 != munmap(buf, screen_mem_len))
        {
            return 1;
        }
        // if (ioctl(ffb, FBIOPUT_VSCREENINFO, &bk_vinfo))
        // {
        //     return 2;
        // }
    }
    if (ffb > 0)
    {
        close(ffb);
    }
    return 0;
}