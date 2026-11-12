#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace zb::ui
{
    /*
     * UTF-8 decode/encode helpers (docs/code-contract.md section 2.1
     * owns the storage ruling: framework API text input is always
     * UTF-8; widget text is stored as UTF-8 bytes).
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
     * The backward mirror of decode_utf8_next: steps *p back to the
     * start of the sequence that ENDS at *p and returns its code
     * point. Counted form only -- `begin` is the exclusive backward
     * bound the walk never crosses (a backward walk has no NUL
     * convention). p <= begin (or nullptr) decodes nothing and
     * returns U+FFFD without moving. An invalid or truncated sequence
     * is one U+FFFD of its own: *p steps back exactly one byte, the
     * backward counterpart of the forward decoder's one-byte advance.
     * On sanitized storage (code-contract §2.1 -- the only in-framework
     * input) the walk always lands on a real boundary.
     */
    char32_t decode_utf8_prev(const char *&p, const char *begin);

    /*
     * The sanitize pass for stored text (code-contract §2.1): decodes
     * and re-encodes, so the result is always valid UTF-8 — an
     * invalid or truncated byte becomes one U+FFFD. The NUL-terminated
     * form stops at the first NUL like every decode entry point; the
     * counted form preserves embedded NULs (the form for packed
     * bytes).
     */
    std::string sanitize_utf8(const char *utf8);
    std::string sanitize_utf8(const char *utf8, size_t len);

    /* appends one code point as its UTF-8 sequence (1-4 bytes) — the
     * editor insert path and any single-character builder */
    void append_utf8(std::string &out, char32_t cp);

    /* the nearest code-point boundary at or after `pos`: an index in
     * the middle of a multi-byte sequence snaps forward to the
     * sequence's end (code-contract §2.1 — no caret, anchor, run edge
     * or span boundary sits inside a sequence). On sanitized text the
     * walk is at most 3 bytes. */
    inline size_t snap_boundary(const std::string &text, const size_t pos)
    {
        size_t p = pos <= text.size() ? pos : text.size();
        while (p < text.size() &&
               (static_cast<unsigned char>(text[p]) & 0xC0) == 0x80)
        {
            ++p;
        }
        return p;
    }
}  // namespace zb::ui
