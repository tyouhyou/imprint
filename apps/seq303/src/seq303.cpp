#include "seq303.hpp"

#include <cstdio>
#include <cstring>

#if defined(IMCORE_HAS_TTF_RUNTIME)
#include "seq303_font.gen.hpp"  // bytes_embed blob (wasm; desktop USE_TTF_RUNTIME=ON)
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace zb::app::seq303;
using namespace zb::ui;

namespace
{
    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    /*
     * Warm palette: the whole board lives on one amber ramp over dark
     * roast browns, so the busy 16x11 grid still reads as a single
     * instrument panel instead of a spreadsheet. The bass layer's halo
     * leans a shade redder so the editing view is identifiable at a
     * glance without stealing attention from the lead.
     */
    const core::Color kBoardBg = core::Color::from(0x24, 0x1b, 0x15);
    const core::Color kCellOff = core::Color::from(0x3e, 0x31, 0x26);
    const core::Color kCellOffBeat = core::Color::from(0x4a, 0x3a, 0x2c);
    const core::Color kMelodyOn = core::Color::from(0xff, 0xb4, 0x54);
    const core::Color kBassHalo = core::Color::from(0xff, 0x9a, 0x4d);
    const core::Color kMelodyCore = core::Color::from(0xff, 0xe2, 0xb0);
    const core::Color kAccentCore = core::Color::from(0xff, 0xf4, 0xe0);
    const core::Color kKickOn = core::Color::from(0xff, 0x78, 0x47);
    const core::Color kSnareOn = core::Color::from(0xff, 0xd1, 0x8a);
    const core::Color kHatOn = core::Color::from(0xf5, 0xea, 0xd8);
    const core::Color kCream = core::Color::from(0xf2, 0xe6, 0xd0);
    const core::Color kDim = core::Color::from(0x9a, 0x86, 0x6e);
    const core::Color kPlayhead = core::Color::from(0x6b, 0x50, 0x2f);
    const core::Color kLedOff = core::Color::from(0x3a, 0x2e, 0x22);
    const core::Color kLedOn = core::Color::from(0xff, 0xb4, 0x54);
    const core::Color kLedBarOff = core::Color::from(0x4a, 0x3a, 0x2c);
    const core::Color kLedBarBeat = core::Color::from(0x74, 0x5a, 0x42);
    const core::Color kFieldBg = core::Color::from(0x2b, 0x21, 0x19);
    const core::Color kBorderDim = core::Color::from(0x3a, 0x2d, 0x20);
    const core::Color kInkOnAmber = core::Color::from(0x24, 0x1b, 0x15);

    // the six playable scales, top row = highest pitch; the SCALE button
    // cycles them, the row labels and the wasm host (midi params) follow
    struct scale_def
    {
        const char *name;
        int midi[8];
        const char16_t *labels[8];
    };
    const scale_def kScaleTable[] = {
        {"PENTA", {76, 74, 72, 69, 67, 64, 62, 60}, {u"E5", u"D5", u"C5", u"A4", u"G4", u"E4", u"D4", u"C4"}},
        {"MAJOR", {72, 71, 69, 67, 65, 64, 62, 60}, {u"C5", u"B4", u"A4", u"G4", u"F4", u"E4", u"D4", u"C4"}},
        {"MINOR", {72, 70, 68, 67, 65, 63, 62, 60}, {u"C5", u"Bb4", u"Ab4", u"G4", u"F4", u"Eb4", u"D4", u"C4"}},
        {"DORIAN", {72, 70, 69, 67, 65, 64, 62, 60}, {u"C5", u"Bb4", u"A4", u"G4", u"F4", u"E4", u"D4", u"C4"}},
        {"MIXO", {72, 71, 69, 67, 65, 64, 62, 60}, {u"C5", u"B4", u"A4", u"G4", u"F4", u"E4", u"D4", u"C4"}},
        {"CHROM", {72, 71, 70, 69, 68, 67, 66, 65}, {u"C5", u"B4", u"Bb4", u"A4", u"Ab4", u"G4", u"Gb4", u"F4"}},
    };
    constexpr int kScaleCount =
        static_cast<int>(sizeof(kScaleTable) / sizeof(kScaleTable[0]));

    // knob order on BOTH pages: cutoff reso decay delay, then SWING on
    // the lead page / DRIVE on the bass page, then mix; 0..100 each
    constexpr int kKnobInitLead[6] = {55, 25, 45, 35, 50, 50};
    constexpr int kKnobInitBass[6] = {45, 40, 50, 15, 50, 30};

    /*
     * Live parameter mirror drained by the wasm host (seq303_param):
     * 0..5 the LEAD knob page, 6 the bpm, 7..14 the lead row midi
     * pitches, 15 the active (edited) layer, 16..21 the BASS knob page,
     * 22 the playing pattern slot. The bass pitches are the lead ladder
     * transposed down two octaves, so the host derives them without a
     * second table.
     */
    constexpr int kParamCount = 23;
    int g_params[kParamCount] = {
        kKnobInitLead[0], kKnobInitLead[1], kKnobInitLead[2],
        kKnobInitLead[3], kKnobInitLead[4], kKnobInitLead[5],
        120, 76, 74, 72, 69, 67, 64, 62, 60, 0, 0,
        kKnobInitBass[0], kKnobInitBass[1], kKnobInitBass[2],
        kKnobInitBass[3], kKnobInitBass[4], kKnobInitBass[5]};

    // note trigger queue drained by the wasm host (seq303_note_pop);
    // each entry packs (global_step << 7) | (slide << 6) | (accent << 5)
    // | (layer << 4) | row; rows 0..7 melody (layer 0 lead, 1 bass),
    // 8 HAT, 9 SNARE, 10 KIK (drums carry no layer)
    constexpr int kQueue = 64;
    int g_notes[kQueue] = {};
    unsigned g_note_head = 0;
    unsigned g_note_tail = 0;
    bool g_playing = false;

    void push_note(const int packed)
    {
        const unsigned next = (g_note_tail + 1u) % kQueue;
        if (next != g_note_head)
        {
            g_notes[g_note_tail] = packed;
            g_note_tail = next;
        }
    }
}  // namespace

#ifdef __EMSCRIPTEN__
// demo-host seam (page-only, not part of the zbapi C-ABI surface): pop
// one packed note event (-1 = empty) and the playing state, plus the
// live parameter mirror, so the page's WebAudio synth can sound what
// the sequencer step machine triggered
extern "C" EMSCRIPTEN_KEEPALIVE int seq303_note_pop(void)
{
    if (g_note_head == g_note_tail)
    {
        return -1;
    }
    const int packed = g_notes[g_note_head];
    g_note_head = (g_note_head + 1u) % kQueue;
    return packed;
}

extern "C" EMSCRIPTEN_KEEPALIVE int seq303_is_playing(void)
{
    return g_playing ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int seq303_param(int idx)
{
    if (idx < 0 || idx >= kParamCount)
    {
        return -1;
    }
    return g_params[idx];
}
#endif

void SeqGrid::set_metrics(const int label_w, const int pitch, const int led_h,
                          const int font_px)
{
    label_w_ = label_w;
    pitch_ = pitch;
    led_h_ = led_h;
    set_font_size(font_px);
    mark_dirty();
}

const unsigned char &SeqGrid::cell(const int row, const int step) const
{
    return row < kMelodyRows ? melody_[pattern_][layer_][row][step]
                             : drums_[pattern_][row - kMelodyRows][step];
}

unsigned char &SeqGrid::cell(const int row, const int step)
{
    return row < kMelodyRows ? melody_[pattern_][layer_][row][step]
                             : drums_[pattern_][row - kMelodyRows][step];
}

void SeqGrid::set_playing(const bool on)
{
    if (on == playing_)
    {
        return;
    }
    playing_ = on;
    g_playing = on;
    if (on)
    {
        step_ = 0;
        acc10_ = 0;
        counter_ = 0;
        // play start: step 0 sounds on counter 0 for every voice, step
        // 1 is queued ahead on counter 1 (trigger_step keeps the
        // lookahead going)
        push_step(0, 0, 0);
        push_step(0, 0, 1);
        push_step(0, 0, -1);
        trigger_step(0);
    }
    mark_dirty();
}

void SeqGrid::set_layer(const int layer)
{
    if (layer < 0 || layer >= kLayers || layer == layer_)
    {
        return;
    }
    layer_ = layer;
    mark_dirty();
}

void SeqGrid::select_pattern(const int p)
{
    if (p < 0 || p >= kPatterns)
    {
        return;
    }
    if (!playing_)
    {
        queued_ = -1;
        if (p != pattern_)
        {
            pattern_ = p;
            g_params[22] = p;  // wasm host mirror (the playing slot)
            mark_dirty();
        }
    }
    else if (p != pattern_)
    {
        // quantized: takes over when the playhead wraps to step 0
        queued_ = p;
    }
}

void SeqGrid::copy_to_next()
{
    const int next = (pattern_ + 1) % kPatterns;
    std::memcpy(melody_[next], melody_[pattern_], sizeof melody_[next]);
    std::memcpy(drums_[next], drums_[pattern_], sizeof drums_[next]);
    select_pattern(next);
}

void SeqGrid::set_chain(const bool on)
{
    chain_ = on;
    if (!on)
    {
        // a pending auto-advance dies with the switch; a user-queued
        // slot survives (it was tapped deliberately)
        if (queued_ == (pattern_ + 1) % kPatterns)
        {
            queued_ = -1;
        }
    }
}

void SeqGrid::clear()
{
    for (auto &layer : melody_[pattern_])
    {
        for (auto &row : layer)
        {
            for (unsigned char &c : row)
            {
                c = 0;
            }
        }
    }
    for (auto &row : drums_[pattern_])
    {
        for (unsigned char &c : row)
        {
            c = 0;
        }
    }
    mark_dirty();
}

void SeqGrid::dice()
{
    // deterministic consonant fill of the whole playing pattern: a
    // sparse lead line (pentatonic rows -- any pick sounds well
    // together), four-on-the-floor kick, backbeat snare, sparse hats,
    // and a bass voice that shadows the kick on the low rows with an
    // accent at the top of each half bar
    clear();
    rng_ = rng_ * 1664525u + 1013904223u;
    unsigned s = rng_ | 1u;
    auto rnd = [&s]
    {
        s = s * 1664525u + 1013904223u;
        return (s >> 10) & 0x3ffu;
    };
    // lead rows weighted toward the middle of the ramp (C5 A4 G4 C4)
    static const int kPick[12] = {2, 4, 3, 7, 2, 3, 4, 7, 0, 1, 5, 6};
    for (int col = 0; col < kSteps; ++col)
    {
        if (rnd() % 100u < 58u)
        {
            melody_[pattern_][0][kPick[rnd() % 12u]][col] = 1;
        }
        if (rnd() % 100u < 18u)
        {
            melody_[pattern_][0][kPick[rnd() % 12u]][col] = 1;
        }
    }
    for (int col = 0; col < kSteps; ++col)
    {
        if (col % 4 == 0 || rnd() % 100u < 12u)
        {
            drums_[pattern_][2][col] = 1;  // KIK
        }
        if (col % 8 == 4 || rnd() % 100u < 10u)
        {
            drums_[pattern_][1][col] = 1;  // SNR
        }
        if (rnd() % 100u < 55u)
        {
            drums_[pattern_][0][col] = 1;  // HAT
        }
    }
    // bass: every kick column gets the lowest-pitch region, accented on
    // the half-bar; a few extra off-beat eighths keep it walking
    static const int kBassPick[6] = {7, 7, 6, 4, 5, 7};
    for (int col = 0; col < kSteps; ++col)
    {
        const bool on_kick = drums_[pattern_][2][col] != 0;
        if (on_kick || rnd() % 100u < 15u)
        {
            melody_[pattern_][1][kBassPick[rnd() % 6u]][col] =
                col % 8 == 0 ? 2 : 1;
        }
    }
}

void SeqGrid::tick()
{
    level_ = level_ * 88 / 100;  // VU decay, one host frame at a time
    if (!playing_)
    {
        return;
    }
    acc10_ += 10;                       // one 60 fps frame == 10 units of 1/600 s
    const int threshold = 9000 / bpm_;  // step length in the same units
    while (acc10_ >= threshold)
    {
        acc10_ -= threshold;
        step_ = (step_ + 1) % kSteps;
        ++counter_;
        if (step_ == 0)
        {
            // bar wrap: CHAIN queues the next slot; any queued switch
            // (user or chain) takes over here
            if (chain_)
            {
                queued_ = (pattern_ + 1) % kPatterns;
            }
            if (queued_ >= 0 && queued_ < kPatterns && queued_ != pattern_)
            {
                pattern_ = queued_;
                g_params[22] = pattern_;  // wasm host mirror
                mark_dirty();
            }
            queued_ = -1;
        }
        trigger_step(step_);
    }
}

/*
 * The playhead entered `step`: raise the predicted output level from
 * every voice's notes in this column and queue the NEXT step's notes
 * one advance early, tagged with the counter they will sound at
 * (one-step lookahead, once per step).
 */
void SeqGrid::trigger_step(const int step)
{
    bool any = false;
    bool accent = false;
    for (int r = 0; r < kDrumRows; ++r)
    {
        const unsigned char st = drums_[pattern_][r][step];
        if (st == 0)
        {
            continue;
        }
        any = true;
        if (st == 2)
        {
            accent = true;
        }
        if (r == kDrumRows - 1)
        {
            level_ = 100;  // KIK always peaks the lamp
        }
    }
    for (int layer = 0; layer < kLayers; ++layer)
    {
        for (int row = 0; row < kMelodyRows; ++row)
        {
            const unsigned char st = melody_[pattern_][layer][row][step];
            if (st == 0)
            {
                continue;
            }
            any = true;
            if (st == 2)
            {
                accent = true;
            }
        }
    }
    const int target = accent ? 95 : (any ? 70 : 0);
    if (target > level_)
    {
        level_ = target;
    }
    push_step((step + 1) % kSteps, counter_ + 1, 0);
    push_step((step + 1) % kSteps, counter_ + 1, 1);
    push_step((step + 1) % kSteps, counter_ + 1, -1);
}

// queue one step's notes for one voice slice, packed with the counter
// they sound on; layer < 0 = the shared drum rows, layer 1 is the mono
// bass (only the lowest lit pitch of the column sounds)
void SeqGrid::push_step(const int step, const int g, const int layer)
{
    if (layer < 0)
    {
        for (int r = 0; r < kDrumRows; ++r)
        {
            const unsigned char st = drums_[pattern_][r][step];
            if (st == 0)
            {
                continue;
            }
            push_note((g << 7) | (st == 2 ? (1 << 5) : 0) |
                      (r + kMelodyRows));
        }
        return;
    }
    if (layer == 1)
    {
        for (int row = kMelodyRows - 1; row >= 0; --row)
        {
            const unsigned char st = melody_[pattern_][1][row][step];
            if (st == 0)
            {
                continue;
            }
            push_note((g << 7) | (slide_[1][row] ? (1 << 6) : 0) |
                      (st == 2 ? (1 << 5) : 0) | (1 << 4) | row);
            break;  // one voice: the lowest lit pitch wins
        }
        return;
    }
    for (int row = 0; row < kMelodyRows; ++row)
    {
        const unsigned char st = melody_[pattern_][0][row][step];
        if (st == 0)
        {
            continue;
        }
        push_note((g << 7) | (slide_[0][row] ? (1 << 6) : 0) |
                  (st == 2 ? (1 << 5) : 0) | row);
    }
}

void SeqGrid::cycle_scale()
{
    scale_ = (scale_ + 1) % kScaleCount;
    mark_dirty();
}

const char *SeqGrid::scale_name() const
{
    return kScaleTable[scale_].name;
}

int SeqGrid::row_midi(const int row) const
{
    return (row >= 0 && row < kMelodyRows) ? kScaleTable[scale_].midi[row] : 0;
}

void SeqGrid::draw_tag(core::Graphics &area, const char16_t *text, const int len,
                       const int x, const int y, const core::Color &c) const
{
    draw_text_at(area, text, len, x, y, c);
}

void SeqGrid::draw_at(core::Graphics &area) const
{
    const int cs = pitch_ - 2;
    area.fill(kBoardBg);

    static const char16_t *const kDrumName[kDrumRows] = {u"HAT", u"SNR", u"KIK"};
    static const int kDrumNameLen[kDrumRows] = {3, 3, 3};

    for (int row = 0; row < kRows; ++row)
    {
        const int y = row * pitch_ + 1;
        const bool drum = row >= kMelodyRows;
        // row label rides the left column: melody rows carry the active
        // scale's note names (the bass view two octaves down), drums
        // stay dim
        if (drum)
        {
            draw_tag(area, kDrumName[row - kMelodyRows],
                     kDrumNameLen[row - kMelodyRows], 2, y + cs / 2 - 3, kDim);
        }
        else
        {
            char16_t lab[8] = {};
            int len = 0;
            while (len < 7 && kScaleTable[scale_].labels[row][len] != u'\0')
            {
                lab[len] = kScaleTable[scale_].labels[row][len];
                ++len;
            }
            if (layer_ == 1 && len > 0 && lab[len - 1] >= u'0' + 2)
            {
                // the bass ladder reads the same scale two octaves down
                lab[len - 1] = static_cast<char16_t>(lab[len - 1] - 2);
            }
            draw_tag(area, lab, len, 2, y + cs / 2 - 3, kCream);
        }
        for (int col = 0; col < kSteps; ++col)
        {
            const int x = label_w_ + col * pitch_ + 1;
            const bool beat_head = col % 4 == 0;
            const unsigned char st = cell(row, col);
            const bool on = st != 0;
            const bool acc = st == 2;
            const bool ph = playing_ && col == step_;
            if (on)
            {
                const core::Color &base = drum
                                            ? (row == 10 ? kKickOn : (row == 9 ? kSnareOn : kHatOn))
                                            : (layer_ == 1 ? kBassHalo : kMelodyOn);
                // outer pad = the "LED halo", inner core the lit lamp;
                // accents burn white-hot inside the family halo
                area.fill_round_rect(x - 1, y - 1, x + cs, y + cs, cs / 4,
                                     ph ? core::Color::from(0xff, 0xe2, 0xb0)
                                        : base);
                area.fill_round_rect(x + 1, y + 1, x + cs - 2, y + cs - 2, cs / 5,
                                     ph ? core::Color::from(0xff, 0xff, 0xf4)
                                        : (acc ? kAccentCore
                                               : (row == 10
                                                      ? core::Color::from(0xff, 0xa8, 0x84)
                                                      : kMelodyCore)));
            }
            else
            {
                area.fill_round_rect(x, y, x + cs - 1, y + cs - 1, cs / 5,
                                     beat_head ? kCellOffBeat : kCellOff);
            }
        }
        // SLIDE latch pill at the right edge of every melody row; the
        // label only fits once the pitch leaves room for it
        if (!drum && slide_w_ > 0)
        {
            const int px = label_w_ + kSteps * pitch_ + 1;
            const bool lit = slide_[layer_][row];
            area.fill_round_rect(px, y, px + slide_w_ - 3, y + cs - 1, cs / 5,
                                 lit ? kMelodyOn : kCellOffBeat);
            if (pitch_ >= 12)
            {
                const int adv = advance_of(u"SL", 2);
                const int tx = px + 1 + (slide_w_ - 3 - adv) / 2;
                draw_tag(area, u"SL", 2, tx > px ? tx : px, y + cs / 2 - 3,
                         lit ? kBoardBg : kDim);
            }
        }
    }

    // playhead column: a dim amber strip under the cells, so the lit
    // lamps keep their contrast
    if (playing_)
    {
        const int x = label_w_ + step_ * pitch_;
        area.fill_rect(x, 0, x + pitch_ - 2, kRows * pitch_ - 1, kPlayhead);
    }

    // half-row of air between the drum block and the step LEDs, so the
    // drums read as their own section; rows stay at their original y so
    // the input hit test (row = ly / pitch_) keeps matching
    const int led_y = kRows * pitch_ + 6 + pitch_ / 2;
    for (int col = 0; col < kSteps; ++col)
    {
        const int x = label_w_ + col * pitch_ + 1;
        const bool on = playing_ && col == step_;
        area.fill_rect(x, led_y, x + 10, led_y + 4,
                       on ? kLedOn : (col % 4 == 0 ? kLedBarBeat : kLedBarOff));
    }
}

bool SeqGrid::on_input(const zb::input::input_event &ev)
{
    if (ev.type != zb::input::input_type::touch_down)
    {
        return false;
    }
    const core::impoint_t p = get_position();
    const int lx = ev.x - p.x - label_w_;
    const int ly = ev.y - p.y;
    if (lx < 0 || ly < 0 || ly >= kRows * pitch_)
    {
        return false;
    }
    const int col = lx / pitch_;
    const int row = ly / pitch_;
    if (row < 0 || row >= kRows)
    {
        return false;
    }
    if (col >= kSteps)
    {
        // the SLIDE pill zone after the last column: melody rows latch,
        // drum rows have no pill
        if (row >= kMelodyRows)
        {
            return false;
        }
        slide_[layer_][row] = !slide_[layer_][row];
        mark_dirty();
        return true;
    }
    if (col < 0)
    {
        return false;
    }
    // a tap cycles the cell it lands in: off -> note -> accent -> off
    unsigned char &st = cell(row, col);
    st = st >= 2 ? 0 : static_cast<unsigned char>(st + 1);
    mark_dirty();
    return true;
}

void Sequencer::install_font()
{
#if defined(IMCORE_HAS_TTF_RUNTIME)
    if (zb::ui::has_font_family())
    {
        return;
    }
    // function-level static: the family must outlive every paint (the
    // showcase install_font shape)
    static const zb::ui::TtfFamily family =
        zb::ui::TtfFamily::from_memory(seq303_font, seq303_font_len);
    zb::ui::set_font_family(family);
#endif
}

void Sequencer::sync_play_button()
{
    if (!play_btn_)
    {
        return;
    }
    const bool on = grid_ && grid_->is_playing();
    play_btn_->set_text(on ? "STOP" : "PLAY");
    play_btn_->set_text_color(on ? kInkOnAmber : kCream);
    play_btn_->set_background_color(on ? kMelodyOn : kFieldBg);
}

void Sequencer::sync_strip()
{
    if (!grid_)
    {
        return;
    }
    // layer tabs: the active one lights amber with dark ink
    for (int i = 0; i < kLayers; ++i)
    {
        const bool on = grid_->layer() == i;
        layer_btns_[i]->set_text_color(on ? kInkOnAmber : kCream);
        layer_btns_[i]->set_background_color(on ? kMelodyOn : kFieldBg);
    }
    // pattern slots: current = amber text, queued = amber outline
    const int cur = grid_->pattern();
    const int queued = grid_->queued_pattern();
    for (int i = 0; i < kPatterns; ++i)
    {
        pat_btns_[i]->set_text_color(i == cur ? kMelodyOn : kCream);
        pat_btns_[i]->set_border_color(i == queued ? kMelodyOn : kBorderDim);
    }
    const bool chain_on = grid_->chain();
    chain_btn_->set_text_color(chain_on ? kInkOnAmber : kCream);
    chain_btn_->set_background_color(chain_on ? kMelodyOn : kFieldBg);
}

void Sequencer::select_layer(const int layer)
{
    if (!grid_)
    {
        return;
    }
    grid_->set_layer(layer);
    g_params[15] = layer;
    // the knob cluster edits the active page: reload the values and
    // swap the sixth knob's role (SWING on lead, DRIVE on bass)
    for (int i = 0; i < kKnobs; ++i)
    {
        knobs_[i]->set_value(knob_page_[layer][i]);
    }
    knob_label_[5]->set_text(layer == 1 ? "DRIVE" : "SWING");
    sync_strip();
}

void Sequencer::cycle_scale()
{
    if (grid_ == nullptr)
    {
        return;
    }
    grid_->cycle_scale();
    // the wasm host reads the row pitches through seq303_param
    for (int row = 0; row < SeqGrid::kMelodyRows; ++row)
    {
        g_params[7 + row] = grid_->row_midi(row);
    }
    if (scale_btn_ != nullptr)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "SCALE %s", grid_->scale_name());
        scale_btn_->set_text(buf);
    }
}

void Sequencer::create_window(const uint32_t max_client_width,
                              const uint32_t max_client_height, void *buffer)
{
    install_font();

    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    for (int i = 0; i < kKnobs; ++i)
    {
        knob_page_[0][i] = kKnobInitLead[i];
        knob_page_[1][i] = kKnobInitBass[i];
    }

    const int w = static_cast<int>(max_client_width);
    const int h = static_cast<int>(max_client_height);
    // ui scale from the host buffer (the g2048 shape); row 1 transport,
    // row 2 the pattern strip (LEAD/BASS + A..D + CHAIN + COPY)
    const int u = clamp_i(h / 240, 1, 6);
    const int header_h = 66 * u;

    auto &root = window_->root();
    root.set_background_color(core::Color::from(0x1a, 0x13, 0x10));

    auto title = std::make_unique<Label>();
    title->set_text("SEQ-303");
    title->set_text_color(kMelodyOn);
    title->set_bold(true);
    title->set_size(46 * u, 24 * u);
    title->set_position(10 * u, 10 * u);
    title->set_font_size(9 * u);
    root.add_child(std::move(title));

    auto play = std::make_unique<Button>();
    play->set_text("PLAY");
    play->set_size(44 * u, 26 * u);
    play->set_position(62 * u, 8 * u);
    play->set_font_size(8 * u);
    play->set_border_color(kMelodyOn);
    play_btn_ = play.get();
    sub_play_ = play->clicked.subscribe([this]
                                        {
                                            grid_->set_playing(!grid_->is_playing());
                                            sync_play_button(); });
    root.add_child(std::move(play));

    auto dice = std::make_unique<Button>();
    dice->set_text("DICE");
    dice->set_text_color(kCream);
    dice->set_size(42 * u, 26 * u);
    dice->set_position(112 * u, 8 * u);
    dice->set_font_size(7 * u);
    sub_dice_ = dice->clicked.subscribe([this]
                                        { grid_->dice(); });
    root.add_child(std::move(dice));

    auto clear = std::make_unique<Button>();
    clear->set_text("CLEAR");
    clear->set_text_color(kCream);
    clear->set_size(50 * u, 26 * u);
    clear->set_position(160 * u, 8 * u);
    clear->set_font_size(7 * u);
    sub_clear_ = clear->clicked.subscribe([this]
                                          { grid_->clear(); });
    root.add_child(std::move(clear));

    const int bpm_x = 218 * u > w - 184 * u ? 218 * u : w - 184 * u;
    auto bpm_tag = std::make_unique<Label>();
    bpm_tag->set_text("BPM");
    bpm_tag->set_text_color(kDim);
    bpm_tag->set_size(30 * u, 16 * u);
    bpm_tag->set_position(bpm_x, 13 * u);
    bpm_tag->set_font_size(7 * u);
    root.add_child(std::move(bpm_tag));

    auto bpm = std::make_unique<Slider>();
    bpm->set_range(60, 180);
    bpm->set_value(120);
    bpm->set_size(100 * u, 22 * u);
    bpm->set_position(bpm_x + 32 * u, 10 * u);
    bpm->set_thumb_color(kMelodyOn);
    bpm->set_track_color(kLedBarBeat);
    bpm->set_thumb_width(clamp_i(8 * u, 10, 24));
    bpm->set_track_height(clamp_i(4 * u, 4, 10));
    sub_bpm_ = bpm->changed.subscribe([this](const int v)
                                      {
                                          grid_->set_bpm(v);
                                          g_params[6] = v;  // wasm host mirror
                                          char buf[16];
                                          std::snprintf(buf, sizeof(buf), "%d", v);
                                          bpm_label_->set_text(buf); });
    root.add_child(std::move(bpm));

    auto bpm_val = std::make_unique<Label>();
    bpm_val->set_text("120");
    bpm_val->set_text_color(kCream);
    bpm_val->set_size(40 * u, 16 * u);
    bpm_val->set_position(bpm_x + 138 * u, 13 * u);
    bpm_val->set_font_size(8 * u);
    bpm_label_ = bpm_val.get();
    root.add_child(std::move(bpm_val));

    // --- row 2: the pattern strip ---
    static const char *const kLayerName[kLayers] = {"LEAD", "BASS"};
    for (int i = 0; i < kLayers; ++i)
    {
        auto b = std::make_unique<Button>();
        b->set_text(kLayerName[i]);
        b->set_size(44 * u, 24 * u);
        b->set_position((62 + i * 50) * u, 38 * u);
        b->set_font_size(7 * u);
        sub_layer_[i] = b->clicked.subscribe([this, i]
                                             { select_layer(i); });
        layer_btns_[i] = b.get();
        root.add_child(std::move(b));
    }
    static const char *const kPatName[kPatterns] = {"A", "B", "C", "D"};
    for (int i = 0; i < kPatterns; ++i)
    {
        auto b = std::make_unique<Button>();
        b->set_text(kPatName[i]);
        b->set_size(24 * u, 24 * u);
        b->set_position((w / u - 208 + i * 28) * u, 38 * u);
        b->set_font_size(8 * u);
        sub_pat_[i] = b->clicked.subscribe([this, i]
                                           {
                                               grid_->select_pattern(i);
                                               sync_strip(); });
        pat_btns_[i] = b.get();
        root.add_child(std::move(b));
    }
    auto chain = std::make_unique<Button>();
    chain->set_text("CHAIN");
    chain->set_size(42 * u, 24 * u);
    chain->set_position((w / u - 92) * u, 38 * u);
    chain->set_font_size(7 * u);
    sub_chain_ = chain->clicked.subscribe([this]
                                          {
                                              grid_->set_chain(!grid_->chain());
                                              sync_strip(); });
    chain_btn_ = chain.get();
    root.add_child(std::move(chain));

    auto copy = std::make_unique<Button>();
    copy->set_text("COPY");
    copy->set_size(38 * u, 24 * u);
    copy->set_position((w / u - 46) * u, 38 * u);
    copy->set_font_size(7 * u);
    sub_copy_ = copy->clicked.subscribe([this]
                                        {
                                            grid_->copy_to_next();
                                            sync_strip(); });
    copy_btn_ = copy.get();
    root.add_child(std::move(copy));

    // body split: the voice bay on the right, the board fills what is
    // left. Bay width caps at a third of the window so the grid keeps
    // room even at narrow hosts.
    const int bay_w = 140 * u > w / 3 ? w / 3 : 140 * u;
    const int bay_x = w - bay_w;

    // board geometry: whole-cell pitch from what fits beside the bay.
    // The grid's own full width is the label column + 16 columns + the
    // SL pill zone (+4px of frame); clamp the pitch so that width plus
    // a 10*u gap stays strictly left of the bay, under the two header
    // rows
    const int label_w = 34 * u;
    const int slide_w = clamp_i(14 * u, 14, 28);  // ~14px SL pill zone
    const int pw =
        (w - bay_w - 16 * u - label_w - slide_w - 4) / SeqGrid::kSteps;
    const int ph = (h - header_h - 26 * u) / (SeqGrid::kRows + 1);
    const int pitch_final = clamp_i(pw < ph ? pw : ph, 7, 34);

    auto grid = std::make_unique<SeqGrid>();
    const int gw = label_w + SeqGrid::kSteps * pitch_final + slide_w;
    // +pitch/2 mirrors the led_y shift in draw_at: the reserved LED
    // zone grows with the half-row of air above the strip
    const int gh = SeqGrid::kRows * pitch_final + 4 + 10 * u + pitch_final / 2;
    grid->set_size(gw, gh);
    grid->set_position((w - bay_w - gw) / 2 > 0 ? (w - bay_w - gw) / 2 : 0,
                       header_h + (h - header_h - 18 * u - gh > 0
                                       ? (h - header_h - 18 * u - gh) / 2
                                       : 0));
    grid->set_metrics(label_w, pitch_final, 10 * u,
                      clamp_i(7 * u, 8, 48));
    grid->set_slide_w(slide_w);
    grid_ = grid.get();
    // the board opens with a pattern already on it: press PLAY and it
    // makes music immediately (empty-board first paint reads as broken)
    grid_->dice();
    root.add_child(std::move(grid));

    /*
     * Voice bay, top to bottom: OUT label + VU dial (fed from the
     * grid's predicted output level), VOICE label + the 2x3 knob
     * cluster (edits the ACTIVE layer's page; the sixth knob is SWING
     * on LEAD and DRIVE on BASS), then the SCALE button. All stock
     * widgets in the amber palette.
     */
    const int pad = clamp_i(8 * u, 6, 20);
    const int inner_w = bay_w - 2 * pad;
    const int sec_h = clamp_i(12 * u, 12, 26);
    const int sec_font = clamp_i(6 * u, 8, 20);
    const int dial_s = clamp_i(34 * u, 44, 96);
    const int kn = clamp_i(20 * u, 26, 46);
    const int klab_h = clamp_i(9 * u, 10, 18);
    const int klab_font = clamp_i(5 * u, 8, 14);
    const int cell_w = inner_w / 2;
    const int cell_h = kn + klab_h + 2;
    const int btn_h = clamp_i(20 * u, 24, 44);
    const int gap = clamp_i(4 * u, 4, 12);
    int by = header_h + 2 * u;

    auto out_tag = std::make_unique<Label>();
    out_tag->set_text("OUT");
    out_tag->set_text_color(kDim);
    out_tag->set_size(inner_w, sec_h);
    out_tag->set_position(bay_x + pad, by);
    out_tag->set_h_align(Widget::h_align::center);
    out_tag->set_font_size(sec_font);
    out_tag->set_letter_spacing(2);
    root.add_child(std::move(out_tag));
    by += sec_h + gap / 2;

    auto vu = std::make_unique<GaugeDial>();
    vu->set_size(dial_s, dial_s);
    vu->set_position(bay_x + (bay_w - dial_s) / 2, by);
    // full warm set: amber arc/needle over the warm track tone on the
    // dark face (never the widget's cold defaults)
    vu->set_face_color(kFieldBg);
    vu->set_track_color(kLedBarBeat);
    vu->set_arc_color(kMelodyOn);
    vu->set_needle_color(kMelodyOn);
    vu->set_tick_color(kLedBarBeat);
    vu_ = vu.get();
    root.add_child(std::move(vu));
    by += dial_s + gap;

    auto voice_tag = std::make_unique<Label>();
    voice_tag->set_text("VOICE");
    voice_tag->set_text_color(kDim);
    voice_tag->set_size(inner_w, sec_h);
    voice_tag->set_position(bay_x + pad, by);
    voice_tag->set_h_align(Widget::h_align::center);
    voice_tag->set_font_size(sec_font);
    voice_tag->set_letter_spacing(2);
    root.add_child(std::move(voice_tag));
    by += sec_h + gap / 2;

    for (int i = 0; i < kKnobs; ++i)
    {
        const int kx = bay_x + pad + (i % 2) * cell_w;
        const int ky = by + (i / 2) * cell_h;
        auto knob = std::make_unique<Knob>();
        knob->set_range(0, 100);
        knob->set_value(knob_page_[0][i]);
        knob->set_size(kn, kn);
        knob->set_position(kx + (cell_w - kn) / 2, ky);
        knob->set_face_color(kFieldBg);
        knob->set_ring_color(kLedBarBeat);
        knob->set_pointer_color(kMelodyOn);
        // RAII: hold the subscription for the widget's lifetime; the
        // knob writes its page (lead 0..5 / bass 16..21 in g_params)
        sub_knob_[i] = knob->changed.subscribe(
            [this, i](const int v)
            {
                const int layer = grid_ ? grid_->layer() : 0;
                knob_page_[layer][i] = v;
                g_params[layer == 1 ? 16 + i : i] = v;
            });
        knobs_[i] = knob.get();
        root.add_child(std::move(knob));

        auto klab = std::make_unique<Label>();
        static const char *const kKnobName[kKnobs] = {"CUTOFF", "RESO", "DECAY",
                                                      "DELAY", "SWING", "MIX"};
        klab->set_text(kKnobName[i]);
        klab->set_text_color(kDim);
        klab->set_size(cell_w, klab_h);
        klab->set_position(kx, ky + kn);
        klab->set_h_align(Widget::h_align::center);
        klab->set_font_size(klab_font);
        knob_label_[i] = klab.get();
        root.add_child(std::move(klab));
    }
    by += 3 * cell_h + gap;

    auto scale_btn = std::make_unique<Button>();
    scale_btn->set_text("SCALE PENTA");
    scale_btn->set_text_color(kCream);
    scale_btn->set_size(inner_w, btn_h);
    scale_btn->set_position(bay_x + pad, by);
    scale_btn->set_font_size(sec_font);
    scale_btn->set_border_color(kMelodyOn);
    scale_btn_ = scale_btn.get();
    sub_scale_ = scale_btn->clicked.subscribe([this]
                                              { cycle_scale(); });
    root.add_child(std::move(scale_btn));

    auto hint = std::make_unique<Label>();
    hint->set_text("TAP CELLS TO COMPOSE - PRESS PLAY");
    hint->set_text_color(kDim);
    hint->set_size(260 * u, 14 * u);
    hint->set_position((w - bay_w - 260 * u) / 2 > 0 ? (w - bay_w - 260 * u) / 2 : 0,
                       h - 18 * u);
    hint->set_font_size(7 * u);
    root.add_child(std::move(hint));

    sync_play_button();
    sync_strip();
}
