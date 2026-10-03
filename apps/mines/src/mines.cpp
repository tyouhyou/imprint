#include "mines.hpp"

#include <cstdio>
#include <cstring>

using namespace zb::app::mines;
using namespace zb::ui;

namespace
{
    constexpr int kField = 9 * 14 + 10 * 1;  // 136: cells + gaps + frame
    constexpr int kBoardX = (256 - kField) / 2;
    constexpr int kBoardY = 30;

    core::Color number_fg(const int n)
    {
        switch (n)
        {
        case 1: return core::Color::from(0x20, 0x50, 0xC0);
        case 2: return core::Color::from(0x1B, 0x7A, 0x2E);
        case 3: return core::Color::from(0xC0, 0x30, 0x28);
        case 4: return core::Color::from(0x2A, 0x2A, 0x8A);
        case 5: return core::Color::from(0x7A, 0x20, 0x20);
        case 6: return core::Color::from(0x18, 0x6E, 0x6E);
        case 7: return core::Color::from(0x30, 0x30, 0x30);
        case 8: return core::Color::from(0x60, 0x60, 0x60);
        default: return core::colors::Black;
        }
    }
}

MinesBoard::MinesBoard()
{
    const int w = kCells * kCell + (kCells + 1) * kGap;
    set_size(w, w);
    set_background_color(core::Color::from(0x7B, 0x8A, 0x97));

    for (int r = 0; r < kCells; ++r)
    {
        for (int c = 0; c < kCells; ++c)
        {
            auto tile = std::make_unique<Label>();
            tile->set_size(kCell, kCell);
            tile->set_position(kGap + c * (kCell + kGap), kGap + r * (kCell + kGap));
            tiles_[r][c] = tile.get();
            add_child(std::move(tile));
        }
    }
}

void MinesBoard::set_cell(const int row, const int col, const int cell)
{
    state_[row][col] = cell;
    Label &t = *tiles_[row][col];
    char buf[4] = "";
    int fg = -1;   // -1: no text
    switch (cell)
    {
    case -2:  buf[0] = 'F'; break;                    // flag
    case -3:  buf[0] = '*'; break;                    // mine
    case -4:  buf[0] = 'X'; break;                    // wrong flag
    case -5:  buf[0] = '*'; break;                    // boom
    default:
        if (cell > 0)
        {
            buf[0] = static_cast<char>('0' + cell);
            fg = cell;
        }
        break;
    }

    if (buf[0] != 0)
    {
        t.set_text(buf);
        t.set_text_color(fg >= 0 ? number_fg(fg)
                                 : (cell == -2 || cell == -5 ? core::colors::Red
                                                             : core::colors::Black));
    }
    else
    {
        t.set_text("");
    }
}

void MinesBoard::draw_at(core::Graphics &area) const
{
    // slots under the labels: covered cells are raised, revealed are sunken
    for (int r = 0; r < kCells; ++r)
    {
        for (int c = 0; c < kCells; ++c)
        {
            const int x = kGap + c * (kCell + kGap);
            const int y = kGap + r * (kCell + kGap);
            // covered(-1) and the whole pre-first-click field read raised
            const bool covered = state_[r][c] == -1;
            area.fill_rect(x, y, x + kCell - 1, y + kCell - 1,
                           covered ? core::Color::from(0xAE, 0xC0, 0xCF)
                                   : core::Color::from(0xD6, 0xE2, 0xEB));
            area.draw_rect(x, y, x + kCell - 1, y + kCell - 1,
                           core::Color::from(0x6E, 0x7F, 0x8D));
        }
    }
    // Panel::draw_at draws the children (the number labels) -- an
    // override that skips it silences every child
    Panel::draw_at(area);
}

void Mines::create_window(const uint32_t max_client_width,
                          const uint32_t max_client_height, void *buffer)
{
    window_ = zb::make_shared<CanvasWindow>();
    window_->create(max_client_width, max_client_height, buffer);

    auto &root = window_->root();

    auto status = std::make_unique<Label>();
    status->set_size(110, 20);
    status->set_position(8, 6);
    status->set_text("MINES 10");
    status_ = status.get();
    root.add_child(std::move(status));

    auto flag_btn = std::make_unique<Button>();
    flag_btn->set_text("FLAG");
    flag_btn->set_size(52, 22);
    flag_btn->set_position(122, 5);
    flag_btn->clicked += [this]
    {
        flag_mode_ = !flag_mode_;
        flag_btn_->set_text(flag_mode_ ? "DIG" : "FLAG");
    };
    flag_btn_ = flag_btn.get();
    root.add_child(std::move(flag_btn));

    auto reset_btn = std::make_unique<Button>();
    reset_btn->set_text("NEW");
    reset_btn->set_size(52, 22);
    reset_btn->set_position(182, 5);
    reset_btn->clicked += [this] { new_game(); };
    root.add_child(std::move(reset_btn));

    auto board = std::make_unique<MinesBoard>();
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
    auto &quit = dlg->add_button("QUIT");
    sub_quit_ = quit.clicked.subscribe([this] { if (window_) window_->close(); });
    dlg->close();
    end_dialog_ = dlg.get();
    root.add_child(std::move(dlg));

    new_game();
}

int Mines::lcg_rand()
{
    seed_ = seed_ * 1103515245u + 12345u;
    return static_cast<int>((seed_ >> 16) & 0x7fffu);
}

void Mines::new_game()
{
    std::memset(mine_, 0, sizeof(mine_));
    std::memset(flagged_, 0, sizeof(flagged_));
    std::memset(adjacent_, 0, sizeof(adjacent_));
    for (int i = 0; i < kCells * kCells; ++i)
    {
        covered_[i] = true;
    }
    placed_ = false;
    state_ = playing;
    flags_ = 0;
    moves_ = 0;
    boom_ = -1;
    flag_mode_ = false;
    flag_btn_->set_text("FLAG");
    seed_ = 20260101u;
    refresh_board();
    refresh_status();
}

void Mines::place_mines(const int safe_cell)
{
    int placed = 0;
    while (placed < kMines)
    {
        const int cell = lcg_rand() % (kCells * kCells);
        if (mine_[cell] || cell == safe_cell)
        {
            continue;
        }
        mine_[cell] = true;
        ++placed;
    }
    for (int r = 0; r < kCells; ++r)
    {
        for (int c = 0; c < kCells; ++c)
        {
            int n = 0;
            for (int dr = -1; dr <= 1; ++dr)
            {
                for (int dc = -1; dc <= 1; ++dc)
                {
                    const int nr = r + dr;
                    const int nc = c + dc;
                    if ((dr != 0 || dc != 0) && nr >= 0 && nr < kCells &&
                        nc >= 0 && nc < kCells && mine_[nr * kCells + nc])
                    {
                        ++n;
                    }
                }
            }
            adjacent_[r * kCells + c] = n;
        }
    }
    placed_ = true;
}

void Mines::flood(const int cell)
{
    // iterative flood fill (contract: no unbounded recursion)
    int stack[kCells * kCells];
    int top = 0;
    stack[top++] = cell;
    while (top > 0)
    {
        const int cur = stack[--top];
        if (!covered_[cur] || flagged_[cur])
        {
            continue;
        }
        covered_[cur] = false;
        if (adjacent_[cur] != 0)
        {
            continue;
        }
        const int r = cur / kCells;
        const int c = cur % kCells;
        for (int dr = -1; dr <= 1; ++dr)
        {
            for (int dc = -1; dc <= 1; ++dc)
            {
                const int nr = r + dr;
                const int nc = c + dc;
                if ((dr != 0 || dc != 0) && nr >= 0 && nr < kCells &&
                    nc >= 0 && nc < kCells)
                {
                    const int next = nr * kCells + nc;
                    if (covered_[next] && !mine_[next] && !flagged_[next])
                    {
                        stack[top++] = next;
                    }
                }
            }
        }
    }
}

void Mines::reveal(const int cell)
{
    if (state_ != playing || flagged_[cell] || !covered_[cell])
    {
        return;
    }
    if (!placed_)
    {
        place_mines(cell);
    }
    ++moves_;
    if (mine_[cell])
    {
        boom_ = cell;
        state_ = lost;
        refresh_board();
        refresh_status();
        show_end_dialog();
        return;
    }
    flood(cell);
    refresh_board();
    refresh_status();
    check_win();
}

void Mines::toggle_flag(const int cell)
{
    if (state_ != playing || !covered_[cell])
    {
        return;
    }
    flagged_[cell] = !flagged_[cell];
    flags_ += flagged_[cell] ? 1 : -1;
    ++moves_;
    refresh_board();
    refresh_status();
}

void Mines::refresh_board()
{
    for (int r = 0; r < kCells; ++r)
    {
        for (int c = 0; c < kCells; ++c)
        {
            const int i = r * kCells + c;
            int cell = -1;
            if (!covered_[i])
            {
                cell = mine_[i] ? -3 : adjacent_[i];
            }
            else if (flagged_[i])
            {
                cell = (state_ == lost && !mine_[i]) ? -4 : -2;
            }
            if (state_ == lost && mine_[i] && !flagged_[i])
            {
                cell = (i == boom_) ? -5 : -3;
            }
            board_->set_cell(r, c, cell);
        }
    }
    // the board paints via the labels; a state-only change (covered slot
    // shading) still needs a damage report
    board_->mark_dirty();
}

void Mines::refresh_status()
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "MINES %d", kMines - flags_);
    status_->set_text(buf);
}

void Mines::check_win()
{
    for (int i = 0; i < kCells * kCells; ++i)
    {
        if (covered_[i] && !mine_[i])
        {
            return;
        }
    }
    state_ = won;
    show_end_dialog();
}

void Mines::show_end_dialog()
{
    end_dialog_->get_title().set_text(state_ == won ? "CLEARED!" : "BOOM!");
    end_dialog_->open();
    window_->set_modal(end_dialog_);
}

void Mines::close_end_dialog()
{
    end_dialog_->close();
    window_->set_modal(nullptr);
}

int Mines::cell_at(const int x, const int y) const
{
    // board absolute position == its layout position (a root child)
    const int lx = x - kBoardX;
    const int ly = y - kBoardY;
    const int w = kCells * kCell + (kCells + 1) * kGap;
    if (lx < 0 || ly < 0 || lx >= w || ly >= w)
    {
        return -1;
    }
    const int c = (lx - kGap) / (kCell + kGap);
    const int r = (ly - kGap) / (kCell + kGap);
    if (c < 0 || c >= kCells || r < 0 || r >= kCells)
    {
        return -1;
    }
    return r * kCells + c;
}

void Mines::input(const zb::input::input_event &ev) noexcept
{
    if (ev.type == zb::input::input_type::touch_down)
    {
        down_x_ = ev.x;
        down_y_ = ev.y;
    }
    else if (ev.type == zb::input::input_type::touch_up && down_x_ >= 0 &&
             ev.x == down_x_ && ev.y == down_y_ &&
             !(end_dialog_ && end_dialog_->is_open()))
    {
        down_x_ = -1;
        // only intercept taps inside the field; header buttons fall through
        const int cell = cell_at(ev.x, ev.y);
        if (cell >= 0)
        {
            if (flag_mode_)
            {
                toggle_flag(cell);
            }
            else
            {
                reveal(cell);
            }
            return;
        }
    }
    if (window_)
    {
        window_->input(ev);
    }
}
