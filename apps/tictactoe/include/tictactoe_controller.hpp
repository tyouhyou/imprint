#ifndef TICTACTOE_CONTROLLER_HPP
#define TICTACTOE_CONTROLLER_HPP

#include <string>

#include "core/ptr.hpp"
#include "game.hpp"
#include "tictactoe_defines.hpp"

namespace zb::app::tictactoe
{
    class TictactoeView;

    /*
     * Round flow orchestration: the two-step round setup, the human /
     * computer move alternation and the game-over flow. All UI mutation
     * goes through the attached TictactoeView; this class holds the game
     * model and the per-round choice state, no widget state.
     */
    class TictactoeController
    {
    public:
        TictactoeController() = default;

        // installs the flow hooks into the view; call once after build()
        void attach(TictactoeView &view);

        // begins the per-round setup (difficulty dialog)
        void start();

        // flow entry points (invoked by the view's hooks)
        void choose_difficulty(const difficulty d);
        void begin_round(const player_type human);
        void on_cell_clicked(const int row, const int col);
        void quit_game();

        [[nodiscard]] Game &game() noexcept { return *game_; }

    private:
        void show_round_setup();
        void show_side_dialog();
        void computer_turn();
        void finish_after_move();

        TictactoeView *view_ = nullptr;
        zb::SharedPtr<Game> game_ = zb::make_shared<Game>();
        difficulty difficulty_ = difficulty::normal;
        std::string result_msg_ = "DRAW";
    };
}

#endif // TICTACTOE_CONTROLLER_HPP
