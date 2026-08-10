#pragma once

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
     */
    char32_t decode_utf8_next(const char *&p);

    /*
     * Converts an UTF-8 string (null-terminated) to UTF-16. Embedded NUL
     * bytes are preserved as code units. A non-null-terminated slice can
     * be converted by decoding in a loop with decode_utf8_next().
     */
    std::u16string utf8_to_utf16(const char *utf8);
}  // namespace zb::ui
