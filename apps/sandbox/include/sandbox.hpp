#ifndef SANDBOX_HPP
#define SANDBOX_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::sandbox
{
    /*
     * The falling-sand playground: a fixed 180x110 cell grid rendered at a
     * runtime cell size (3..8 px, derived from the host buffer like the
     * life canvas scale). Materials: wall, sand, water, oil, fire, acid,
     * lava, steam, stone. The app advances one CA generation per paint
     * frame and keeps the widget invalidated (the life.cpp animation
     * shape), so the whole run is a pure function of the input stream.
     */
    class SandCanvas : public zb::ui::Widget
    {
    public:
        static constexpr int kCols = 180;
        static constexpr int kRows = 110;
        static constexpr int kMinCell = 3;
        static constexpr int kMaxCell = 8;
        static constexpr int kMaxBrush = 6;

        enum material : unsigned char
        {
            EMPTY = 0,
            WALL,
            SAND,
            WATER,
            OIL,
            FIRE,
            ACID,
            LAVA,
            STEAM,
            STONE
        };

        explicit SandCanvas(int cell);

        int canvas_w() const noexcept { return kCols * cell_; }
        int canvas_h() const noexcept { return kRows * cell_; }

        // frame counter owned by the app: every hash in the CA mixes it in,
        // so generation n renders identically on every platform
        void set_frame(const unsigned frame) noexcept { frame_ = frame; }

        void set_material(const unsigned char m) noexcept { material_ = m; }

        void set_brush(const int r) noexcept
        {
            brush_ = r < 1 ? 1 : (r > kMaxBrush ? kMaxBrush : r);
        }

        void clear_all();
        // fixed opening scene: wall shelves, a sand pile, a water basin
        void preload();
        void step();

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        // drag semantics: while pressed the stroke keeps painting even when
        // the pointer leaves the board (the Slider capture contract)
        [[nodiscard]] bool captures_pointer() const override { return true; }

    private:
        int cell_ = kMinCell;
        unsigned frame_ = 0;
        unsigned char material_ = SAND;
        int brush_ = 2;
        bool pressed_ = false;
        int last_col_ = -1;
        int last_row_ = -1;

        unsigned char cells_[kRows][kCols] = {};
        unsigned char ages_[kRows][kCols] = {};  // FIRE / STEAM lifetime

        // sand sinks through these (swap); steam counts as air to it
        static bool sinkable(const unsigned char m)
        {
            return m == EMPTY || m == WATER || m == OIL || m == STEAM;
        }

        // liquids fall through these
        static bool liquid_sinkable(const unsigned char m)
        {
            return m == EMPTY || m == STEAM;
        }

        // --- CA helpers; every "random" decision hashes (x, y, frame),
        // never a host RNG (see step() for the determinism contract) ---
        [[nodiscard]] unsigned cell_hash(int x, int y) const;
        // 4-adjacency in a fixed order; first match wins (deterministic)
        [[nodiscard]] bool find_neighbor(int x, int y, unsigned char m,
                                         int &nx, int &ny) const;
        void swap_cells(int ax, int ay, int bx, int by);
        void set_cell(int x, int y, unsigned char m);
        void do_sand(int x, int y);
        void do_liquid(int x, int y, bool oil_swap);
        void do_fire(int x, int y);
        void do_acid(int x, int y);
        void do_lava(int x, int y);
        void do_steam(int x, int y);

        // painting: a circular brush disc, plus the no-gap Bresenham run
        // between consecutive move events
        void stamp_disc(int col, int row);
        void stamp_line(int c0, int r0, int c1, int r1);
    };

    /*
     * Falling-sand particle toy. The palette row picks the material (the
     * active button glows amber), a slider scales the brush, drawing
     * paints into the CA grid and every paint frame advances one
     * generation. Header and hint metrics scale with the host buffer
     * (u = h/240, the g2048 ui-scale); at the classic tier u == 1.
     */
    class Sandbox : public IApp
    {
    public:
        // 9 materials + CLEAR on the palette row
        static constexpr int kBtnCount = 10;

        void create_window() override { create_window(kWidth, kHeight); }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height) override
        {
            create_window(max_client_width, max_client_height, nullptr);
        }

        void create_window(uint32_t max_client_width,
                           uint32_t max_client_height, void *buffer) override;

        zb::SharedPtr<IWindow> window() noexcept override { return window_; }

        void input(const zb::input::input_event &ev) noexcept override
        {
            if (window_)
            {
                window_->input(ev);
            }
        }

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
        static constexpr int kHeaderH = 46;  // palette row + brush row (x u)

        // canvas geometry, decided once in create_window
        int cell_ = SandCanvas::kMinCell;
        int canvas_x_ = 0;
        int canvas_y_ = kHeaderH;

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        SandCanvas *canvas_ = nullptr;
        zb::ui::Button *mat_btns_[kBtnCount] = {};
        zb::ui::Label *brush_val_ = nullptr;
        int selected_ = 1;  // SAND

        event::Subscription<> subs_[kBtnCount];
        event::Subscription<int> sub_brush_;

        unsigned frames_ = 0;

        void install_font();
        void on_button(int idx);
        void sync_palette();
    };
}  // namespace zb::app::sandbox

#endif  // !SANDBOX_HPP
