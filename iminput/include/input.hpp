#ifndef IMINPUT_INPUT_HPP
#define IMINPUT_INPUT_HPP

#include <type_traits>

namespace zb::input
{
    enum class mouse_button_t
    {
        none = 0,
        left,
        right,
        middle
    };  // enum class mouse_button_t

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

        // pure relative motion (U-3): dx/dy carry the displacement,
        // x/y are untouched; the dispatcher is inert to it — the event
        // is a carrier for library-mode hosts (ARCHITECTURE §4.2)
        pointer_delta,
    };

    /*
     * Keyboard key codes used by the `key` field of input_event.
     * ASCII codes are used verbatim: the control keys below, and every
     * mapped printable key as its lowercase/verbatim ASCII code
     * (letters `a`=97..`z`=122, digits 48..57) — carried on key_down
     * AND key_up so a host can pair the edges (U-1, ARCHITECTURE §4.2).
     * Non-ASCII keys start at 256.
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
     *   - key_*                  : key (see key_code — mapped printable
     *                              keys carry their lowercase/verbatim ASCII
     *                              code on both edges, U-1), ch (the
     *                              printable character on key_down when the
     *                              key produces one; always 0 on key_up)
     *   - pointer_delta          : dx, dy (host-provided relative
     *                              displacement, U-3; x/y untouched)
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
        int dx = 0;  // relative motion (pointer_delta, U-3)
        int dy = 0;
    };

    /*
     * U-1 decoration: the printable-character half of the rule every
     * input translator applies (win/x11/mac/terminal, plus the wasm and
     * python hosts). Given the printable ASCII a platform reported, fill
     * the event's `key` — lowercase for letters, verbatim for digits —
     * and set `ch` on key_down only (key_up always carries ch == 0).
     * Punctuation and everything outside printable ASCII map to no key
     * (key stays whatever the translator's navigation table set, which
     * is why the rule is "fill if it is a letter/digit", not "assign").
     *
     * Space is deliberately excluded: it is a navigation key
     * (key_code::space), never a character (B1). Returns true when the
     * character was one of the mapped classes.
     */
    inline bool decorate_printable_key(input_event &ev, const int c)
    {
        if (c < 0x20 || c > 0x7e || c == ' ')
        {
            return false;
        }
        if (ev.type == input_type::key_down)
        {
            ev.ch = c;
        }
        if (c >= 'A' && c <= 'Z')
        {
            ev.key = c + 0x20;  // lowercase platform-independent code
        }
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            ev.key = c;
        }
        else
        {
            return false;  // punctuation: ch is set, key is not
        }
        return true;
    }

    // the C-ABI copies this struct verbatim: trivially copyable +
    // standard layout is the loadable guarantee. The default member
    // initializers make the default constructor non-trivial, so the
    // type is deliberately not a strict-C++ POD (ARCHITECTURE §4.2).
    static_assert(std::is_trivially_copyable_v<input_event> &&
                      std::is_standard_layout_v<input_event>,
                  "input_event must stay memcpy-safe across the C-ABI");
}  // namespace zb::input

#endif  // IMINPUT_INPUT_HPP
