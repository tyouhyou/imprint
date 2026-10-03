#include "life.hpp"

#include <cstdio>

using namespace zb::app::life;
using namespace zb::ui;

namespace
{
    constexpr int kCanvasX = 32;
    constexpr int kCanvasY = 40;
    constexpr int kCanvasW = LifeCanvas::kCols * LifeCanvas::kScale;   // 256
    constexpr int kCanvasH = LifeCanvas::kRows * LifeCanvas::kScale;   // 192

    // sine-free plasma: two moving radial ramps + a diagonal wave, all in
    // fixed-point so the pixels are a pure function of the frame counter
    unsigned char plasma_at(const int x, const int y, const unsigned t)
    {
        const int cx = x - (kCanvasW / 2);
        const int cy = y - (kCanvasH / 2);
        const int r = cx * cx + cy * cy;
        const int w1 = (r >> 3) - static_cast<int>(t);
        const int w2 = ((x * 8 + y * 4) >> 3) + static_cast<int>(t) * 2;
        const int w3 = ((r >> 5) ^ (w2 >> 2)) + static_cast<int>(t >> 1);
        const int v = (w1 ^ w2 ^ w3) & 0xff;
        return static_cast<unsigned char>(v);
    }
}

LifeCanvas::LifeCanvas()
{
    set_size(kCanvasW, kCanvasH);
    set_background_color(core::colors::Black);

    for (int i = 0; i < 64; ++i)
    {
        // fixed seed starfield: pure LCG, no host randomness
        unsigned s = 1234u + static_cast<unsigned>(i) * 613u;
        s = s * 1103515245u + 12345u;
        stars_[i].x = static_cast<int>((s >> 8) % 1024u) - 512;
        s = s * 1103515245u + 12345u;
        stars_[i].y = static_cast<int>((s >> 8) % 1024u) - 512;
        s = s * 1103515245u + 12345u;
        stars_[i].z = static_cast<int>((s >> 8) % 512u) + 1;
    }
}

void LifeCanvas::set_mode(const int m)
{
    mode_ = m % 3;
    mark_dirty();
}

void LifeCanvas::toggle_cell(const int col, const int row)
{
    if (col >= 0 && col < kCols && row >= 0 && row < kRows)
    {
        cells_[row][col] = !cells_[row][col];
        mark_dirty();
    }
}

void LifeCanvas::clear_life()
{
    for (auto &row : cells_)
    {
        for (bool &c : row)
        {
            c = false;
        }
    }
    mark_dirty();
}

void LifeCanvas::stamp_glider_gun()
{
    clear_life();
    // Gosper glider gun (36x9) at a fixed offset
    static const char *kGun[9] = {
        "........................#...........",
        "......................#.#...........",
        "............##......##............#",
        "...........#...#....##............#",
        "##........#.....#...##.............",
        "##........#...#.##....#.#..........",
        "..........#.....#.......#..........",
        "...........#...#...................",
        "............##....................."
    };
    for (int r = 0; r < 9; ++r)
    {
        for (int c = 0; kGun[r][c] != 0; ++c)
        {
            if (kGun[r][c] == '#' && c < kCols && 4 + r < kRows)
            {
                cells_[4 + r][4 + c] = true;
            }
        }
    }
    mark_dirty();
}

void LifeCanvas::step_life()
{
    for (int r = 0; r < kRows; ++r)
    {
        for (int c = 0; c < kCols; ++c)
        {
            int n = 0;
            for (int dr = -1; dr <= 1; ++dr)
            {
                for (int dc = -1; dc <= 1; ++dc)
                {
                    if (dr == 0 && dc == 0)
                    {
                        continue;
                    }
                    // toroidal wrap keeps the border alive
                    const int nr = (r + dr + kRows) % kRows;
                    const int nc = (c + dc + kCols) % kCols;
                    n += cells_[nr][nc] ? 1 : 0;
                }
            }
            back_[r][c] = cells_[r][c] ? (n == 2 || n == 3) : (n == 3);
        }
    }
    for (int r = 0; r < kRows; ++r)
    {
        for (int c = 0; c < kCols; ++c)
        {
            cells_[r][c] = back_[r][c];
        }
    }
    mark_dirty();
}

void LifeCanvas::draw_at(core::Graphics &area) const
{
    switch (mode_)
    {
    case plasma:
        for (int y = 0; y < kCanvasH; ++y)
        {
            for (int x = 0; x < kCanvasW; ++x)
            {
                const unsigned char v = plasma_at(x, y, frame_ * 2u);
                // a two-hue ramp: blue-green with a warm crest
                const unsigned char r = v > 200 ? static_cast<unsigned char>(v - 200) : 0;
                const unsigned char g = v > 128 ? static_cast<unsigned char>(v - 128) : v / 2;
                area.draw_pixel(x, y, core::Color::from(r, g, v));
            }
        }
        break;
    case stars:
        area.fill(core::colors::Black);
        for (const star &s : stars_)
        {
            const int z = s.z - static_cast<int>(frame_ % 512u);
            const int zz = z <= 0 ? z + 512 : z;
            const int x = kCanvasW / 2 + s.x * 128 / zz;
            const int y = kCanvasH / 2 + s.y * 128 / zz;
            if (x < 0 || x >= kCanvasW || y < 0 || y >= kCanvasH)
            {
                continue;
            }
            const unsigned char b = static_cast<unsigned char>(255 - zz / 2);
            const int sz = zz < 128 ? 1 : 0;
            area.fill_rect(x, y, x + sz, y + sz,
                           core::Color::from(b, b, b));
        }
        break;
    default:
        // life: 4x4 cells on a dark grid
        for (int r = 0; r < kRows; ++r)
        {
            for (int c = 0; c < kCols; ++c)
            {
                if (!cells_[r][c])
                {
                    continue;
                }
                const int x = c * kScale;
                const int y = r * kScale;
                area.fill_rect(x, y, x + kScale - 1, y + kScale - 1,
                               core::Color::from(0x7F, 0xD4, 0x7F));
            }
        }
        break;
    }
}

void Life::create_window(const uint32_t max_client_width,
                         const uint32_t max_client_height, void *buffer)
{
    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    auto &root = window_->root();

    struct btn_spec
    {
        const char *text;
        int x;
        int w;
        Button **slot;
        void (Life::*handler)();
    };
    static const btn_spec kBtns[] = {
        {"PAUSE", 8, 52, &play_btn_, &Life::on_play},
        {"STEP", 64, 52, nullptr, &Life::on_step},
        {"CLEAR", 120, 52, nullptr, &Life::on_clear},
        {"GUN", 176, 44, nullptr, &Life::on_gun},
        {"MODE", 224, 44, nullptr, &Life::on_mode},
    };
    for (int i = 0; i < 5; ++i)
    {
        auto b = std::make_unique<Button>();
        b->set_text(kBtns[i].text);
        b->set_size(kBtns[i].w, 22);
        b->set_position(kBtns[i].x, 8);
        if (kBtns[i].slot)
        {
            *kBtns[i].slot = b.get();
        }
        const auto handler = kBtns[i].handler;
        subs_[i] = b->clicked.subscribe([this, handler] { (this->*handler)(); });
        root.add_child(std::move(b));
    }
    play_btn_->set_text("PAUSE");

    auto canvas = std::make_unique<LifeCanvas>();
    canvas->set_position(kCanvasX, kCanvasY);
    canvas_ = canvas.get();
    root.add_child(std::move(canvas));

    auto gen = std::make_unique<Label>();
    gen->set_size(64, 14);
    gen->set_position(kCanvasX + kCanvasW - 68, kCanvasY + kCanvasH - 16);
    gen->set_text("GEN 0");
    gen->set_text_color(core::colors::White);
    gen_ = gen.get();
    root.add_child(std::move(gen));

    canvas_->stamp_glider_gun();
}

void Life::on_play()
{
    playing_ = !playing_;
    play_btn_->set_text(playing_ ? "PAUSE" : "PLAY");
}

void Life::on_step()
{
    canvas_->step_life();
    ++generation_;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "GEN %u", generation_);
    gen_->set_text(buf);
}

void Life::on_clear()
{
    canvas_->clear_life();
    generation_ = 0;
    gen_->set_text("GEN 0");
}

void Life::on_gun()
{
    canvas_->stamp_glider_gun();
    generation_ = 0;
    gen_->set_text("GEN 0");
}

void Life::on_mode()
{
    // cycles life -> plasma -> stars; generation only makes sense for life
    mode_ = (mode_ + 1) % 3;
    canvas_->set_mode(mode_);
    gen_->set_text(mode_ == 0 ? "GEN" : mode_ == 1 ? "PLASMA" : "STARS");
}

void Life::input(const zb::input::input_event &ev) noexcept
{
    if (ev.type == zb::input::input_type::touch_down)
    {
        // translate a tap into a colony cell
        const int lx = ev.x - kCanvasX;
        const int ly = ev.y - kCanvasY;
        if (lx >= 0 && ly >= 0 && lx < kCanvasW && ly < kCanvasH)
        {
            canvas_->toggle_cell(lx / LifeCanvas::kScale, ly / LifeCanvas::kScale);
            return;
        }
    }
    if (window_)
    {
        window_->input(ev);
    }
}

void Life::paint() noexcept
{
    ++frames_;
    canvas_->set_frame(frames_);
    if (playing_)
    {
        if ((frames_ % kFramesPerStep) == 0)
        {
            canvas_->step_life();
            ++generation_;
            char buf[24];
            std::snprintf(buf, sizeof(buf), "GEN %u", generation_);
            gen_->set_text(buf);
        }
    }
    // the animation must own its repaint: without an invalidate the shell
    // stops after the tree settles and the demo freezes
    if (window_)
    {
        window_->invalidate();
        window_->paint();
    }
}
