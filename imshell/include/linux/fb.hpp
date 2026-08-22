#ifndef IMSHELL_LINUX_FB_HPP
#define IMSHELL_LINUX_FB_HPP

#include <linux/fb.h>
#include <cstddef>

class FB
{
public:
    FB();
    ~FB();

    // false when /dev/fb0 or its mmap could not be set up (headless
    // host, missing device): draw() is a no-op then
    bool ok() const { return buf != nullptr; }

    void draw(char *b, int w, int h, int rx, int ry, int rw, int rh);

private:
    int init();
    int dispose();

    // zero-initialized: every init() failure path leaves them sane
    int screen_width = 0;   // screen width
    int screen_height = 0;  // screen height
    int screen_line_len = 0;  // screen width in bytes
    int bits_per_pixel = 0;
    int bytes_per_pixel = 0;
    size_t screen_mem_len = 0;
    struct fb_var_screeninfo bk_vinfo {};
    int ffb = -1;
    char *buf = nullptr;
};

#endif // IMSHELL_LINUX_FB_HPP