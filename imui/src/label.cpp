#include "label.hpp"

namespace zb::ui
{
    core::imsize_t Label::measure() const
    {
        if (get_text().empty())
        {
            return {0, 0};
        }
        return {text_advance(), text_height()};
    }
}  // namespace zb::ui