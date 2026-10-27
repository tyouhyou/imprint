#ifndef MOONPULL_HPP
#define MOONPULL_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

#include <imraster/canvas.hpp>
#include <imraster/mesh.hpp>

namespace zb::app::moonpull
{
    /*
     * MOONPULL: a pocket sea under a draggable moon. The player moves the
     * moon along its sky ring (drag: sideways = orbit, vertical = height)
     * and the tidal bulge -- two crests aligned with the moon, two
     * troughs between them -- follows in real time. Three commissions,
     * one night:
     *
     *   1. the ferry leaves its dock only while the reef pass runs deep
     *      (a crest parked on the pass), then crosses to the far dock;
     *   2. the pearler's bed must fall dry (a trough held on the bed);
     *   3. the lantern islet must stay dry while its wick burns (any
     *      crest parked on it fails the commission).
     *
     * Dawn ends the night. The world is a polar mesh of water quads
     * drawn painter-sorted back to front; solids are lit boxes whose
     * light direction IS the moon, so orbiting it swings the shadows.
     * The moon itself is a mesh sphere drawn at 1:1 on the surface after
     * the tent upscale (the viewmodel precedent), with a glitter path
     * added along the crest that faces the eye.
     *
     * DETERMINISM CONTRACT: everything is a pure function of the input
     * stream and the frame counter (hash-driven glitter jitter and wave
     * shimmer included) -- same drags, same bytes.
     */
    class TideCanvas : public zb::ui::Widget
    {
    public:
        // world metrics (units; the projection maps them to pixels)
        static constexpr int kRings = 8;
        static constexpr int kSectors = 24;
        static constexpr float kSeaR = 88.f;       // the working coast
        static constexpr float kMoonR = 110.f;     // the sky ring
        static constexpr float kMoonHLo = 6.f;     // low moon = strong pull
        static constexpr float kMoonHHi = 26.f;    // high moon = weak pull
        static constexpr int kNightFrames = 5400;  // dawn

        void set_geometry(int width, int height);
        void tick();
        void reset();
        [[nodiscard]] int frame() const { return static_cast<int>(frame_); }
        [[nodiscard]] int quests_done() const;
        [[nodiscard]] bool night_over() const { return night_over_ != 0; }
        // the quest state machine's stage (status-line coalescing key)
        [[nodiscard]] int quest_stage() const { return static_cast<int>(quest_); }
        // one line for the status label
        void quest_text(char *buf, int cap) const;

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        [[nodiscard]] bool captures_pointer() const override { return true; }

    private:
        // ---- static world layout -------------------------------------
        static constexpr float kRingR[kRings + 1] = {0.f, 16.f, 34.f, 52.f,
                                                     70.f, 88.f, 110.f, 132.f,
                                                     150.f};
        // site angles (deg, atan2(x, z): 0 = far side) and radii
        static constexpr float kDockA = -16.f;
        static constexpr float kDockB = 16.f;
        static constexpr float kSill = 0.f;
        static constexpr float kPearls = 140.f;
        static constexpr float kLantern = 228.f;
        static constexpr float kCoastR = 88.f;
        static constexpr float kBedR = 84.f;

        // ---- quest state ---------------------------------------------
        enum class Quest : int
        {
            FerryWait = 0,
            FerryCross = 1,
            Pearls = 2,
            Lantern = 3,
            Done = 4
        };

        mutable zb::raster::Canvas rc_;
        // painter sort scratch (members: no per-frame allocation)
        struct DrawItem
        {
            float v;
            int kind;  // 0 = water quad, else solid id
            int a, b;
        };
        mutable std::vector<DrawItem> items_;
        // moon mesh: unit sphere model + per-frame world copy
        zb::raster::MeshBuf moon_model_;
        mutable zb::raster::MeshBuf moon_world_;

        int vw_ = 0, vh_ = 0;
        unsigned frame_ = 0;
        Quest quest_ = Quest::FerryWait;
        int night_over_ = 0;  // 0 running, 1 dawn, 2 lantern flooded, 3 all clear
        bool ferry_done_ = false;
        bool pearls_done_ = false;
        bool lantern_done_ = false;
        bool lantern_failed_ = false;
        int ferry_wait_ = 0;   // consecutive deep-pass frames
        int pearl_dry_ = 0;    // consecutive dry-bed frames
        int lantern_dry_ = 0;  // consecutive dry-islet frames
        float ferry_t_ = 0.f;  // 0 = dock A, 1 = dock B

        // moon state (the player's lever)
        float moon_phi_ = 0.55f;  // azimuth, rad (off the pass at reset)
        float moon_h_ = 12.f;     // height above the sea plane

        // drag state
        bool dragging_ = false;
        int last_x_ = 0, last_y_ = 0;

        // ---- world math ----------------------------------------------
        [[nodiscard]] float tide(float x, float z) const;
        [[nodiscard]] float tide_at(float deg, float r) const;
        [[nodiscard]] float strength() const;
        void build_camera(zb::raster::Camera &cam) const;
        void render_world() const;
        void draw_water(const zb::raster::Camera &cam) const;
        void draw_water_quad(const zb::raster::Camera &cam, int band, int sec,
                             float A) const;
        void solid_center(int id, float &x, float &y, float &z) const;
        void draw_solid(const zb::raster::Camera &cam, int id) const;
        void draw_moon_and_lights(const zb::raster::Camera &cam) const;
        [[nodiscard]] unsigned hash(unsigned a, unsigned b) const;
    };

    /*
     * App shell: title, quest/status line, RESET.
     */
    class Moonpull : public IApp
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
                sea_->tick();
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
        TideCanvas *sea_ = nullptr;
        zb::ui::Label *status_ = nullptr;
        int last_status_ = -1;

        event::Subscription<> sub_reset_;
    };
}  // namespace zb::app::moonpull

#endif  // !MOONPULL_HPP
