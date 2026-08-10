#ifndef IMEVENT_INPUT_DEF_HPP
#define IMEVENT_INPUT_DEF_HPP

#include "event.hpp"

namespace zb::event
{
    /*
     * Emitted when a frame has been fully rendered and is ready to be
     * presented. The single argument is the framebuffer pointer; the whole
     * frame is replaced (no partial updates).
     */
    using PAINT_EVENT = event::Event<const void *>;
    using CLOSE_EVENT = event::Event<>;
}

#endif // IMEVENT_INPUT_DEF_HPP