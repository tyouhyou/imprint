#ifndef WINDLOOM_HPP
#define WINDLOOM_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::windloom
{
    /*
     * WINDLOOM: every stroke the finger draws becomes a persistent
     * current of wind (a flowing dashed line); the mother plant sheds
     * seeds and the player weaves them across the dusk sky into soil
     * pockets, where they bloom. Thorn rosettes kill a seed that
     * touches them; a seed that reaches open ground sprouts grass.
     *
     * DETERMINISM CONTRACT: seed release timing, drift wobble and dash
     * phase all hash (index, frame) -- the whole garden is a pure
     * function of the input stream.
     */
    class WindCanvas : public zb::ui::Widget
    {
    public:
        static constexpr int kVW = 400;
        static constexpr int kVH = 250;
        static constexpr int kSeeds = 24;
        static constexpr int kStrokes = 3;
        static constexpr int kPts = 24;

        void set_geometry(int ox, int oy, int scale, int font_px);

        void tick();
        void reset();
        [[nodiscard]] int delivered() const { return delivered_; }
        [[nodiscard]] int goal() const { return kGoal; }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        [[nodiscard]] bool captures_pointer() const override { return true; }

    private:
        struct pocket
        {
            int x, y, rx, ry, need, got;
        };
        static constexpr int kGoal = 11;

        int ox_ = 0, oy_ = 0, s_ = 1;

        // wind strokes: polylines in virtual space, dash phase = frame
        int sx_[kStrokes][kPts] = {};
        int sy_[kStrokes][kPts] = {};
        int sc_[kStrokes] = {};
        int stroke_next_ = 0;
        bool drawing_ = false;
        int draw_last_x_ = -1, draw_last_y_ = -1;

        // seeds: waiting (release_at) -> flying -> landed kind
        int release_at_[kSeeds] = {};
        float px_[kSeeds] = {}, py_[kSeeds] = {};
        float vx_[kSeeds] = {}, vy_[kSeeds] = {};
        int landed_[kSeeds] = {};  // 0 flying, 1 flower(pocket idx+10), 2 grass, 3 thorn
        int land_x_[kSeeds] = {}, land_y_[kSeeds] = {};

        pocket pockets_[3] = {{120, 216, 17, 9, 4, 0},
                              {252, 226, 19, 10, 4, 0},
                              {352, 200, 15, 8, 3, 0}};
        int delivered_ = 0;
        unsigned frame_ = 0;

        [[nodiscard]] unsigned hash(int a, int b) const;
        void wind_at(float x, float y, float &ax, float &ay) const;
    };

    /*
     * App shell: title, delivery status, RESET -- the garden itself is
     * the one canvas widget (in-app text English, page copy localized).
     */
    class Windloom : public IApp
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
                garden_->tick();
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
        WindCanvas *garden_ = nullptr;
        zb::ui::Label *status_ = nullptr;
        int last_status_ = -1;

        event::Subscription<> sub_reset_;
    };
}  // namespace zb::app::windloom

#endif  // !WINDLOOM_HPP
