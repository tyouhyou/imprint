#ifndef ROTAFARM_HPP
#define ROTAFARM_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::rotafarm
{
    /*
     * ROTAFARM: a clockface farm. The sun walks around the field ring;
     * only sectors inside its day arc grow crops (tap to plant, tap a
     * ripe crop to harvest). A locust shade patrols the opposite way
     * and eats any crop past half growth -- harvest before it arrives.
     * Each full sun lap is a day; the score is harvests.
     *
     * DETERMINISM CONTRACT: sun/locust phases, fireflies and pulses are
     * pure functions of the frame counter; taps are the only input.
     */
    class FarmCanvas : public zb::ui::Widget
    {
    public:
        static constexpr int kVW = 400;
        static constexpr int kVH = 250;
        static constexpr int kSectors = 12;

        void set_geometry(int ox, int oy, int scale, int font_px);
        void tick();
        void reset();
        [[nodiscard]] int harvests() const { return harvests_; }
        [[nodiscard]] int day() const
        {
            return static_cast<int>(frame_ / kDayLen) + 1;
        }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;

    private:
        static constexpr int kDayLen = 7200;   // frames per sun lap
        static constexpr int kGrowLight = 80;  // lit frames per stage
        static constexpr int kRIn = 66;
        static constexpr int kROut = 94;
        static constexpr int kRPlant = 80;

        int ox_ = 0, oy_ = 0, s_ = 1;
        unsigned frame_ = 0;
        int harvests_ = 0;
        int stage_[kSectors] = {};     // -1 empty, 0..5 growth
        int light_[kSectors] = {};     // lit frames accumulated
        int eaten_at_[kSectors] = {};  // last locust bite (cooldown)

        [[nodiscard]] unsigned hash(int a, int b) const;
        [[nodiscard]] int ang_dist(int a, int b) const;
        void sector_poly(int i, int r0, int r1, int *pts) const;
        void fill_sector(zb::ui::core::Graphics &area, int i,
                         const zb::ui::core::Color &c) const;
    };

    /*
     * App shell: title, DAY n / HARVEST n status, RESET.
     */
    class Rotafarm : public IApp
    {
    public:
        void create_window() override { create_window(kWidth, kHeight); }
        void create_window(uint32_t max_client_width, uint32_t max_client_height) override
        {
            create_window(max_client_width, max_client_height, nullptr);
        }
        void create_window(uint32_t max_client_width, uint32_t max_client_height,
                           void *buffer) override;

        zb::SharedPtr<IWindow> window() noexcept override { return window_; }

        void input(const zb::input::input_event &ev) noexcept override
        {
            if (window_)
            {
                window_->input(ev);
            }
        }

        void paint() noexcept override
        {
            if (window_)
            {
                farm_->tick();
                sync_status();
                window_->invalidate();
                window_->paint();
            }
        }

        bool is_dirty() const noexcept override { return window_ && window_->is_dirty(); }
        bool dirty_region(int &x, int &y, int &w, int &h) const noexcept override
        {
            return window_ && window_->dirty_region(x, y, w, h);
        }

        event::Subscription<const void *> on_painting(event::PAINT_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->painting.subscribe(h) : event::Subscription<const void *>();
        }
        event::Subscription<const void *> on_painted(event::PAINT_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->painted.subscribe(h) : event::Subscription<const void *>();
        }
        event::Subscription<> on_closing(event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->closing.subscribe(h) : event::Subscription<>();
        }
        event::Subscription<> on_closed(event::CLOSE_EVENT::EventHandler h) noexcept override
        {
            return window_ ? window_->closed.subscribe(h) : event::Subscription<>();
        }

    private:
        static constexpr uint32_t kWidth = 800;
        static constexpr uint32_t kHeight = 520;

        void install_font();
        void sync_status();

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        FarmCanvas *farm_ = nullptr;
        zb::ui::Label *status_ = nullptr;
        int last_status_ = -1;

        event::Subscription<> sub_reset_;
    };
}  // namespace zb::app::rotafarm

#endif  // !ROTAFARM_HPP
