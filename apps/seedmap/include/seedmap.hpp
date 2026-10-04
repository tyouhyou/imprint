#ifndef SEEDMAP_HPP
#define SEEDMAP_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::seedmap
{
    /*
     * The map canvas: a 64x48 tile dungeon rendered at a runtime scale
     * (classic tier: 4 px per tile).
     * Tiles: stone wall, floor, water, door, tree (scattered decor).
     */
    class MapCanvas : public zb::ui::Widget
    {
    public:
        static constexpr int kCols = 64;
        static constexpr int kRows = 48;
        static constexpr int kScale = 4;

        enum tile : unsigned char
        {
            wall = 0,
            floor,
            water,
            door,
            tree
        };

        MapCanvas(int scale);

        int canvas_w() const noexcept { return kCols * scale_; }
        int canvas_h() const noexcept { return kRows * scale_; }

        // hands in a fully generated map (the app owns the generator)
        void set_tile(int col, int row, unsigned char t);

        [[nodiscard]] unsigned char tile_at(int col, int row) const
        {
            return tiles_[row][col];
        }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;

    private:
        int scale_ = kScale;
        unsigned char tiles_[kRows][kCols] = {};
    };

    /*
     * Seed -> deterministic dungeon. The seed string is hashed with FNV-1a
     * and drives a fixed LCG; the same seed produces the same map byte for
     * byte on every platform (the demo's selling point: deterministic
     * rendering you can verify by eye). DRAW re-generates; the seed box is
     * a TextInput (ASCII hex recommended, any ASCII works).
     */
    class Seedmap : public IApp
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
        void paint() noexcept override { if (window_) window_->paint(); }

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
        static constexpr int kHeaderH = 40;   // seed row + spacing

        // canvas geometry, decided once in create_window
        int scale_ = kScale;
        int canvas_x_ = 32;
        int canvas_y_ = kHeaderH;

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        MapCanvas *canvas_ = nullptr;
        zb::ui::TextInput *seed_box_ = nullptr;
        zb::ui::Label *status_ = nullptr;

        event::Subscription<> sub_draw_;
        event::Subscription<std::string> sub_submit_;

        unsigned state_ = 0;

        [[nodiscard]] unsigned next_rand();
        void generate();
        void place_room(int x, int y, int w, int h);
        void carve_corridor(int x1, int y1, int x2, int y2);
    };
}  // namespace zb::app::seedmap

#endif // !SEEDMAP_HPP
