#ifndef COMET_HPP
#define COMET_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::comet
{
    /*
     * COMET COURIER: slingshot a courier comet around two gravity
     * wells, thread the three gate rings in any order, then swing back
     * into the depot. A crash, a boundary exit or a stranded orbit
     * costs an attempt (gates stay lit). While aiming, the field runs
     * the same integrator ahead 90 steps and paints the predicted path.
     *
     * DETERMINISM CONTRACT: the whole flight is a pure function of the
     * release vector and the frame counter (hash-seeded starfield and
     * gate pulses only) -- same shot, same bytes.
     */
    class SkyCanvas : public zb::ui::Widget
    {
    public:
        static constexpr int kVW = 400;
        static constexpr int kVH = 250;
        static constexpr int kGates = 3;
        static constexpr int kTrail = 64;

        void set_geometry(int ox, int oy, int scale, int font_px);
        void tick();
        void reset();
        [[nodiscard]] int attempts() const { return attempts_; }
        [[nodiscard]] bool cleared() const { return cleared_; }
        [[nodiscard]] int gates_passed() const;

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        [[nodiscard]] bool captures_pointer() const override { return true; }

    private:
        // arena (virtual space): two wells, three gates, the depot
        static constexpr int kWellX[2] = {150, 290};
        static constexpr int kWellY[2] = {110, 175};
        static constexpr int kWellM[2] = {900, 650};
        static constexpr int kGateX[kGates] = {110, 320, 230};
        static constexpr int kGateY[kGates] = {55, 80, 205};
        static constexpr int kDepotX = 30;
        static constexpr int kDepotY = 125;

        int ox_ = 0, oy_ = 0, s_ = 1;
        unsigned frame_ = 0;
        int attempts_ = 1;
        bool cleared_ = false;
        bool flying_ = false;
        int flight_ = 0;
        float px_ = static_cast<float>(kDepotX);
        float py_ = static_cast<float>(kDepotY);
        float vx_ = 0.f, vy_ = 0.f;
        bool gate_[kGates] = {};
        int trail_x_[kTrail] = {}, trail_y_[kTrail] = {};
        int trail_n_ = 0;

        // slingshot: press point and current pointer (virtual)
        bool aiming_ = false;
        int aim_x0_ = 0, aim_y0_ = 0, aim_x1_ = 0, aim_y1_ = 0;

        void relaunch();
        void gravity(float x, float y, float &ax, float &ay) const;
        [[nodiscard]] unsigned hash(int a, int b) const;
    };

    /*
     * App shell: title, ATTEMPT n / GATES n/3 status, RESET.
     */
    class Comet : public IApp
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
                sky_->tick();
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
        SkyCanvas *sky_ = nullptr;
        zb::ui::Label *status_ = nullptr;
        int last_status_ = -1;

        event::Subscription<> sub_reset_;
    };
}  // namespace zb::app::comet

#endif  // !COMET_HPP
