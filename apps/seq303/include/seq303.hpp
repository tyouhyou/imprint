#ifndef SEQ303_HPP
#define SEQ303_HPP

#include "canvas_window.hpp"
#include "event.hpp"
#include "iapp.hpp"
#include "imui.hpp"

namespace zb::app::seq303
{
    /*
     * Step-sequencer board: a 16-step grid that edits one LAYER at a
     * time -- layer 0 LEAD (8 scale rows) or layer 1 BASS (the same
     * ladder two octaves down, drawn with its own halo) -- over 3 shared
     * percussion rows. Every PATTERN slot (A..D) holds both melody
     * layers plus the drums; the board shows and edits the playing
     * pattern's active-layer slice. Cells are 3-state (off / note /
     * accent, a tap cycles); every melody row has a SLIDE latch pill at
     * the right edge.
     *
     * Sequencer time is the host frame counter (~60 fps): the step
     * accumulator advances in 1/600 s units so the bpm math stays
     * integer. A monotonic global step counter G (0 at PLAY, +1 per
     * advanced step) tags every queued note event so the wasm host can
     * schedule audio absolutely:
     * (G << 7) | (slide << 6) | (accent << 5) | (layer << 4) | row
     * (rows 0..7 melody with the layer bit, 8..10 drums). A step's notes
     * are queued one step ahead of the playhead (lookahead). Pattern
     * switches are quantized: a queued slot (or CHAIN's next slot) takes
     * over when the playhead wraps to step 0.
     */
    class SeqGrid : public zb::ui::Widget
    {
    public:
        static constexpr int kSteps = 16;
        static constexpr int kMelodyRows = 8;  // top row = highest pitch
        static constexpr int kDrumRows = 3;    // HAT SNR KIK
        static constexpr int kRows = kMelodyRows + kDrumRows;
        static constexpr int kScales = 6;
        static constexpr int kLayers = 2;    // 0 LEAD, 1 BASS
        static constexpr int kPatterns = 4;  // A B C D

        // geometry decided by the app from the host buffer; the row
        // labels and LEDs scale with it (font set once, before paint)
        void set_metrics(int label_w, int pitch, int led_h, int font_px);
        // width of the SLIDE pill zone after the last column
        void set_slide_w(int px)
        {
            slide_w_ = px < 8 ? 8 : px;
            mark_dirty();
        }

        // the active layer's cell in the playing pattern: 0 off, 1 note,
        // 2 accent
        [[nodiscard]] int cell_state(int row, int step) const
        {
            return (row >= 0 && row < kRows && step >= 0 && step < kSteps)
                     ? cell(row, step)
                     : 0;
        }

        void set_playing(bool on);
        [[nodiscard]] bool is_playing() const { return playing_; }
        void set_bpm(int bpm) { bpm_ = bpm < 60 ? 60 : (bpm > 180 ? 180 : bpm); }
        [[nodiscard]] int get_bpm() const { return bpm_; }
        // CLEAR wipes the whole playing pattern (both layers + drums);
        // DICE fills it the same way -- both operate on pattern scope
        void clear();
        void dice();  // deterministic consonant-pattern generator

        // scales cycled by the SCALE button; both layers follow (the
        // bass ladder is the lead ladder transposed down two octaves)
        void cycle_scale();
        [[nodiscard]] const char *scale_name() const;
        [[nodiscard]] int row_midi(int row) const;  // 0..7 -> lead pitch

        // per-layer, per-melody-row SLIDE latch (portamento)
        [[nodiscard]] bool slide(int row) const
        {
            return row >= 0 && row < kMelodyRows && slide_[layer_][row];
        }

        // editing view: which layer the board shows (also drives the
        // knob page on the app side)
        void set_layer(int layer);
        [[nodiscard]] int layer() const { return layer_; }

        // pattern slots: queued switches apply at the next bar wrap;
        // while stopped the switch is immediate
        void select_pattern(int p);
        void copy_to_next();      // current slot -> next letter, queued
        void set_chain(bool on);  // auto-advance A->B->C->D per bar
        [[nodiscard]] bool chain() const { return chain_; }
        [[nodiscard]] int pattern() const { return pattern_; }
        [[nodiscard]] int queued_pattern() const { return queued_; }

        // predicted output level 0..100 for the VU dial (decays per frame)
        [[nodiscard]] int level() const { return level_; }

        // one host frame: decay the VU level, advance the playhead,
        // apply the pattern queue at the bar wrap, queue note triggers
        // for the wasm host (popped through seq303_note_pop)
        void tick();

    protected:
        void draw_at(zb::ui::core::Graphics &area) const override;
        bool on_input(const zb::input::input_event &ev) override;

    private:
        [[nodiscard]] const unsigned char &cell(int row, int step) const;
        [[nodiscard]] unsigned char &cell(int row, int step);

        void trigger_step(int step);
        // queue one step's notes for one voice slice: layer < 0 = drums
        void push_step(int step, int g, int layer);
        void draw_tag(zb::ui::core::Graphics &area, const char16_t *text,
                      int len, int x, int y, const zb::ui::core::Color &c) const;

        // the song: 4 pattern slots x 2 melody layers x 8 rows x 16
        // steps, plus the per-slot drum rows
        unsigned char melody_[kPatterns][kLayers][kMelodyRows][kSteps] = {};
        unsigned char drums_[kPatterns][kDrumRows][kSteps] = {};
        bool slide_[kLayers][kMelodyRows] = {};
        int layer_ = 0;                 // editing view
        int pattern_ = 0;               // playing / shown pattern slot
        int queued_ = -1;               // pending switch, applied at the bar wrap
        bool queued_by_chain_ = false;  // queued_ came from CHAIN, not a tap
        bool chain_ = false;
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
     * SEQ-303: a small two-voice groovebox for a young musician. The UI
     * is the framework: transport and pattern strip (PLAY / DICE /
     * CLEAR / tempo, LEAD-BASS tabs, pattern A..D + CHAIN + COPY), the
     * board canvas, and the voice bay (GaugeDial VU, a 2x3 Knob cluster
     * that edits the ACTIVE layer's voice -- the sixth knob is SWING on
     * LEAD and DRIVE on BASS -- and the SCALE button). Sound is
     * host-side (WebAudio on the wasm page; audio is a host facility by
     * architecture ruling). Every pattern is consonant by construction:
     * the melody rows are one scale, so any cell combination sounds
     * good.
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
                sync_strip();
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
        static constexpr int kKnobs = 6;  // CUTOFF RESO DECAY DELAY + SWING/DRIVE + MIX
        static constexpr int kLayers = SeqGrid::kLayers;
        static constexpr int kPatterns = SeqGrid::kPatterns;

        void install_font();
        void sync_play_button();
        void sync_strip();             // layer tabs + pattern buttons + chain state
        void cycle_scale();            // SCALE button: re-map the rows + button text
        void select_layer(int layer);  // tabs: grid view + knob page

        zb::SharedPtr<zb::app::CanvasWindow> window_;
        SeqGrid *grid_ = nullptr;
        zb::ui::Button *play_btn_ = nullptr;
        zb::ui::Label *bpm_label_ = nullptr;
        zb::ui::GaugeDial *vu_ = nullptr;
        zb::ui::Button *scale_btn_ = nullptr;
        zb::ui::Knob *knobs_[kKnobs] = {};
        zb::ui::Button *layer_btns_[kLayers] = {};
        zb::ui::Button *pat_btns_[kPatterns] = {};
        zb::ui::Button *chain_btn_ = nullptr;
        zb::ui::Button *copy_btn_ = nullptr;
        zb::ui::Label *knob_label_[kKnobs] = {};

        // knob values per layer (the cluster edits the active layer's
        // page; the wasm host reads both pages through seq303_param)
        int knob_page_[kLayers][kKnobs];

        event::Subscription<> sub_play_;
        event::Subscription<> sub_dice_;
        event::Subscription<> sub_clear_;
        event::Subscription<int> sub_bpm_;
        event::Subscription<> sub_scale_;
        event::Subscription<int> sub_knob_[kKnobs];
        event::Subscription<> sub_layer_[kLayers];
        event::Subscription<> sub_pat_[kPatterns];
        event::Subscription<> sub_chain_;
        event::Subscription<> sub_copy_;
        int strip_cache_ = -1;  // packed strip state; setters fire on change only
    };
}  // namespace zb::app::seq303

#endif  // !SEQ303_HPP
