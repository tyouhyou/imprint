#pragma once

#include "imcore.hpp"
#include "theme.hpp"
#include "widget.hpp"

namespace zb::app::showcase
{
    /*
     * V-2 asset helpers: the procedural RGBA8 assets (tools/asset_gen)
     * materialized into the build's pixel layout and composited with the
     * Batch V-1 capabilities -- alpha blit, tint, 9-slice.
     */

    // RGBA8 -> the build's pixel layout, through the 8-bit-normalized
    // accessors (A-19): at 16bpp the channels collapse to 5 bits and the
    // alpha bit is binary, so the assets degrade but stay correct
    inline zb::ui::core::Color rgba_color(const unsigned char *p)
    {
        zb::ui::core::Color c{};
        c.set_r(p[0]);
        c.set_g(p[1]);
        c.set_b(p[2]);
        c.set_a(p[3]);
        return c;
    }

    // 9-slice blit: the four corners pass 1:1, the edges and the center
    // stretch by nearest-neighbor sampling. Every write goes through
    // draw_pixel, so the clip/damage/alpha conventions (A-12/A-13) hold;
    // blending follows the graphics' alpha_enabled switch. App-side on
    // purpose (tool-placement rule): one user today; a framework
    // primitive needs a second one first.
    inline void draw_nine_slice(zb::ui::core::Graphics &g,
                                const zb::ui::core::image_t &img,
                                const int x, const int y,
                                const int w, const int h, const int m)
    {
        if (img.pixels == nullptr || w <= 0 || h <= 0 ||
            img.width < 2 * m || img.height < 2 * m)
        {
            return;
        }
        const auto axis = [m](const int t, const int total, const int src) -> int
        {
            if (total <= 2 * m)
            {
                return t * src / total;
            }
            if (t < m)
            {
                return t;
            }
            if (t >= total - m)
            {
                return src - (total - t);
            }
            return m + (t - m) * (src - 2 * m) / (total - 2 * m);
        };
        for (int dy = 0; dy < h; ++dy)
        {
            const int sy = axis(dy, h, img.height);
            for (int dx = 0; dx < w; ++dx)
            {
                const int sx = axis(dx, w, img.width);
                g.draw_pixel(x + dx, y + dy,
                             img.pixels[static_cast<std::size_t>(sy) * img.row_stride + sx]);
            }
        }
    }

    /*
     * An alpha asset drawn as-is. A plain set_background_image() panel
     * cannot show soft alpha (background draws run with the graphics'
     * alpha switch off, so transparent corners would write black); a
     * widget that owns its draw_at() opts into alpha for the blit and
     * restores the switch -- the same pattern the modal mask uses.
     */
    class AlphaImage final : public zb::ui::Widget
    {
    public:
        explicit AlphaImage(const zb::ui::core::image_t &img)
            : img_(img)
        {
            set_size(img.width, img.height);
        }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override
        {
            const bool bak = area.is_alpha_enabled();
            area.enable_alpha(true);
            area.draw_image(img_, 0, 0);
            area.enable_alpha(bak);
        }

    private:
        zb::ui::core::image_t img_;
    };

    /*
     * A card floating on a pre-blurred shadow (the blur lives in
     * asset_gen, once at build time -- the framework deliberately has
     * none), with the ball asset tinted by the accent token: one asset,
     * any palette.
     */
    class ShadowCard final : public zb::ui::Widget
    {
    public:
        ShadowCard(const zb::ui::core::image_t &shadow, const zb::ui::core::image_t &ball)
            : shadow_(shadow), ball_(ball)
        {
            set_size(132, 44);
        }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override
        {
            const auto s = get_size();
            if (s.width < 40 || s.height < 24)
            {
                return;
            }
            const zb::ui::Theme &th = zb::ui::theme();

            // shadow behind the card, inset so the blur tail stays inside
            const bool bak = area.is_alpha_enabled();
            area.enable_alpha(true);
            draw_nine_slice(area, shadow_, 2, 2, s.width - 4, s.height - 4, 16);
            area.enable_alpha(bak);

            // the card covers the shadow's solid plateau
            area.fill_round_rect(6, 0, s.width - 7, s.height - 1, 6, th.field_bg);
            area.draw_round_rect(6, 0, s.width - 7, s.height - 1, 6, th.border);

            // accent-tinted ball on the card
            const int bx = (s.width - ball_.width) / 2;
            const int by = (s.height - ball_.height) / 2;
            area.enable_alpha(true);
            area.draw_image(ball_, bx, by, th.accent);
            area.enable_alpha(bak);
        }

    private:
        zb::ui::core::image_t shadow_;
        zb::ui::core::image_t ball_;
    };
}
