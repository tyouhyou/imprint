#pragma once

#include "widget.hpp"

namespace zb::ui
{
    /*
     * Text label: a widget that renders text over an optional background.
     * All state lives in the Widget base class; this class is kept for
     * semantic clarity.
     */
    class Label : public Widget
    {
    public:
        Label() = default;
    };
}
