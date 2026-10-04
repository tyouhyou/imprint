#include "mines.hpp"

#include <cstdio>
#include <cstring>

using namespace zb::app::mines;
using namespace zb::ui;

namespace
{
    // field size for a given cell count: cells + gaps + closing frame
    constexpr int field_px(const int cells, const int cell, const int gap)
    {
        return cells * cell + (cells + 1) * gap;
    }

    constexpr int clamp_i(const int v, const int lo, const int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

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

MinesBoard::MinesBoard(const int cols, const int rows, const int cell, const int gap)
    : cols_(cols), rows_(rows), cell_(cell), gap_(gap)
{
    set_size(field_px(cols, cell, gap), field_px(rows, cell, gap));
    set_background_color(core::Color::from(0x7B, 0x8A, 0x97));

    for (int r = 0; r < rows_; ++r)
    {
        for (int c = 0; c < cols_; ++c)
        {
            auto tile = std::make_unique<Label>();
            tile->set_size(cell_, cell_);
            tile->set_position(gap_ + c * (cell_ + gap_), gap_ + r * (cell_ + gap_));
            // a number sits dead center of its cell: without the
            // alignment the text rides the top-left (and the TTF line
            // box overflows a 14px cell)
            tile->set_h_align(Widget::h_align::center);
            tile->set_v_align(Widget::v_align::center);
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
    for (int r = 0; r < rows_; ++r)
    {
        for (int c = 0; c < cols_; ++c)
        {
            const int x = gap_ + c * (cell_ + gap_);
            const int y = gap_ + r * (cell_ + gap_);
            // covered(-1) and the whole pre-first-click field read raised
            const bool covered = state_[r][c] == -1;
            area.fill_rect(x, y, x + cell_ - 1, y + cell_ - 1,
                           covered ? core::Color::from(0xAE, 0xC0, 0xCF)
                                   : core::Color::from(0xD6, 0xE2, 0xEB));
            area.draw_rect(x, y, x + cell_ - 1, y + cell_ - 1,
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

    const int w = static_cast<int>(max_client_width);
    const int h = static_cast<int>(max_client_height);

    // Field geometry, decided once from the host buffer size. The classic
    // tier (kWidth x kHeight) must come out as the 9x9 / 14px field; both
    // derivations scale the design-minimum ratios (integer math, no
    // floats), taking the smaller of the width and height ratios.
    const int sw = w * 192; // width ratio = w / 256 == w * 192 / 480
    const int sh = h * 256; // height ratio = h / 192 == h * 256 / 480
    const int num = sw <= sh ? w : h;          // numerator of min ratio
    const int den = sw <= sh ? 256 : 192;      // denominator of min ratio
    cell_ = clamp_i((kCell * num + den / 2) / den, kCell, kMaxCellPx);
    int cells = clamp_i((kMinCells * num + den / 2) / den, kMinCells, kMaxCells);
    // the square field must also fit the vertical budget under the header
    const int vfit = (h - kHeaderH - 8) / (cell_ + kGap);
    cells = clamp_i(cells, kMinCells, clamp_i(vfit, kMinCells, kMaxCells));
    cols_ = cells;
    rows_ = cells;
    gap_ = kGap;
    mines_ = clamp_i(cols_ * rows_ * 12 / 100, 10, 99);

    const int field = field_px(cols_, cell_, gap_);
    board_x_ = (w - field) / 2;
    board_y_ = kHeaderH;

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
    flag_btn->set_position(w - 134, 5);
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
    reset_btn->set_position(w - 74, 5);
    reset_btn->clicked += [this] { new_game(); };
    root.add_child(std::move(reset_btn));

    auto board = std::make_unique<MinesBoard>(cols_, rows_, cell_, gap_);
    board->set_position(board_x_, board_y_);
    board_ = board.get();
    root.add_child(std::move(board));

    auto dlg = std::make_unique<Dialog>();
    dlg->set_size(max_client_width, max_client_height);
    const int frame_w = clamp_i(w / 2, 160, 300);
    dlg->set_frame_size(frame_w, 92);
    dlg->get_title().set_size(frame_w - 20, 20);
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
    for (int i = 0; i < cols_ * rows_; ++i)
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
    while (placed < mines_)
    {
        const int cell = lcg_rand() % (cols_ * rows_);
        if (mine_[cell] || cell == safe_cell)
        {
            continue;
        }
        mine_[cell] = true;
        ++placed;
    }
    for (int r = 0; r < rows_; ++r)
    {
        for (int c = 0; c < cols_; ++c)
        {
            int n = 0;
            for (int dr = -1; dr <= 1; ++dr)
            {
                for (int dc = -1; dc <= 1; ++dc)
                {
                    const int nr = r + dr;
                    const int nc = c + dc;
                    if ((dr != 0 || dc != 0) && nr >= 0 && nr < rows_ &&
                        nc >= 0 && nc < cols_ && mine_[nr * cols_ + nc])
                    {
                        ++n;
                    }
                }
            }
            adjacent_[r * cols_ + c] = n;
        }
    }
    placed_ = true;
}

void Mines::flood(const int cell)
{
    // iterative flood fill (contract: no unbounded recursion)
    int stack[kMaxCells * kMaxCells];
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
        const int r = cur / cols_;
        const int c = cur % cols_;
        for (int dr = -1; dr <= 1; ++dr)
        {
            for (int dc = -1; dc <= 1; ++dc)
            {
                const int nr = r + dr;
                const int nc = c + dc;
                if ((dr != 0 || dc != 0) && nr >= 0 && nr < rows_ &&
                    nc >= 0 && nc < cols_)
                {
                    const int next = nr * cols_ + nc;
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
    for (int r = 0; r < rows_; ++r)
    {
        for (int c = 0; c < cols_; ++c)
        {
            const int i = r * cols_ + c;
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
    std::snprintf(buf, sizeof(buf), "MINES %d", mines_ - flags_);
    status_->set_text(buf);
}

void Mines::check_win()
{
    for (int i = 0; i < cols_ * rows_; ++i)
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
    const int lx = x - board_x_;
    const int ly = y - board_y_;
    const int fw = field_px(cols_, cell_, gap_);
    const int fh = field_px(rows_, cell_, gap_);
    if (lx < 0 || ly < 0 || lx >= fw || ly >= fh)
    {
        return -1;
    }
    const int c = (lx - gap_) / (cell_ + gap_);
    const int r = (ly - gap_) / (cell_ + gap_);
    if (c < 0 || c >= cols_ || r < 0 || r >= rows_)
    {
        return -1;
    }
    return r * cols_ + c;
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
