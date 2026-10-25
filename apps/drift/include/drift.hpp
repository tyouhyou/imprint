#ifndef DRIFT_HPP
#define DRIFT_HPP

#include <vector>

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::drift
{
    /*
     * SIGNAL DRIFT: a change-blindness game over a procedural
     * monitoring feed. The scene is a pure function of (seed, frame);
     * during the watch phase short signal glitches mask small changes
     * to the feed, and the player then clicks what moved. The reveal
     * is the product pitch played straight: the same runtime re-renders
     * the t=0 scene byte-for-byte next to the current one, and the two
     * frame hashes + the real pixel diff are shown on screen.
     */
    class SceneWidget : public zb::ui::Widget
    {
    public:
        static constexpr int kVW = 240;  // virtual scene space
        static constexpr int kVH = 150;
        static constexpr int kChanges = 3;  // altered details per round

        void set_geometry(int ox, int oy, int scale, int font_px);

        // one host frame: phase machine + glitch bookkeeping
        void tick();

        [[nodiscard]] const char *phase_text() const { return phase_buf_; }
        [[nodiscard]] int score() const { return score_; }
        [[nodiscard]] int round_no() const { return round_; }

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;

    private:
        static constexpr int kWatchLen = 1500;  // frames @ ~60 fps
        static constexpr int kFindLen = 1080;
        static constexpr int kRevealLen = 420;
        static constexpr int kGlitchLen = 14;

        enum class phase
        {
            watch,
            find,
            reveal
        };

        struct change
        {
            int kind = 0;
            int stamp = 0x7fffffff;  // unapplied until the glitch frame
            bool found = false;
        };

        // buffer-pixel mapping (set once per create from the host size)
        int ox_ = 0;
        int oy_ = 0;
        int s_ = 1;

        phase phase_ = phase::watch;
        int round_ = 1;
        int rframe_ = 0;
        int glitch_left_ = 0;
        int score_ = 0;
        change changes_[kChanges];
        unsigned seed_ = 1;

        // find-phase feedback
        int flash_x_ = 0;
        int flash_y_ = 0;
        int flash_until_ = -1;

        char phase_buf_[80] = "WATCH THE FEED";

        // reveal caches: both scenes are static during the reveal, so
        // they render once into offscreen buffers (the zero-copy
        // wrapper mode) and every reveal frame is a diff + blit
        mutable std::vector<unsigned char> buf_a_;
        mutable std::vector<unsigned char> buf_b_;
        mutable int cache_w_ = 0;
        mutable int cache_h_ = 0;
        mutable bool reveal_ready_ = false;
        mutable unsigned hash_a_ = 0;
        mutable unsigned hash_b_ = 0;
        mutable int diff_px_ = 0;

        void new_round();
        void draw_scene(zb::ui::core::Graphics &g, unsigned t, const bool *alts, int glitch, int bx = 0, int by = 0) const;
        void draw_ascii(zb::ui::core::Graphics &g, const char *text,
                        int x, int y, const zb::ui::core::Color &c) const;
    };

    /*
     * The drift app: header (title / phase / score / round labels --
     * stock widgets) over the scene canvas. English in-app text: the
     * runtime TTF family is Latin-only (the page around the canvas
     * carries the localized copy).
     */
    class Drift : public IApp
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
                scene_->tick();
                sync_header();
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
        void sync_header();

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        SceneWidget *scene_ = nullptr;
        zb::ui::Label *phase_label_ = nullptr;
        zb::ui::Label *score_label_ = nullptr;
        zb::ui::Label *round_label_ = nullptr;
        int last_score_ = -1;
        int last_round_ = -1;
        const char *last_phase_ = nullptr;
    };
}  // namespace zb::app::drift

#endif  // !DRIFT_HPP
