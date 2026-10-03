#include "g2048.hpp"

#include <cstdio>
#include <cstring>

using namespace zb::app::g2048;
using namespace zb::ui;

namespace
{
    constexpr int kBoard = 140;          // 4 * 30 + 5 * 4
    constexpr int kBoardX = 58;
    constexpr int kBoardY = 44;

    core::Color tile_bg(const int v)
    {
        switch (v)
        {
        case 2:    return core::Color::from(0xEE, 0xE4, 0xDA);
        case 4:    return core::Color::from(0xED, 0xE0, 0xC8);
        case 8:    return core::Color::from(0xF2, 0xB1, 0x79);
        case 16:   return core::Color::from(0xF5, 0x95, 0x63);
        case 32:   return core::Color::from(0xF6, 0x7C, 0x5F);
        case 64:   return core::Color::from(0xF6, 0x5E, 0x3B);
        case 128:  return core::Color::from(0xED, 0xCF, 0x72);
        case 256:  return core::Color::from(0xED, 0xCC, 0x61);
        case 512:  return core::Color::from(0xED, 0xC8, 0x50);
        case 1024: return core::Color::from(0xED, 0xC5, 0x3F);
        case 2048: return core::Color::from(0xED, 0xC2, 0x2E);
        default:   return core::Color::from(0x3C, 0x3A, 0x32);
        }
    }

    core::Color tile_fg(const int v)
    {
        // the classic palette: low tiles carry dark digits, high ones white
        return (v >= 8) ? core::colors::White : core::Color::from(0x77, 0x6E, 0x65);
    }
}

Board2048::Board2048()
{
    set_size(kBoard, kBoard);
    set_background_color(core::Color::from(0xBB, 0xAD, 0xA0));

    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            auto tile = std::make_unique<Label>();
            tile->set_size(kCell, kCell);
            tile->set_position(kGap + c * (kCell + kGap), kGap + r * (kCell + kGap));
            tiles_[r][c] = tile.get();
            add_child(std::move(tile));
        }
    }
}

void Board2048::set_cell(const int row, const int col, const int v)
{
    Label &t = *tiles_[row][col];
    if (v == 0)
    {
        t.set_visible(false);
        return;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", v);
    t.set_text(buf);
    t.set_background_color(tile_bg(v));
    t.set_text_color(tile_fg(v));
    t.set_visible(true);
}

void Board2048::draw_at(core::Graphics &area) const
{
    // empty slots under the tile labels
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            const int x = kGap + c * (kCell + kGap);
            const int y = kGap + r * (kCell + kGap);
            area.fill_round_rect(x, y, x + kCell - 1, y + kCell - 1, 3,
                                 core::Color::from(0xCD, 0xC1, 0xB4));
        }
    }
    // Panel::draw_at draws the children -- an override that skips it
    // silences every child label (found by the browser test: the board
    // rendered, its tiles did not)
    Panel::draw_at(area);
}

void G2048::create_window(const uint32_t max_client_width,
                          const uint32_t max_client_height, void *buffer)
{
    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    auto &root = window_->root();

    auto score = std::make_unique<Label>();
    score->set_size(104, 20);
    score->set_position(8, 8);
    score->set_text("SCORE 0");
    score_label_ = score.get();
    root.add_child(std::move(score));

    auto best = std::make_unique<Label>();
    best->set_size(70, 20);
    best->set_position(112, 8);
    best->set_text("BEST 0");
    best_label_ = best.get();
    root.add_child(std::move(best));

    auto new_btn = std::make_unique<Button>();
    new_btn->set_text("NEW");
    new_btn->set_size(56, 22);
    new_btn->set_position(190, 7);
    new_btn->clicked += [this] { new_game(); };
    root.add_child(std::move(new_btn));

    auto board = std::make_unique<Board2048>();
    board->set_position(kBoardX, kBoardY);
    board_ = board.get();
    root.add_child(std::move(board));

    auto dlg = std::make_unique<Dialog>();
    dlg->set_size(kWidth, kHeight);
    dlg->set_frame_size(160, 92);
    dlg->get_title().set_size(140, 20);
    dlg->get_title().set_h_align(Widget::h_align::center);
    dlg->set_button_size(64, 24);
    auto &again = dlg->add_button("AGAIN");
    sub_again_ = again.clicked.subscribe([this] { new_game(); close_end_dialog(); });
    auto &cont = dlg->add_button("CONTINUE");
    sub_cont_ = cont.clicked.subscribe([this] { close_end_dialog(); });
    auto &quit = dlg->add_button("QUIT");
    sub_quit_ = quit.clicked.subscribe([this] { if (window_) window_->close(); });
    dlg->close();
    end_dialog_ = dlg.get();
    root.add_child(std::move(dlg));

    new_game();
}

int G2048::lcg_rand()
{
    seed_ = seed_ * 1103515245u + 12345u;
    return static_cast<int>((seed_ >> 16) & 0x7fffu);
}

void G2048::new_game()
{
    std::memset(grid_, 0, sizeof(grid_));
    score_ = 0;
    won_ = false;
    win_shown_ = false;
    over_ = false;
    seed_ = 20260101u;
    spawn_tile();
    spawn_tile();
    refresh();
}

void G2048::spawn_tile()
{
    int flat[16];
    int n = 0;
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            if (grid_[r][c] == 0)
            {
                flat[n++] = r * 4 + c;
            }
        }
    }
    if (n == 0)
    {
        return;
    }
    const int pick = flat[lcg_rand() % n];
    grid_[pick / 4][pick % 4] = (lcg_rand() % 10 == 0) ? 4 : 2;
}

bool G2048::move(const int dr, const int dc)
{
    if (over_)
    {
        return false;
    }
    bool moved = false;
    for (int i = 0; i < 4; ++i)
    {
        // read the line head-to-tail in the direction of travel
        int line[4];
        for (int j = 0; j < 4; ++j)
        {
            line[j] = grid_[dc != 0 ? i : (dr > 0 ? 3 - j : j)]
                          [dr != 0 ? i : (dc > 0 ? 3 - j : j)];
        }
        int out[4] = {};
        bool merged[4] = {};
        int w = 0;
        for (int j = 0; j < 4; ++j)
        {
            if (line[j] == 0)
            {
                continue;
            }
            if (w > 0 && out[w - 1] == line[j] && !merged[w - 1])
            {
                out[w - 1] *= 2;
                merged[w - 1] = true;
                score_ += out[w - 1];
                if (out[w - 1] >= 2048)
                {
                    won_ = true;
                }
            }
            else
            {
                out[w++] = line[j];
            }
        }
        for (int j = 0; j < 4; ++j)
        {
            const int r = dc != 0 ? i : (dr > 0 ? 3 - j : j);
            const int c = dr != 0 ? i : (dc > 0 ? 3 - j : j);
            if (grid_[r][c] != out[j])
            {
                moved = true;
            }
            grid_[r][c] = out[j];
        }
    }
    return moved;
}

bool G2048::can_move() const
{
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            if (grid_[r][c] == 0)
            {
                return true;
            }
            if (c < 3 && grid_[r][c] == grid_[r][c + 1])
            {
                return true;
            }
            if (r < 3 && grid_[r][c] == grid_[r + 1][c])
            {
                return true;
            }
        }
    }
    return false;
}

void G2048::refresh()
{
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            board_->set_cell(r, c, grid_[r][c]);
        }
    }
    if (score_ > best_)
    {
        best_ = score_;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "SCORE %d", score_);
    score_label_->set_text(buf);
    std::snprintf(buf, sizeof(buf), "BEST %d", best_);
    best_label_->set_text(buf);

    if (won_ && !win_shown_)
    {
        win_shown_ = true;
        show_end_dialog(true);
        return;
    }
    if (!can_move())
    {
        over_ = true;
        show_end_dialog(false);
    }
}

void G2048::show_end_dialog(const bool won)
{
    end_dialog_->get_title().set_text(won ? "YOU WIN!" : "GAME OVER");
    end_dialog_->open();
    window_->set_modal(end_dialog_);
}

void G2048::close_end_dialog()
{
    end_dialog_->close();
    window_->set_modal(nullptr);
}

void G2048::input(const zb::input::input_event &ev) noexcept
{
    const bool dialog_open = end_dialog_ && end_dialog_->is_open();

    if (ev.type == zb::input::input_type::key_down && !dialog_open)
    {
        int dr = 0;
        int dc = 0;
        if (ev.key == static_cast<int>(zb::input::key_code::up) || ev.ch == 'w')
        {
            dr = -1;
        }
        else if (ev.key == static_cast<int>(zb::input::key_code::down) || ev.ch == 's')
        {
            dr = 1;
        }
        else if (ev.key == static_cast<int>(zb::input::key_code::left) || ev.ch == 'a')
        {
            dc = -1;
        }
        else if (ev.key == static_cast<int>(zb::input::key_code::right) || ev.ch == 'd')
        {
            dc = 1;
        }
        if (dr != 0 || dc != 0)
        {
            if (move(dr, dc))
            {
                spawn_tile();
                refresh();
            }
            return;
        }
    }
    else if (ev.type == zb::input::input_type::touch_down)
    {
        down_x_ = ev.x;
        down_y_ = ev.y;
    }
    else if (ev.type == zb::input::input_type::touch_up && down_x_ >= 0 &&
             !dialog_open)
    {
        const int dx = ev.x - down_x_;
        const int dy = ev.y - down_y_;
        down_x_ = -1;
        const int adx = dx > 0 ? dx : -dx;
        const int ady = dy > 0 ? dy : -dy;
        if (adx >= 12 || ady >= 12)
        {
            const bool horizontal = adx >= ady;
            const bool moved = horizontal ? move(0, dx > 0 ? 1 : -1)
                                          : move(dy > 0 ? 1 : -1, 0);
            if (moved)
            {
                spawn_tile();
                refresh();
            }
            return;
        }
        // a tap falls through to the tree so buttons still work
    }
    if (window_)
    {
        window_->input(ev);
    }
}
