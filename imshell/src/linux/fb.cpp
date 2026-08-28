#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "core/color.hpp"
#include "linux/fb.hpp"
#include "logging.hpp"

FB::FB()
{
    // TODO: throw error
    if (init() != 0)
    {
        // members keep their zero defaults: draw() is a no-op and ok()
        // reports the failure (a null buf also survives the present path)
        LE << "fb: init failed, presenting disabled";
    }
}

FB::~FB()
{
    dispose();
}

// TODO: for prototype sample. need pixel size, color format etc.
void FB::draw(char *b, int w, int h, int rx, int ry, int rw, int rh)
{
    if (nullptr == b || nullptr == buf || w <= 0 || h <= 0)
    {
        return;
    }

    // clip the region to the screen so a buffer larger than the display
    // cannot overrun buf
    if (rx < 0)
    {
        rw += rx;
        rx = 0;
    }
    if (ry < 0)
    {
        rh += ry;
        ry = 0;
    }
    if (rw > screen_width - rx)
    {
        rw = screen_width - rx;
    }
    if (rh > screen_height - ry)
    {
        rh = screen_height - ry;
    }
    // the same bound against the SOURCE surface: when the app surface is
    // smaller than the panel, a region pinned to the panel edge would
    // memcpy past the end of b (A-16)
    if (rw > w - rx)
    {
        rw = w - rx;
    }
    if (rh > h - ry)
    {
        rh = h - ry;
    }
    if (rw <= 0 || rh <= 0)
    {
        return;
    }
    if (mode == present_mode::unsupported)
    {
        return;  // named once in init(); never corrupt the panel
    }

    if (mode == present_mode::convert)
    {
        // A-1 seam: internal row -> panel bytes, one converter call per
        // row; bytes_per_pixel is the panel's own width here
        const size_t row_need =
            static_cast<size_t>(rw) * zb::ui::core::panel_pixel_bytes(panel);
        for (int row = ry; row < ry + rh; row++)
        {
            const auto *src =
                reinterpret_cast<const zb::ui::core::Color *>(b) + row * w + rx;
            auto *dst = reinterpret_cast<uint8_t *>(
                buf + row * screen_line_len + rx * bytes_per_pixel);
            zb::ui::core::convert_row(panel, src, rw, dst, row_need);
        }
    }
    else
    {
        // one memcpy per row instead of per pixel, restricted to the region
        const int copy_bytes = rw * bytes_per_pixel;
        for (int row = ry; row < ry + rh; row++)
        {
            std::memcpy(buf + row * screen_line_len + rx * bytes_per_pixel,
                        b + row * w * bytes_per_pixel + rx * bytes_per_pixel, copy_bytes);
        }
    }

    // sync whole rows of the region: the old start offset (rx included)
    // plus a row-count length overran the mapping when the region
    // touched the bottom of the screen; MS_SYNC instead of the
    // implementation-defined flags value 0
    msync(buf + static_cast<size_t>(ry) * screen_line_len,
          static_cast<size_t>(rh) * screen_line_len, MS_SYNC);
}

int FB::init()
{
    // TODO: can be set to any fbx
    ffb = open("/dev/fb0", O_RDWR);
    if (ffb < 0)
    {
        LE << "can not open framebuffer device";
        return 1;
    }

    struct fb_fix_screeninfo finfo;
    if (ioctl(ffb, FBIOGET_FSCREENINFO, &finfo) < 0)
    {
        LE << "can not read fix screen info";
        return 2;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(ffb, FBIOGET_VSCREENINFO, &vinfo) < 0)
    {
        LE << "can not read var screen info";
        return 3;
    }

    //memcpy(&bk_vinfo, &vinfo, sizeof(struct fb_var_screeninfo));
    bits_per_pixel = vinfo.bits_per_pixel;
    bytes_per_pixel = bits_per_pixel / 8;
    LD << "bits_per_pixel : " << bits_per_pixel;
    LD << "bytes_per_pixel : " << bytes_per_pixel;
    // presentation seam (A-1): byte-equal panel -> the memcpy fast path;
    // a known convertible layout -> per-row conversion; anything else
    // refuses to present instead of corrupting the panel (the old
    // warning-only path kept memcpy'ing mismatched bytes)
    if (bytes_per_pixel == static_cast<int>(sizeof(zb::ui::core::Color)))
    {
        mode = present_mode::fast_copy;
    }
    else if (bits_per_pixel == 16 &&
             vinfo.red.offset == 11 && vinfo.red.length == 5 &&
             vinfo.green.offset == 5 && vinfo.green.length == 6 &&
             vinfo.blue.offset == 0 && vinfo.blue.length == 5)
    {
        panel = zb::ui::core::panel_format::bgr565;
        mode = present_mode::convert;
        LD << "fb: panel is RGB565; presenting through the row converter";
    }
    else
    {
        mode = present_mode::unsupported;
        LW << "fb: panel is " << bits_per_pixel << "bpp but the build renders "
           << sizeof(zb::ui::core::Color) * 8 << "bpp and no converter matches; "
           << "presenting disabled";
    }

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

    LD << "screen_width : " << screen_width;
    LD << "screen_height : " << screen_height;
    LD << "screen_line_len : " << screen_line_len;

    buf = (char *)mmap(0,
                       screen_mem_len,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       ffb,
                       0);

    if (MAP_FAILED == buf)
    {
        // a non-null check in draw() must not see MAP_FAILED (-1)
        buf = nullptr;
        LE << "mmap of the framebuffer failed";
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