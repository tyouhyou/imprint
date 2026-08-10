#include "tictactoe_controller.hpp"

#include "ai.hpp"
#include "logging.hpp"
#include "tictactoe_view.hpp"

using namespace zb::app::tictactoe;

void TictactoeController::attach(TictactoeView &view)
{
    view_ = &view;
    view.difficulty_chosen = [this](const difficulty d) { choose_difficulty(d); };
    view.side_chosen = [this](const player_type p) { begin_round(p); };
    view.cell_clicked = [this](const int row, const int col) { on_cell_clicked(row, col); };
    view.again_requested = [this]() { show_round_setup(); };
    view.quit_requested = [this]() { quit_game(); };
}

void TictactoeController::start()
{
    show_round_setup();
}

void TictactoeController::show_round_setup()
{
    LI << "round setup: difficulty";
    view_->board().clear_marks();
    view_->open_difficulty_dialog();
}

void TictactoeController::show_side_dialog()
{
    LI << "round setup: side";
    view_->open_side_dialog();
}

void TictactoeController::choose_difficulty(const difficulty d)
{
    difficulty_ = d;
    LI << "difficulty: "
       << (d == difficulty::easy ? "easy" : d == difficulty::normal ? "normal" : "hard");
    show_side_dialog();
}

void TictactoeController::begin_round(const player_type human)
{
    LI << (human == player_type::x ? "round start: human X" : "round start: human O");
    game_->set_human(human);
    game_->start_round(player_type::x);  // X always opens
    view_->board().clear_marks();
    view_->close_all_dialogs();
    if (human != player_type::x)
    {
        computer_turn();
    }
}

void TictactoeController::on_cell_clicked(const int row, const int col)
{
    if (view_->dialog_open())
    {
        return;
    }
    if (game_->get_current_player() != game_->get_human())
    {
        return;  // only the human can click; the computer moves on its own
    }

    const auto p = game_->get_current_player();
    if (!game_->place_mark(row, col))
    {
        return;
    }
    LI << "human move: " << row << "," << col;
    view_->board().set_mark(row, col, p);
    finish_after_move();
    if (!view_->dialog_open())
    {
        computer_turn();  // the computer replies unless the game is over
    }
}

void TictactoeController::computer_turn()
{
    if (game_->get_current_player() != game_->get_computer())
    {
        return;
    }
    if (view_->dialog_open())
    {
        return;
    }

    const auto m = choose_move(*game_, game_->get_computer(), difficulty_);
    if (m.row < 0)
    {
        return;  // full board: the game-over flow already handled it
    }
    LI << "computer move: " << m.row << "," << m.col;
    view_->board().set_mark(m.row, m.col, game_->get_computer());
    game_->place_mark(m.row, m.col);
    finish_after_move();
}

void TictactoeController::finish_after_move()
{
    const auto winner = game_->check_winner();
    if (winner == player_type::none && !game_->is_full())
    {
        return;
    }

    result_msg_ = (winner == game_->get_human()) ? "YOU WIN"
                  : (winner == player_type::none) ? "DRAW"
                                                  : "COMPUTER WINS";
    LI << "game over: " << result_msg_;
    view_->open_result_dialog(result_msg_);
}

void TictactoeController::quit_game()
{
    LI << "quit";
    view_->window()->close();
}
