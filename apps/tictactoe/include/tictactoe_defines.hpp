#ifndef TICTACTOE_DEFINES_HPP
#define TICTACTOE_DEFINES_HPP

namespace zb::app::tictactoe
{
    enum class player_type : int
    {
        none = 0,
        o,
        x
    };

    // computer opponent strength, chosen at the start of each round
    enum class difficulty : int
    {
        easy = 0,   // random legal moves
        normal,     // win > block > center > corner > side
        hard        // minimax: never loses
    };
}

#endif // !TICTACTOE_DEFINES_HPP
