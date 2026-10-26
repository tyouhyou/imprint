#ifndef FIREFLY_HPP
#define FIREFLY_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::firefly
{
    /*
     * The night world: a fixed swarm of fireflies living in a 400x250
     * virtual space rendered at a runtime scale (the drift mapping).
     * The one verb is the lantern: while the pointer is held the light
     * attracts every firefly inside its beam radius; released, the
     * swarm drifts on its own wander noise. Hazards: spider webs stall
     * caught fireflies until the lantern is brought close enough to
     * pull them out, and a wind band pushes the swarm off course. The
     * mother grove in the far corner homes any firefly that reaches it.
     *
     * DETERMINISM CONTRACT: every "random" input (wander noise, flicker
     * phase, star field, tree placement) hashes (index, frame) -- no
     * host RNG, no wall clock. The whole night is a pure function of
     * the input stream: the same taps and the same paint count always
     * produce the same framebuffer, byte for byte.
     */
    class GroveCanvas : public zb::ui::Widget
    {
    public:
        static constexpr int kVW = 400;  // virtual world space
        static constexpr int kVH = 250;
        static constexpr int kSwarm = 120;

        void set_geometry(int ox, int oy, int scale, int font_px);

        // one host frame: advance the night (paused unless playing)
        void tick();
        void start_night();   // fresh swarm, full night ahead
        void set_playing(bool on) { playing_ = on; }
        [[nodiscard]] bool done() const { return done_; }
        [[nodiscard]] int homed() const { return homed_; }
        [[nodiscard]] int dawn_pct() const
        {
            return frame_ * 100 / (kNightLen > 0 ? kNightLen : 1);
        }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;
        // the lantern tracks the pointer across the whole world while
        // pressed -- the drag must survive leaving any single hazard
        [[nodiscard]] bool captures_pointer() const override { return true; }

    private:
        static constexpr int kNightLen = 5400;  // frames @ ~60 fps = 90 s

        struct sparkle
        {
            int x, y;
            int t0;
        };

        // world -> buffer mapping (set once per create)
        int ox_ = 0;
        int oy_ = 0;
        int s_ = 1;

        bool playing_ = false;
        bool done_ = false;
        unsigned frame_ = 0;
        int homed_ = 0;

        // the lantern (virtual coords); lit while the pointer is held
        bool lit_ = false;
        float lamp_x_ = 60.f;
        float lamp_y_ = 60.f;

        float fx_[kSwarm] = {};
        float fy_[kSwarm] = {};
        float vx_[kSwarm] = {};
        float vy_[kSwarm] = {};
        bool stuck_[kSwarm] = {};

        sparkle pops_[16] = {};
        int pop_head_ = 0;

        [[nodiscard]] unsigned hash(int a, int b) const;
        void seed_swarm();
    };

    /*
     * FIREFLY GROVE: hold to shine, lead the swarm home before dawn.
     * The HUD (title, homed count, night progress) is stock widgets;
     * the intro and dawn cards are overlay panels. In-app text stays
     * English (the runtime TTF family is Latin-only); the page around
     * the canvas carries the localized copy.
     */
    class FireflyApp : public IApp
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
                grove_->tick();
                sync_hud();
                window_->invalidate();
                window_->paint();
            }
        }

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
        static constexpr uint32_t kWidth = 800;
        static constexpr uint32_t kHeight = 520;

        void install_font();
        void sync_hud();
        void show_intro(bool on);

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        GroveCanvas *grove_ = nullptr;
        zb::ui::Label *homed_label_ = nullptr;
        zb::ui::ProgressBar *night_bar_ = nullptr;
        zb::ui::Panel *intro_ = nullptr;
        zb::ui::Panel *dawn_card_ = nullptr;
        zb::ui::Label *dawn_title_ = nullptr;
        zb::ui::Label *dawn_score_ = nullptr;

        event::Subscription<> sub_start_;
        event::Subscription<> sub_again_;
        int last_homed_ = -1;   // HUD cache: setters fire on change only
        bool last_done_ = false;
    };
}  // namespace zb::app::firefly

#endif  // !FIREFLY_HPP
