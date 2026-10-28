#ifndef HOURSMITH_HPP
#define HOURSMITH_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

#include <imraster/canvas.hpp>
#include <imraster/raster.hpp>

namespace zb::app::hoursmith
{
    /*
     * HOURSMITH 晷匠: a sundial you calibrate while the sun runs. Stone
     * gnomons stand on a dial plaza; each one's ink shadow sweeps an arc
     * as the sun crosses the sky, and each hour ring -- color-matched to
     * its stone -- wants the sweep's tip inside itself at that ring's
     * hour. Hold the tip through the window and the plaza bell rings;
     * every bell before dusk advances the smith's day.
     *
     * 2D AND 3D BY DESIGN, not decoration: the plaza, the obelisks and
     * the sweeping shadows are a projected 3D world, while the smith's
     * paper tools -- the plan inset, the tip arcs, the day timeline and
     * the height keys -- are flat. The puzzle is reading one from the
     * other: morning and evening shadows run long and fast, the noon
     * shadow is short and slow, so a stone that rings at nine is the
     * wrong stone at twelve.
     *
     * DETERMINISM CONTRACT: everything is a pure function of the input
     * stream and the frame counter (hash-driven stone shimmer included)
     * -- same drags, same bytes.
     */
    class DialCanvas : public zb::ui::Widget
    {
    public:
        static constexpr int kMaxPieces = 3;
        static constexpr int kLevels = 4;
        static constexpr float kDialR = 150.f;    // plaza radius
        static constexpr float kMarkR = 128.f;    // hour marks ring
        static constexpr int kDayFrames = 3600;   // hour 6 .. hour 18
        static constexpr int kNightFrames = 480;  // summary interlude
        static constexpr float kHLo = 10.f;       // stone height range
        static constexpr float kHHi = 46.f;
        // the plan inset's height keys (x/width/height; y sits at the
        // inset's top and middle rows, computed from vh_)
        static constexpr int kBtnX = 158;
        static constexpr int kBtnW = 26;
        static constexpr int kBtnH = 24;
        static constexpr int kPlanBox = 130;  // inset edge, and its offset
        static constexpr int kTrail = 130;    // tip-trail samples per stone

        void set_geometry(int width, int height);
        void tick();
        void reset();
        void set_level(int level);
        // the shell's key path: raise/lower the picked stone
        void adjust_height(float delta);
        [[nodiscard]] int level() const { return level_; }
        [[nodiscard]] int day() const { return day_; }
        [[nodiscard]] bool is_night() const { return night_; }
        // bells of the CURRENT level that rang (status-line coalescing)
        [[nodiscard]] int level_bells() const;
        // rings on the current level (the status line's "/N")
        [[nodiscard]] int level_targets() const
        {
            return kLevelCount[level_];
        }
        // one line for the status label
        void level_text(char *buf, int cap) const;

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        [[nodiscard]] bool captures_pointer() const override { return true; }

    private:
        struct Piece
        {
            float x, z, h;
        };
        struct Target
        {
            float x, z, r;
            int hour;
        };

        // ---- static level table --------------------------------------
        static constexpr Target kLevelRings[kLevels][kMaxPieces] = {
            // L1: teach the sweep -- noon, generous ring
            {{-30.f, -52.f, 0.f, 12}, {0.f, 0.f, 0.f, 12}, {0.f, 0.f, 0.f, 12}},
            // L2: morning and afternoon, mirrored
            {{-72.f, 8.f, 0.f, 9}, {72.f, 8.f, 0.f, 15}, {0.f, 0.f, 0.f, 12}},
            // L3: three bells, the noon one shortest
            {{-84.f, -36.f, 0.f, 8}, {0.f, -78.f, 0.f, 12}, {84.f, -36.f, 0.f, 16}},
            // L4: the master's day -- long dawn, short noon, long dusk
            {{-70.f, -8.f, 0.f, 7}, {-16.f, -92.f, 0.f, 12}, {86.f, 6.f, 0.f, 17}}};
        static constexpr float kLevelRingR[kLevels] = {12.f, 10.f, 9.f, 8.f};
        static constexpr int kLevelCount[kLevels] = {1, 2, 3, 3};
        static constexpr Piece kLevelStart[kLevels][kMaxPieces] = {
            {{36.f, 84.f, 22.f}, {0.f, 0.f, 22.f}, {0.f, 0.f, 22.f}},
            {{56.f, 60.f, 26.f}, {-56.f, 60.f, 26.f}, {0.f, 0.f, 22.f}},
            {{60.f, 72.f, 22.f}, {-8.f, 96.f, 22.f}, {-60.f, 72.f, 22.f}},
            {{64.f, 88.f, 26.f}, {-8.f, 100.f, 26.f}, {-64.f, 88.f, 26.f}}};
        // the ring a stone serves is its own index: colors pair them
        static const zb::raster::Rgba kPieceColor[kMaxPieces];

        mutable zb::raster::Canvas rc_;
        // painter sort scratch (members: no per-frame allocation)
        struct DrawItem
        {
            float v;
            int kind;  // 0 = stone, 1 = hour mark, 2 = tower, 3 = ring stone
            int a, b;
        };
        mutable std::vector<DrawItem> items_;

        int vw_ = 0, vh_ = 0;
        bool inited_ = false;  // set_geometry laid out level 1 once
        unsigned frame_ = 0;
        int level_ = 0;
        int day_ = 1;
        int frame_in_day_ = 0;
        bool night_ = false;    // dusk summary interlude
        int night_frames_ = 0;  // frames into the interlude
        int flash_ = 0;         // bell-ring glow countdown

        Piece pieces_[kMaxPieces]{};
        Target targets_[kMaxPieces]{};
        int charge_[kMaxPieces] = {};
        bool rung_[kMaxPieces] = {};
        // tip trail for the plan inset (world x,z; oldest overwritten)
        float trail_[kMaxPieces][kTrail][2] = {};
        int trail_len_[kMaxPieces] = {};
        int trail_head_[kMaxPieces] = {};

        int selected_ = -1;
        bool dragging_ = false;
        float drag_off_x_ = 0.f, drag_off_z_ = 0.f;

        // ---- world math ----------------------------------------------
        [[nodiscard]] float hour_now() const;
        // the sun's elevation at a given hour (radians); the ONE place
        // the sun path lives -- the world's shadows and the plan's tip
        // arc both read it
        [[nodiscard]] static float sun_elevation(float hour);
        // sun direction (toward the sun) and its elevation angle
        void sun(float &sx, float &sy, float &sz, float &elev) const;
        void shadow_offset(float h, float &dx, float &dz) const;
        void tip_of(const Piece &p, float &tx, float &tz) const;
        void tip_at_hour(const Piece &p, float hour, float &tx,
                         float &tz) const;
        void build_camera(zb::raster::Camera &cam) const;
        // screen point back onto the plaza plane (false: ray misses ground)
        [[nodiscard]] bool unproject(int sx, int sy, float &x, float &z) const;
        void render_world() const;
        void draw_ground(const zb::raster::Camera &cam) const;
        void draw_shadows(const zb::raster::Camera &cam) const;
        void shadow_hull(const Piece &p, float hx[8], float hz[8],
                         int &n) const;
        void draw_solids(const zb::raster::Camera &cam) const;
        void draw_box_at(const zb::raster::Camera &cam, float cx, float cy,
                         float cz, float hx, float hy, float hz,
                         const zb::raster::Rgba &c) const;
        void draw_sky_lights(const zb::raster::Camera &cam) const;
        void draw_plan(zb::ui::core::Graphics &area) const;
        void draw_timeline(zb::ui::core::Graphics &area) const;
        [[nodiscard]] unsigned hash(unsigned a, unsigned b) const;
    };

    /*
     * App shell: title, status line, RESET.
     */
    class Hoursmith : public IApp
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
            // keys are the shell's (a canvas widget only sees them once
            // focused): level digits and the height brackets live here,
            // everything else goes down the tree
            if (ev.type == zb::input::input_type::key_down)
            {
                if (ev.ch >= '1' && ev.ch <= '4')
                {
                    dial_->set_level(ev.ch - '1');
                    return;
                }
                if (ev.ch == '[')
                {
                    dial_->adjust_height(-2.f);
                    return;
                }
                if (ev.ch == ']')
                {
                    dial_->adjust_height(2.f);
                    return;
                }
            }
            if (window_)
            {
                window_->input(ev);
            }
        }

        void paint() noexcept override
        {
            if (window_)
            {
                dial_->tick();
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
        DialCanvas *dial_ = nullptr;
        zb::ui::Label *status_ = nullptr;
        int last_status_ = -1;

        event::Subscription<> sub_reset_;
    };
}  // namespace zb::app::hoursmith

#endif  // !HOURSMITH_HPP
