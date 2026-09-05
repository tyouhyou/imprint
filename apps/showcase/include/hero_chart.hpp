#pragma once

#include "imcore.hpp"
#include "theme.hpp"
#include "widget.hpp"

namespace zb::app::showcase
{
    /*
     * V-2 hero chart: the framework draws its own showcase asset. A
     * fixed 8-point series rendered with the Batch V-1 primitives --
     * rounded-rect card, per-column area fill, AA polyline, filled data
     * dots -- and revealed bottom-up by the app-side Tween (F-2 glue):
     * the progress value scales every point's height, one step per
     * paint request.
     *
     * Depth notes: all colors are opaque mixes of theme tokens (no
     * runtime alpha), so light/dark and 16bpp degrade cleanly; the AA
     * pair blends at 32bpp and quantizes to a binary stroke at 16bpp.
     */
    class HeroChart final : public zb::ui::Widget
    {
    public:
        HeroChart()
        {
            set_size(280, 48);
        }

        void set_progress(const int p)
        {
            progress_ = p < 0 ? 0 : (p > 255 ? 255 : p);
            mark_dirty();
        }

        [[nodiscard]] int progress() const { return progress_; }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override
        {
            const auto s = get_size();
            if (s.width < 24 || s.height < 20)
            {
                return;
            }
            const zb::ui::Theme &th = zb::ui::theme();

            // card
            area.fill_round_rect(0, 0, s.width - 1, s.height - 1, 6, th.field_bg);
            area.draw_round_rect(0, 0, s.width - 1, s.height - 1, 6, th.border);

            const int left = 10;
            const int top = 8;
            const int right = s.width - 11;
            const int bottom = s.height - 9;
            if (right <= left + 4 || bottom <= top + 4)
            {
                return;
            }

            // mid grid rule, dimmed from the tokens
            area.draw_line(left, (top + bottom) / 2, right, (top + bottom) / 2,
                           mix(th.border, th.field_bg, 1, 3));

            // revealed heights: progress scales every point up from the
            // baseline, so the curve grows in instead of wiping across
            const int span = bottom - top;
            int xs[kPoints];
            int ys[kPoints];
            for (int i = 0; i < kPoints; ++i)
            {
                xs[i] = left + (right - left) * i / (kPoints - 1);
                ys[i] = bottom - span * kSeries[i] * progress_ / (255 * 255);
            }

            // area under the curve: one interpolated strip per column
            const zb::ui::core::Color area_col = mix(th.accent, th.field_bg, 2, 5);
            for (int i = 0; i < kPoints - 1; ++i)
            {
                const int w = xs[i + 1] - xs[i];
                for (int x = xs[i]; x < xs[i + 1]; ++x)
                {
                    const int y = ys[i] + (ys[i + 1] - ys[i]) * (x - xs[i]) / w;
                    area.draw_line(x, y, x, bottom, area_col);
                }
            }

            // baseline over the area, then the AA curve and its dots
            area.draw_line(left, bottom, right, bottom, th.border);
            for (int i = 0; i < kPoints - 1; ++i)
            {
                area.draw_line_aa(xs[i], ys[i], xs[i + 1], ys[i + 1], th.accent);
            }
            for (int i = 0; i < kPoints; ++i)
            {
                area.fill_circle(xs[i], ys[i], 2, th.accent);
            }
        }

    private:
        static constexpr int kPoints = 8;
        static constexpr int kSeries[kPoints] = {36, 72, 52, 96, 78, 128, 110, 158};

        // channel-wise token mix through the normalized accessors (A-19);
        // opaque by construction, so the result is depth-stable
        static zb::ui::core::Color mix(const zb::ui::core::Color &a, const zb::ui::core::Color &b,
                               const int na, const int nb)
        {
            zb::ui::core::Color c{};
            c.set_r(static_cast<uint8_t>((a.r() * na + b.r() * nb) / (na + nb)));
            c.set_g(static_cast<uint8_t>((a.g() * na + b.g() * nb) / (na + nb)));
            c.set_b(static_cast<uint8_t>((a.b() * na + b.b() * nb) / (na + nb)));
            c.set_a(0xFF);
            return c;
        }

        int progress_ = 255;
    };
}
