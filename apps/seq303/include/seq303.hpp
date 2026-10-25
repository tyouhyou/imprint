#ifndef SEQ303_HPP
#define SEQ303_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::seq303
{
    /*
     * Step-sequencer grid: 8 melody rows (the active scale across two
     * octaves, top row = highest pitch) over 3 percussion rows, 16 steps.
     * One custom widget draws the whole board -- cells, beat gutters,
     * row labels, playhead, step LEDs, per-melody-row SLIDE pills -- so
     * the board's look lives in one place; the transport (PLAY / CLEAR /
     * DICE / tempo slider) and the voice bay (VU dial, knobs, scale
     * button) are stock widgets around it.
     *
     * Cells are 3-state: 0 off, 1 note, 2 accent; a tap cycles. The
     * melody row labels and pitches come from the active scale.
     *
     * Sequencer time is the host frame counter (the shell calls paint()
     * once per frame, ~60 fps): the step accumulator advances in 1/600 s
     * units so the bpm -> frames-per-step math stays integer. A global
     * step counter G (0 at PLAY, +1 per advanced step) tags every queued
     * note event so the wasm host can schedule audio absolutely:
     * (G << 6) | (slide << 5) | (accent << 4) | row. A step's notes are
     * queued one step ahead of the playhead (lookahead).
     */
    class SeqGrid : public zb::ui::Widget
    {
    public:
        static constexpr int kSteps = 16;
        static constexpr int kMelodyRows = 8;  // top row = highest pitch
        static constexpr int kDrumRows = 3;    // HAT SNR KIK
        static constexpr int kRows = kMelodyRows + kDrumRows;
        static constexpr int kScales = 6;

        // geometry decided by the app from the host buffer; the row
        // labels and LEDs scale with it (font set once, before paint)
        void set_metrics(int label_w, int pitch, int led_h, int font_px);
        // width of the SLIDE pill zone after the last column
        void set_slide_w(int px)
        {
            slide_w_ = px < 8 ? 8 : px;
            mark_dirty();
        }

        // 0 off, 1 note, 2 accent
        [[nodiscard]] int cell_state(int row, int step) const
        {
            return (row >= 0 && row < kRows && step >= 0 && step < kSteps)
                     ? pattern_[row][step]
                     : 0;
        }

        void set_playing(bool on);
        [[nodiscard]] bool is_playing() const { return playing_; }
        void set_bpm(int bpm) { bpm_ = bpm < 60 ? 60 : (bpm > 180 ? 180 : bpm); }
        [[nodiscard]] int get_bpm() const { return bpm_; }
        void clear();
        void dice();  // deterministic consonant-pattern generator

        // scales cycled by the SCALE button; the melody row labels and
        // pitches redraw from the active scale, the pattern bits stay
        void cycle_scale();
        [[nodiscard]] const char *scale_name() const;
        [[nodiscard]] int row_midi(int row) const;  // 0..7 -> midi pitch

        // per melody-row SLIDE latch (portamento), toggled on the pills
        [[nodiscard]] bool slide(int row) const
        {
            return row >= 0 && row < kMelodyRows && slide_[row];
        }

        // predicted output level 0..100 for the VU dial (decays per frame)
        [[nodiscard]] int level() const { return level_; }

        // one host frame: decay the VU level, advance the playhead, queue
        // note triggers for the wasm host (popped through seq303_note_pop)
        void tick();

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;

    private:
        void trigger_step(int step);
        void push_step(int step, int g);
        void draw_tag(zb::ui::core::Graphics &area, const char16_t *text,
                      int len, int x, int y, const zb::ui::core::Color &c) const;

        unsigned char pattern_[kRows][kSteps] = {};  // 0 off, 1 note, 2 accent
        bool slide_[kMelodyRows] = {};
        int scale_ = 0;
        int label_w_ = 30;
        int pitch_ = 14;
        int led_h_ = 10;
        int slide_w_ = 14;

        bool playing_ = false;
        int step_ = 0;
        int counter_ = 0;  // global step counter G: 0 at PLAY, +1 per advance
        int acc10_ = 0;    // 1/600 s units; 10 per host frame (60 fps)
        int bpm_ = 120;
        int level_ = 0;  // predicted output level, decays per frame
        unsigned rng_ = 0x9E3779B9u;
    };

    /*
     * SEQ-303: a small step sequencer for a young musician. The UI is
     * the framework (buttons / slider / labels, one custom canvas widget
     * for the board, and the voice bay: a GaugeDial VU, six Knobs and a
     * scale Button); sound is host-side (WebAudio on the wasm page --
     * audio is a host facility by architecture ruling). Every pattern is
     * consonant by construction: the melody rows are one scale, so any
     * cell combination sounds good.
     */
    class Sequencer : public IApp
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
                grid_->tick();
                if (vu_ != nullptr)
                {
                    vu_->set_value(grid_->level());
                }
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
        static constexpr int kKnobs = 6;  // CUTOFF RESO DECAY DELAY SWING MIX

        void install_font();
        void sync_play_button();
        void cycle_scale();  // SCALE button: re-map the rows + button text

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        SeqGrid *grid_ = nullptr;
        zb::ui::Button *play_btn_ = nullptr;
        zb::ui::Label *bpm_label_ = nullptr;
        zb::ui::GaugeDial *vu_ = nullptr;
        zb::ui::Button *scale_btn_ = nullptr;

        event::Subscription<> sub_play_;
        event::Subscription<> sub_dice_;
        event::Subscription<> sub_clear_;
        event::Subscription<int> sub_bpm_;
        event::Subscription<> sub_scale_;
        event::Subscription<int> sub_knob_[kKnobs];
    };
}  // namespace zb::app::seq303

#endif  // !SEQ303_HPP
