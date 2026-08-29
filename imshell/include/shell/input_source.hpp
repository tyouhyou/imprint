#pragma once

#include "input.hpp"

namespace zb::app
{
    class IApp;
}

namespace zb::shell
{
    /*
     * Input seam (A-2), shared tail: feeds one translated input_event
     * into the app and repaints when a frame is owed. Apps may consume
     * an event as an app-level command without routing it through
     * CanvasWindow::input, and their widget changes must still get
     * presented -- so the repaint check belongs to this step, not to
     * the caller. Every event-driven shell (win / x11 / mac) feeds its
     * translated events through here; idle-polling shells (fb / NDS)
     * have no input source at all and only use the paint loop.
     */
    void feed_input(zb::app::IApp &app, const zb::input::input_event &ev);
}
