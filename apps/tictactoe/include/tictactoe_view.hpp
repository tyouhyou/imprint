#ifndef TICTACTOE_VIEW_HPP
#define TICTACTOE_VIEW_HPP

#include <functional>
#include <string>
#include <vector>

#include "board.hpp"
#include "canvas_window.hpp"
#include "event.hpp"
#include "imui.hpp"
#include "tictactoe_defines.hpp"

namespace zb::ui
{
    class Dialog;
    class Button;
}

namespace zb::app::tictactoe
{
    /*
     * Builds and owns the widget tree (the Board grid and the three modal
     * dialogs -- difficulty, side, result) together with their styling.
     * Contains no game logic: the user's choices and board clicks surface
     * through the flow hooks below, which the controller installs. Dialog
     * state changes used by the round flow are exposed as open_* methods.
     */
    class TictactoeView
    {
    public:
        TictactoeView() = default;

        // creates the window and the widget tree; throws if built twice
        void build(uint32_t max_client_width, uint32_t max_client_height, void *buffer = nullptr);

        [[nodiscard]] zb::SharedPtr<CanvasWindow> window() const noexcept { return window_; }
        [[nodiscard]] Board &board() const noexcept { return *board_; }

        // flow hooks (installed by TictactoeController::attach)
        std::function<void(difficulty)> difficulty_chosen;
        std::function<void(player_type)> side_chosen;
        std::function<void(const int, const int)> cell_clicked;
        std::function<void()> again_requested;
        std::function<void()> quit_requested;

        // whether any of the three dialogs is open
        [[nodiscard]] bool dialog_open() const noexcept;

        // dialog state changes (the controller composes the flow with these)
        void open_difficulty_dialog();   // closes the others, sets the modal
        void open_side_dialog();
        void open_result_dialog(const std::string &result_msg);  // re-renders the title
        void close_all_dialogs();

    private:
        void build_board(uint32_t max_client_width, uint32_t max_client_height);
        void build_difficulty_dialog(uint32_t max_client_width, uint32_t max_client_height);
        void build_side_dialog(uint32_t max_client_width, uint32_t max_client_height);
        void build_result_dialog(uint32_t max_client_width, uint32_t max_client_height);

        // blit text helpers (lifetime held by images_)
        zb::ui::core::image_t make_image(const char *text, const int w, const int h,
                                         const zb::ui::core::Color &fg, const zb::ui::core::Color &bg);
        void style_button(zb::ui::Button &b, const char *text);
        void rebuild_dialog_images();

        zb::SharedPtr<CanvasWindow> window_;
        Board *board_ = nullptr;

        // step 1 of the round setup: difficulty
        zb::ui::Dialog *diff_dialog_ = nullptr;
        zb::ui::Button *btn_easy_ = nullptr;
        zb::ui::Button *btn_normal_ = nullptr;
        zb::ui::Button *btn_hard_ = nullptr;
        // step 2 of the round setup: who moves first
        zb::ui::Dialog *side_dialog_ = nullptr;
        zb::ui::Button *btn_first_ = nullptr;
        zb::ui::Button *btn_second_ = nullptr;
        // game over
        zb::ui::Dialog *result_dialog_ = nullptr;
        zb::ui::Button *btn_again_ = nullptr;
        zb::ui::Button *btn_quit_ = nullptr;

        std::string result_msg_ = "DRAW";

        // keeps the generated text images alive while widgets reference them
        std::vector<zb::SharedPtr<zb::ui::core::Graphics>> images_;

        // RAII subscriptions: auto-unsubscribing when the view dies, even if
        // a widget was torn down first
        zb::event::Subscription<int, int> sub_cell_clicked_;
        zb::event::Subscription<> sub_easy_, sub_normal_, sub_hard_;
        zb::event::Subscription<> sub_first_, sub_second_;
        zb::event::Subscription<> sub_again_, sub_quit_;
    };
}

#endif // TICTACTOE_VIEW_HPP
