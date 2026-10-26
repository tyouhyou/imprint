#ifndef CRYSTAL_HPP
#define CRYSTAL_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::crystal
{
    /*
     * CRYSTAL EPOCH: a supersaturated tank, one seed nucleus, and a
     * budget of solute. Drag to lay catalyst lines -- the growth front
     * prefers them -- and grow dendrites that reach all three anchors
     * before the solution runs out. Impurity speks kill any branch
     * that touches them. Crystallizing a catalyst cell costs double.
     *
     * DETERMINISM CONTRACT: branching, flicker and pulses hash
     * (index, frame); growth order is array order. The whole crystal
     * is a pure function of the input stream.
     */
    class TankCanvas : public zb::ui::Widget
    {
    public:
        static constexpr int kVW = 400;  // 2 px per cell, grid 200x125
        static constexpr int kVH = 250;
        static constexpr int kCols = 200;
        static constexpr int kRows = 125;
        static constexpr int kAnchors = 3;
        static constexpr int kBudget = 2600;

        void set_geometry(int ox, int oy, int scale, int font_px);
        void tick();
        void reset();
        [[nodiscard]] int solution_pct() const
        {
            return budget_ * 100 / kBudget;
        }
        [[nodiscard]] int anchors_hit() const;
        [[nodiscard]] bool done() const { return anchors_hit() == kAnchors; }
        [[nodiscard]] bool failed() const { return budget_ <= 0 && !done(); }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        [[nodiscard]] bool captures_pointer() const override { return true; }

    private:
        enum cell : unsigned char
        {
            empty = 0,
            crystal,
            catalyst,
            impurity
        };

        struct tip
        {
            short x, y;
            signed char dx, dy;
        };
        static constexpr int kMaxTips = 64;
        static constexpr int kAnchorX[kAnchors] = {150, 60, 170};
        static constexpr int kAnchorY[kAnchors] = {30, 100, 90};

        int ox_ = 0, oy_ = 0, s_ = 1;
        unsigned frame_ = 0;
        int budget_ = kBudget;
        int cat_count_ = 0;
        unsigned char cells_[kRows][kCols] = {};
        unsigned short born_[kRows][kCols] = {};
        tip tips_[kMaxTips];
        int tips_n_ = 0;
        bool anchor_[kAnchors] = {};
        int last_cx_ = -1, last_cy_ = -1;

        [[nodiscard]] unsigned hash(int a, int b) const;
        void grow_cell(int cx, int cy, int cost);
        void paint_line(int c0, int r0, int c1, int r1);
    };

    /*
     * App shell: title, SOLUTION n% / ANCHORS n/3 status, RESET.
     */
    class Crystal : public IApp
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
                tank_->tick();
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
        [[nodiscard]] int solution_key() const;

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        TankCanvas *tank_ = nullptr;
        zb::ui::Label *status_ = nullptr;
        int last_status_ = -1;

        event::Subscription<> sub_reset_;
    };
}  // namespace zb::app::crystal

#endif  // !CRYSTAL_HPP
