#pragma once

#include "widget.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zb::ui
{
    /*
     * Displays a decoded image (FI-09, code-contract §3.5).
     *
     * The widget owns its pixels: a copy is taken at set time (a
     * load-time operation, never on the frame path) and the widget never
     * borrows host pixel memory. set_image_png / set_image_jpeg (under
     * USE_PNG / USE_JPEG) decode into a per-widget single-entry cache
     * keyed by the content id: a repeated id is a no-op whatever the
     * bytes (same id means same image), a new id replaces the cached
     * decode; a decode failure keeps the previous image.
     *
     * Geometry: an explicit size wins (the shared length rules);
     * otherwise measure() is the image's natural size (0x0 while empty).
     * The draw letterboxes scale-to-fit, centered in the content box;
     * the uncovered margins stay the widget background.
     *
     * `src` is the resource name stored and exposed for the host — the
     * framework never resolves it. Pixels arrive through set_image /
     * set_image_png (the html_embed packed table at build time, or the
     * host's runtime feed via find_by_id), the same "static structure in
     * the document, resources and behavior in C++" line as event wiring.
     */
    class ImageView : public Widget
    {
    public:
        ImageView() = default;

        void set_src(const std::string &resource)
        {
            src = resource;
        }
        [[nodiscard]] const std::string &get_src() const { return src; }

        /* copies the view; a malformed view (null pixels, stride <
           width, non-positive extents) logs one LW and changes nothing */
        void set_image(const core::image_t &view);

#if defined(USE_PNG)
        /* decodes once into the content-id cache: a repeated id is a
           no-op (true), a decode failure logs one LW, keeps the previous
           image and returns false. `bytes` is only read. */
        bool set_image_png(const std::string &content_id, const uint8_t *bytes, size_t n);
#endif
#if defined(USE_JPEG)
        /* set_image_png's mirror for baseline jpegs (RGB decode) */
        bool set_image_jpeg(const std::string &content_id, const uint8_t *bytes, size_t n);
#endif

        void clear_image();
        [[nodiscard]] bool has_image() const { return !pixels.empty(); }
        [[nodiscard]] const std::string &get_content_id() const { return content_id; }

        /* natural size: the image's extent, 0x0 while empty (explicit
           sizes win through the shared layout rules, which skip
           measure() for explicit axes) */
        [[nodiscard]] core::imsize_t measure() const override
        {
            return {img_width, img_height};
        }

    protected:
        void draw_at(core::Graphics &area) const override;

    private:
        /* store tail shared by the decode entry points (set_image keeps
           its own path: a view-set image reports an empty content id) */
        void store_decoded(std::vector<core::Color> &&decoded, int w, int h, const std::string &id);

        std::string src;
        std::string content_id;
        std::vector<core::Color> pixels;  // w*h, tightly packed
        int img_width = 0;
        int img_height = 0;
    };
}
