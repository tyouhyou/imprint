#include "tictactoe_view.hpp"

#include <stdexcept>

#include "logging.hpp"
#include "text/text_image.hpp"

using namespace zb::app::tictactoe;
using namespace zb::ui;

namespace
{
    // dialog geometry (fits the NDS 256x192 screen)
    constexpr int step_frame_w = 200;
    constexpr int step_frame_h = 64;      // title + one button row
    constexpr int result_frame_w = 200;
    constexpr int result_frame_h = 84;
    constexpr int dialog_padding = 10;
    constexpr int title_height = 16;
    constexpr int content_width = step_frame_w - 2 * dialog_padding;  // 180

    constexpr int diff_button_w = 56;
    constexpr int diff_button_h = 24;
    constexpr int side_button_w = 86;
    constexpr int side_button_h = 24;
    constexpr int result_button_w = 88;
    constexpr int result_button_h = 20;

    const core::Color frame_bg = core::Color::from(240, 240, 240);
    const core::Color title_fg = core::Color::from(40, 40, 40);
    const core::Color button_fg = core::Color::from(40, 40, 40);
    const core::Color button_bg = core::colors::White;
    const core::Color button_fg_pressed = core::colors::White;
    const core::Color button_bg_pressed = core::Color::from(150, 150, 150);
}  // namespace

void TictactoeView::build(const uint32_t &max_client_width, const uint32_t &max_client_height, void *buffer)
{
    if (window_)
    {
        throw std::runtime_error("gui already created");
    }

    auto win = zb::make_shared<CanvasWindow>();
    win->create(max_client_width, max_client_height, buffer);
    win->root().set_background_color(core::Color::from(28, 148, 64));
    window_ = win;  // children are added to the window's root below

    build_board(max_client_width, max_client_height);
    build_difficulty_dialog(max_client_width, max_client_height);
    build_side_dialog(max_client_width, max_client_height);
    build_result_dialog(max_client_width, max_client_height);

    rebuild_dialog_images();
    diff_dialog_->layout();
    side_dialog_->layout();
    result_dialog_->layout();
}

void TictactoeView::build_board(const uint32_t &max_client_width, const uint32_t &max_client_height)
{
    // the 3x3 grid is a square spanning the shorter window edge (with a
    // small margin), so it fits small screens (NDS: 256x192) as well as
    // desktop windows
    const int short_edge = (max_client_width < max_client_height)
                               ? static_cast<int>(max_client_width)
                               : static_cast<int>(max_client_height);
    const int margin = short_edge / 20;
    const int board_size = short_edge - 2 * margin;
    auto board = std::make_unique<Board>();
    board->set_size(board_size, board_size);
    board->set_position((static_cast<int>(max_client_width) - board_size) / 2,
                        (static_cast<int>(max_client_height) - board_size) / 2);
    board->set_background_color(core::Color::from(100, 190, 70));
    board->set_grid_color(core::colors::White);
    board->set_mark_color(core::colors::White);
    sub_cell_clicked_ = board->cell_clicked.subscribe([this](const int row, const int col)
    {
        if (cell_clicked)
        {
            cell_clicked(row, col);
        }
    });
    auto *pboard = board.get();
    window_->root().add_child(std::move(board));
    board_ = pboard;
}

void TictactoeView::build_difficulty_dialog(const uint32_t &max_client_width, const uint32_t &max_client_height)
{
    auto dlg = std::make_unique<Dialog>();
    dlg->set_size(max_client_width, max_client_height);
    dlg->set_frame_size(step_frame_w, step_frame_h);
    dlg->set_frame_padding(dialog_padding);
    dlg->set_frame_background_color(frame_bg);
    dlg->get_title().set_size(content_width, title_height);
    dlg->get_title().set_h_align(Widget::h_align::center);
    dlg->set_button_size(diff_button_w, diff_button_h);
    auto &easy = dlg->add_button("EASY");
    sub_easy_ = easy.clicked.subscribe([this]()
    {
        if (difficulty_chosen)
        {
            difficulty_chosen(difficulty::easy);
        }
    });
    auto &normal = dlg->add_button("NORMAL");
    sub_normal_ = normal.clicked.subscribe([this]()
    {
        if (difficulty_chosen)
        {
            difficulty_chosen(difficulty::normal);
        }
    });
    auto &hard = dlg->add_button("HARD");
    sub_hard_ = hard.clicked.subscribe([this]()
    {
        if (difficulty_chosen)
        {
            difficulty_chosen(difficulty::hard);
        }
    });
    btn_easy_ = &easy;
    btn_normal_ = &normal;
    btn_hard_ = &hard;

    dlg->close();
    auto *pdlg = dlg.get();
    window_->root().add_child(std::move(dlg));
    diff_dialog_ = pdlg;
}

void TictactoeView::build_side_dialog(const uint32_t &max_client_width, const uint32_t &max_client_height)
{
    auto dlg = std::make_unique<Dialog>();
    dlg->set_size(max_client_width, max_client_height);
    dlg->set_frame_size(step_frame_w, step_frame_h);
    dlg->set_frame_padding(dialog_padding);
    dlg->set_frame_background_color(frame_bg);
    dlg->get_title().set_size(content_width, title_height);
    dlg->get_title().set_h_align(Widget::h_align::center);
    dlg->set_button_size(side_button_w, side_button_h);
    auto &first = dlg->add_button("X FIRST");
    sub_first_ = first.clicked.subscribe([this]()
    {
        if (side_chosen)
        {
            side_chosen(player_type::x);
        }
    });
    auto &second = dlg->add_button("O SECOND");
    sub_second_ = second.clicked.subscribe([this]()
    {
        if (side_chosen)
        {
            side_chosen(player_type::o);
        }
    });
    btn_first_ = &first;
    btn_second_ = &second;

    dlg->close();
    auto *pdlg = dlg.get();
    window_->root().add_child(std::move(dlg));
    side_dialog_ = pdlg;
}

void TictactoeView::build_result_dialog(const uint32_t &max_client_width, const uint32_t &max_client_height)
{
    auto dlg = std::make_unique<Dialog>();
    dlg->set_size(max_client_width, max_client_height);
    dlg->set_frame_size(result_frame_w, result_frame_h);
    dlg->set_frame_padding(dialog_padding);
    dlg->set_frame_background_color(frame_bg);
    dlg->get_title().set_size(content_width, title_height);
    dlg->get_title().set_h_align(Widget::h_align::center);
    dlg->set_button_size(result_button_w, result_button_h);
    auto &again = dlg->add_button("AGAIN");
    sub_again_ = again.clicked.subscribe([this]()
    {
        if (again_requested)
        {
            again_requested();
        }
    });
    auto &quit = dlg->add_button("QUIT");
    sub_quit_ = quit.clicked.subscribe([this]()
    {
        if (quit_requested)
        {
            quit_requested();
        }
    });
    btn_again_ = &again;
    btn_quit_ = &quit;

    dlg->close();
    auto *pdlg = dlg.get();
    window_->root().add_child(std::move(dlg));
    result_dialog_ = pdlg;
}

bool TictactoeView::dialog_open() const noexcept
{
    return diff_dialog_ != nullptr && (diff_dialog_->is_open() || side_dialog_->is_open() || result_dialog_->is_open());
}

void TictactoeView::open_difficulty_dialog()
{
    result_dialog_->close();
    side_dialog_->close();
    diff_dialog_->open();
    window_->set_modal(diff_dialog_);
}

void TictactoeView::open_side_dialog()
{
    diff_dialog_->close();
    side_dialog_->open();
    window_->set_modal(side_dialog_);
}

void TictactoeView::open_result_dialog(const std::string &result_msg)
{
    result_msg_ = result_msg;
    rebuild_dialog_images();
    result_dialog_->open();
    window_->set_modal(result_dialog_);
}

void TictactoeView::close_all_dialogs()
{
    diff_dialog_->close();
    side_dialog_->close();
    result_dialog_->close();
    window_->set_modal(nullptr);
}

core::image_t TictactoeView::make_image(const char *text, const int w, const int h,
                                        const core::Color &fg, const core::Color &bg)
{
    auto g = zb::ui::make_text_image(text, w, h, fg, bg);
    const auto s = g->size();
    core::image_t img{g->data(), s.width, s.height, 0};
    images_.push_back(std::move(g));
    return img;
}

void TictactoeView::style_button(Button &b, const char *text)
{
    const auto s = b.get_size();
    b.set_background_image(make_image(text, s.width, s.height, button_fg, button_bg));
    b.set_pressed_image(make_image(text, s.width, s.height, button_fg_pressed, button_bg_pressed));
    // the label lives inside the images (make_text_image renders it), so drop
    // the widget text that Dialog::add_button set -- otherwise Button::draw_at
    // would paint the same string a second time (left/top aligned)
    b.set_text("");
}

void TictactoeView::rebuild_dialog_images()
{
    // rebuild every image: the old surfaces become unreferenced, so the
    // pool can be cleared safely
    images_.clear();
    diff_dialog_->get_title().set_background_image(make_image("DIFFICULTY", content_width, title_height, title_fg, frame_bg));
    style_button(*btn_easy_, "EASY");
    style_button(*btn_normal_, "NORMAL");
    style_button(*btn_hard_, "HARD");

    side_dialog_->get_title().set_background_image(make_image("WHO FIRST?", content_width, title_height, title_fg, frame_bg));
    style_button(*btn_first_, "X FIRST");
    style_button(*btn_second_, "O SECOND");

    result_dialog_->get_title().set_background_image(make_image(result_msg_.c_str(), content_width, title_height, title_fg, frame_bg));
    style_button(*btn_again_, "AGAIN");
    style_button(*btn_quit_, "QUIT");
}
