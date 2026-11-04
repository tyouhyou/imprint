#include "image_view.hpp"

#include "codec/image.hpp"
#include "logging.hpp"

#include <algorithm>
#include <utility>

namespace zb::ui
{
    void ImageView::set_image(const core::image_t &view)
    {
        const int stride = view.row_stride > 0 ? view.row_stride : view.width;
        if (view.pixels == nullptr || stride < view.width ||
            view.width <= 0 || view.height <= 0)
        {
            LW << "image_view: malformed image view rejected; keeping the previous image";
            return;
        }

        // own a tightly packed copy (row-by-row: the view's stride may be
        // wider than its width)
        std::vector<core::Color> copy;
        copy.reserve(static_cast<size_t>(view.width) * static_cast<size_t>(view.height));
        for (int row = 0; row < view.height; row++)
        {
            const core::Color *src_row = view.pixels + static_cast<int64_t>(row) * stride;
            copy.insert(copy.end(), src_row, src_row + view.width);
        }
        store_decoded(std::move(copy), view.width, view.height, std::string{});
    }

    void ImageView::store_decoded(std::vector<core::Color> &&decoded, int w, int h, const std::string &id)
    {
        pixels = std::move(decoded);
        img_width = w;
        img_height = h;
        content_id = id;
        mark_dirty();
        mark_layout_dirty();  // measure() reads the image extent
    }

    void ImageView::clear_image()
    {
        if (pixels.empty() && content_id.empty())
        {
            return;  // an empty view fed its empty state must not repaint
        }
        pixels.clear();
        pixels.shrink_to_fit();
        img_width = 0;
        img_height = 0;
        content_id.clear();
        mark_dirty();
        mark_layout_dirty();
    }

#if defined(USE_PNG)
    bool ImageView::set_image_png(const std::string &id, const uint8_t *bytes, size_t n)
    {
        // repeated id: no-op whatever the bytes (the deterministic-decode
        // identity — same id means same image). An empty id is an unnamed
        // image and never hits the cache: set_image leaves an empty id,
        // and the identity does not hold across the two paths.
        if (!id.empty() && id == content_id && has_image())
        {
            return true;
        }

        Image codec;
        std::vector<core::Color> decoded;
        uint32_t w = 0, h = 0;
        auto loaded = [&decoded, &w, &h](image_info &im)
        {
            w = im.image_width;
            h = im.image_height;
            decoded.reserve(static_cast<size_t>(im.image_width) * static_cast<size_t>(im.image_height));
            return true;
        };
        auto read_row = [&decoded, &w](unsigned char *buf)
        {
            for (uint32_t i = 0; i < w; i++)
            {
                decoded.push_back(core::Color::from(buf[0], buf[1], buf[2], buf[3]));
                buf += 4;
            }
            return true;
        };
        if (codec.read_png_memory(bytes, n, loaded, read_row) != 0 ||
            decoded.size() != static_cast<size_t>(w) * static_cast<size_t>(h))
        {
            LW << "image_view: png decode failed for content '" << id
               << "'; keeping the previous image";
            return false;
        }
        store_decoded(std::move(decoded), static_cast<int>(w), static_cast<int>(h), id);
        return true;
    }
#endif

#if defined(USE_JPEG)
    bool ImageView::set_image_jpeg(const std::string &id, const uint8_t *bytes, size_t n)
    {
        // an empty id is an unnamed image and never hits the cache (the
        // set_image_png mirror comment states why)
        if (!id.empty() && id == content_id && has_image())
        {
            return true;
        }

        Image codec;
        std::vector<core::Color> decoded;
        uint32_t w = 0, h = 0;
        auto loaded = [&decoded, &w, &h](image_info &im)
        {
            w = im.image_width;
            h = im.image_height;
            decoded.reserve(static_cast<size_t>(im.image_width) * static_cast<size_t>(im.image_height));
            return true;
        };
        auto read_row = [&decoded, &w](unsigned char *buf)
        {
            for (uint32_t i = 0; i < w; i++)
            {
                decoded.push_back(core::Color::from(buf[0], buf[1], buf[2]));
                buf += 3;
            }
            return true;
        };
        if (codec.read_jpeg_memory(bytes, n, loaded, read_row) != 0 ||
            decoded.size() != static_cast<size_t>(w) * static_cast<size_t>(h))
        {
            LW << "image_view: jpeg decode failed for content '" << id
               << "'; keeping the previous image";
            return false;
        }
        store_decoded(std::move(decoded), static_cast<int>(w), static_cast<int>(h), id);
        return true;
    }
#endif

    void ImageView::draw_at(core::Graphics &area) const
    {
        if (img_width <= 0 || img_height <= 0 || pixels.empty())
        {
            return;
        }
        const auto box = get_size();
        if (box.width <= 0 || box.height <= 0)
        {
            return;
        }

        // letterbox scale-to-fit (code-contract §3.5): dst_w = min(bw,
        // sw*bh/sh), dst_h = min(bh, sh*bw/sw), 64-bit intermediates,
        // centered in the content box; the uncovered margins stay the
        // widget background (painted by draw() before draw_at)
        const int64_t bw = box.width, bh = box.height;
        const int64_t sw = img_width, sh = img_height;
        const int64_t dst_w = std::min(bw, sw * bh / sh);
        const int64_t dst_h = std::min(bh, sh * bw / sw);
        const int x = static_cast<int>((bw - dst_w) / 2);
        const int y = static_cast<int>((bh - dst_h) / 2);

        area.draw_image_scaled(
            core::image_t{pixels.data(), img_width, img_height, 0},
            x, y, static_cast<int>(dst_w), static_cast<int>(dst_h));
    }
}
