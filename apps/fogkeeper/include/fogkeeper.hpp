#ifndef FOGKEEPER_HPP
#define FOGKEEPER_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

#include <imraster/canvas.hpp>
#include <imraster/raster.hpp>

namespace zb::app::fogkeeper
{
    /*
     * FOGKEEPER: night watch over a fog sea, from the lamp gallery. The
     * player sweeps the great beam (drag: sideways = azimuth) and the
     * fleet follows the light: a ship gains headway only while the beam
     * is on it, otherwise it drifts, blind, into the rocks that guard
     * the harbor mouth. Docked ships score; three wrecks or dawn end
     * the watch. The fog itself is the renderer: distance haze melts
     * everything toward the sky color, and the beam's light factor is
     * baked into every sea quad and hull it touches, so the cone reads
     * as a hole burnt through the fog.
     *
     * The beam volume is two nested translucent pyramids (FaceShade
     * alpha, the contact-shadow blend path) over an additive lamp glow;
     * the lit pool where the beam meets the water glows too.
     *
     * DETERMINISM CONTRACT: spawns come from a hash of the frame
     * counter, steering from fixed rules, aiming from the input stream
     * only -- same drags, same bytes.
     */
    class BeamCanvas : public zb::ui::Widget
    {
    public:
        static constexpr int kNightFrames = 5400;
        static constexpr int kMaxShips = 3;
        static constexpr int kWreckLimit = 3;
        static constexpr int kCols = 18;  // sea mesh columns (x)
        static constexpr int kRows = 14;  // sea mesh rows (z bands)

        void set_geometry(int width, int height);
        void tick();
        void reset();
        [[nodiscard]] int frame() const { return static_cast<int>(frame_); }
        [[nodiscard]] int docked() const { return docked_; }
        [[nodiscard]] int wrecks() const { return wrecks_; }
        [[nodiscard]] bool night_over() const { return night_over_; }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        [[nodiscard]] bool captures_pointer() const override { return true; }

    private:
        // one ship in the fleet
        struct Ship
        {
            float x = 0.f, z = 0.f;
            int state = 0;  // 0 empty, 1 sailing, 2 wrecking, 3 gone
            int sink_t = 0;
        };

        mutable zb::raster::Canvas rc_;
        // painter sort scratch (members: no per-frame allocation)
        struct DrawItem
        {
            float v;
            int kind;  // 0 = sea quad, else solid id
            int a, b;
        };
        mutable std::vector<DrawItem> items_;

        int vw_ = 0, vh_ = 0;
        unsigned frame_ = 0;
        int night_over_ = 0;
        int docked_ = 0;
        int wrecks_ = 0;
        int spawned_ = 0;
        unsigned next_spawn_ = 300;

        // the lamp: azimuth only, with a heavy-lamp lag toward the aim
        float aim_ = 0.f;   // where the player points
        float beam_ = 0.f;  // where the lamp actually faces

        Ship ships_[kMaxShips];

        bool dragging_ = false;
        int last_x_ = 0;

        // ---- world math ----------------------------------------------
        [[nodiscard]] float beam_dir_x() const;
        [[nodiscard]] float beam_dir_z() const;
        [[nodiscard]] float beam_hit_x() const;
        [[nodiscard]] float beam_hit_z() const;
        // beam light factor at a world point (0 outside the cone)
        [[nodiscard]] float beam_factor(float x, float y, float z,
                                        float half) const;
        // distance haze toward the sky color, 1 near .. 0 far
        [[nodiscard]] float fog(float z) const;
        void build_camera(zb::raster::Camera &cam) const;
        void render_world() const;
        void draw_sea(const zb::raster::Camera &cam) const;
        void draw_sea_quad(const zb::raster::Camera &cam, int col, int row,
                           float dawn) const;
        void solid_center(int id, float &x, float &y, float &z) const;
        void draw_solid(const zb::raster::Camera &cam, int id, float dawn) const;
        void draw_beam(const zb::raster::Camera &cam) const;
        void draw_lights(const zb::raster::Camera &cam) const;
        [[nodiscard]] unsigned hash(unsigned a, unsigned b) const;
    };

    /*
     * App shell: title, watch status, RESET.
     */
    class Fogkeeper : public IApp
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
                bay_->tick();
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
        BeamCanvas *bay_ = nullptr;
        zb::ui::Label *status_ = nullptr;
        int last_status_ = -1;

        event::Subscription<> sub_reset_;
    };
}  // namespace zb::app::fogkeeper

#endif  // !FOGKEEPER_HPP
