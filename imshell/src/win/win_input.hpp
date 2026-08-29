#pragma once

#include <windows.h>

#include "input.hpp"

namespace zb::shell::win_input
{
    enum class result
    {
        not_handled,  // not an input message: the shell's own switch / DefWindowProc keeps it
        handled,      // translated: `out` carries the event to feed
        swallowed,    // an input message the framework deliberately drops
    };

    /*
     * A-2 InputSource: translates one window message into an
     * input_event. The key_code mapping (VK_* -> key_code), the WM_CHAR
     * character rules (printable ASCII except space; space keeps its
     * navigation key routing) and the wheel normalization (signed
     * notches, sub-notch deltas dropped) live here once, and the whole
     * mapping is dummy-driven unit-testable with synthetic messages.
     *
     * `wheel_client` is only read for WM_MOUSEWHEEL: the message's
     * lParam carries SCREEN coordinates, and converting them needs the
     * window handle (ScreenToClient). The shell converts first and
     * passes the result, keeping this function free of window state.
     */
    result translate(UINT msg, WPARAM wparam, LPARAM lparam, const POINT &wheel_client,
                     zb::input::input_event &out);

    /* the centralized VK_* -> key_code mapping (0 = unmapped) */
    int key_from_virtual_key(WPARAM vk);
}
