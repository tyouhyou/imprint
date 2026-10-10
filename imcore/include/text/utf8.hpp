#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace zb::ui
{
    /*
     * UTF-8 <-> UTF-16 helpers (see docs/code-contract.md section 2:
     * framework API text input is always UTF-8, widget text is stored as
     * UTF-16).
     *
     * decode_utf8_next(): decodes the sequence starting at *p, advances *p
     * past it and returns the code point. Invalid input bytes (truncated
     * sequences, overlong encodings, surrogates, out-of-range values)
     * produce U+FFFD and advance by one byte, so the input is never
     * consumed past its actual length.
     *
     * Two bounds, because a NUL-terminated string and a counted slice
     * need different ones:
     *   - decode_utf8_next(p) stops at the terminating NUL: a sequence
     *     truncated by the NUL is reported invalid (the NUL is not a
     *     continuation byte).
     *   - decode_utf8_next(p, end) decodes the half-open slice [p, end)
     *     and NEVER reads at or past `end` -- the mandatory form for text
     *     that is not NUL-terminated (packed design-file bytes, a
     *     `std::string_view` field). Reading a truncated sequence there
     *     used to run up to 3 bytes past the buffer.
     * `end == nullptr` selects the NUL-terminated behavior; `end <= p`
     * decodes nothing (returns U+FFFD without advancing) so a
     * `while (p < end)` loop cannot spin.
     */
    char32_t decode_utf8_next(const char *&p);
    char32_t decode_utf8_next(const char *&p, const char *end);

    /*
     * Converts an UTF-8 string to UTF-16. Conversion stops at the first
     * NUL (the C-string convention: an embedded NUL ends the text, it is
     * not preserved as a code unit). The counted overload converts the
     * whole slice instead, embedded NULs included -- the form for packed
     * bytes.
     */
    std::u16string utf8_to_utf16(const char *utf8);
    std::u16string utf8_to_utf16(const char *utf8, size_t len);

    /*
     * Converts an UTF-16 string back to UTF-8 (the string shape the
     * framework API hands out; widget text is stored as UTF-16). Lone
     * surrogates are emitted as U+FFFD, so the result is always valid
     * UTF-8.
     */
    std::string utf16_to_utf8(const std::u16string &u16);
}  // namespace zb::ui
