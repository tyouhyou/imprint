#ifndef IMINPUT_INPUT_HPP
#define IMINPUT_INPUT_HPP

namespace zb::input
{
    enum class mouse_button_t
    {
        none = 0,
        left,
        right,
        middle
    }; // enum class mouse_button_t

    enum class input_type
    {
        none = 0,

        // mouse
        mouse_left_down,
        mouse_left_up,
        mouse_left_click,
        mouse_right_down,
        mouse_right_up,
        mouse_right_click,
        mouse_wheel,
        mouse_move,

        // touch
        touch_down,
        touch_up,
        touch_move,

        // keyboard
        key_down,
        key_up,
    };

    /*
     * Keyboard key codes used by the `key` field of input_event.
     * ASCII codes are used verbatim; non-ASCII keys start at 256.
     */
    enum class key_code : int
    {
        backspace = 8,
        tab = 9,
        enter = 13,
        escape = 27,
        space = 32,
        del = 127,
        up = 256,
        down,
        left,
        right,
    };

    /*
     * Platform-independent input event. Plain old data, C-ABI friendly,
     * safe to pass across language boundaries (python / wasm later).
     *
     * Fields other than `type` are zero-initialized; which fields are
     * meaningful depends on `type`:
     *   - mouse_* (click / move) : x, y, button
     *   - touch_*                : x, y, touch_id (which finger)
     *   - mouse_wheel            : delta  (> 0 up / forward, < 0 down / back)
     *   - key_*                  : key (see key_code), ch (printable character
     *                              when the key produces one; 0 otherwise)
     *
     * `ch` holds a Unicode code point produced by a printable key (ASCII
     * 0x20..0x7E for now). Both fields are independent: a key may set only
     * `key` (navigation/editing keys), only `ch` (characters), or both.
     * The dispatcher routes `ch` to the focused widget; an unconsumed
     * character is dropped and never falls back to focus navigation.
     *
     * `touch_id` identifies a touch pointer (0 for mouse events and for
     * single-touch shells). The framework currently tracks one active
     * press, but the data model is multi-touch: a move/up from a different
     * touch_id never interferes with the active press.
     */
    struct input_event
    {
        input_type type = input_type::none;
        int x = 0;
        int y = 0;
        mouse_button_t button = mouse_button_t::none;
        int delta = 0;
        int key = 0;
        int touch_id = 0;
        int ch = 0;  // Unicode code point (0 = no character)
    };
} // namespace zb::input

#endif // IMINPUT_INPUT_HPP
