#ifndef LIFE_HPP
#define LIFE_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::life
{
    /*
     * The demoscene canvas: one widget that renders whatever the current
     * mode computes -- a Life colony, a plasma field or a starfield. The
     * app advances the state in paint() and marks the widget dirty, so
     * the "dirty frame" loop of every shell keeps the animation running
     * (the deterministic no-timers contract holds: frame n always renders
     * the same pixels on every platform).
     */
    class LifeCanvas : public zb::ui::Widget
    {
    public:
        // pixel model: the colony lives on a 64x48 cell grid rendered
        // at kScale px per cell
        static constexpr int kCols = 64;
        static constexpr int kRows = 48;
        static constexpr int kScale = 4;

        LifeCanvas();

        // --- life ---
        void toggle_cell(int col, int row);
        void step_life();
        void clear_life();
        void stamp_glider_gun();

        // --- plasma / stars (pure functions of the frame counter) ---
        void set_frame(unsigned frame) noexcept { frame_ = frame; }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;

    private:
        enum mode : int
        {
            life = 0,
            plasma,
            stars
        };
        friend class Life;   // the app switches modes and drives the state

        void set_mode(int m);

        bool cells_[kRows][kCols] = {};
        bool back_[kRows][kCols] = {};
        unsigned frame_ = 0;
        int mode_ = life;

        struct star
        {
            int x;
            int y;
            int z;
        };
        star stars_[64] = {};
    };

    /*
     * Game of Life / plasma / starfield demo app. The colony advances
     * every kFramesPerStep paints; taps toggle cells; the buttons are the
     * classic life controls plus the mode switch.
     */
    class Life : public IApp
    {
    public:
        void create_window() override { create_window(kWidth, kHeight); }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height) override
        {
            create_window(max_client_width, max_client_height, nullptr);
        }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height, void *buffer) override;

        zb::SharedPtr<IWindow> window() noexcept override { return window_; }

        void input(const zb::input::input_event &ev) noexcept override;
        void paint() noexcept override;

        bool is_dirty() const noexcept override
        {
            return window_ && window_->is_dirty();
        }

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
        static constexpr uint32_t kWidth = 320;
        static constexpr uint32_t kHeight = 240;
        static constexpr unsigned kFramesPerStep = 8;
        static constexpr int kGenW = 44;   // generation label width
        static constexpr int kGenH = 12;

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        LifeCanvas *canvas_ = nullptr;
        zb::ui::Label *gen_ = nullptr;
        zb::ui::Button *play_btn_ = nullptr;
        bool playing_ = true;
        unsigned frames_ = 0;
        unsigned generation_ = 0;
        int mode_ = 0;

        event::Subscription<> subs_[5];

        void on_play();
        void on_step();
        void on_clear();
        void on_gun();
        void on_mode();
    };
}  // namespace zb::app::life

#endif // !LIFE_HPP
