#include "label.hpp"

namespace zb::ui
{
    core::imsize_t Label::measure() const
    {
        // H-1: a wrapping label a layout assigned a box to reports that
        // width and the wrapped block height; before any width is known
        // (and for every non-wrapping label) the single-line demand wins
        // so a container seated on the measured width converges (the
        // block height is re-read on the next layout round)
        const int w = get_size().width;
        if (text_wrap() && w > 0)
        {
            return {w, wrapped_block_height(w)};
        }
        if (get_text().empty())
        {
            return {0, 0};
        }
        return {text_advance(), text_height()};
    }
}  // namespace zb::ui