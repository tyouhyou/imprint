#include "utf8.hpp"

namespace zb::ui
{
    namespace
    {
        constexpr char32_t replacement = 0xFFFD;

        char32_t invalid(const char *&p)
        {
            ++p;
            return replacement;
        }

        // the one decoder; `end` is the exclusive read bound, or nullptr
        // for the NUL-terminated form. Never reads at or past `end`, and
        // never reads past a NUL in the NUL-terminated form (a NUL is not
        // a continuation byte, so a sequence truncated by it fails the
        // continuation test like any other invalid byte).
        char32_t decode_bounded(const char *&p, const char *end)
        {
            if (p == nullptr || (end != nullptr && p >= end))
            {
                return replacement;  // exhausted: nothing to consume
            }

            const auto c0 = static_cast<unsigned char>(*p);

            // 1-byte: ASCII (and the ASCII range only)
            if (c0 < 0x80)
            {
                ++p;
                return c0;
            }

            // count the continuation bytes promised by the leading byte
            int extra = 0;
            char32_t cp = 0;
            char32_t cp_min = 0;
            if ((c0 & 0xE0) == 0xC0)
            {
                extra = 1;
                cp = c0 & 0x1F;
                cp_min = 0x80;
            }
            else if ((c0 & 0xF0) == 0xE0)
            {
                extra = 2;
                cp = c0 & 0x0F;
                cp_min = 0x800;
            }
            else if ((c0 & 0xF8) == 0xF0)
            {
                extra = 3;
                cp = c0 & 0x07;
                cp_min = 0x10000;
            }
            else
            {
                return invalid(p);  // 0x80..0xBF stray continuation, 0xF8..0xFF invalid lead
            }

            const char *q = p + 1;
            for (int i = 0; i < extra; ++i)
            {
                // the slice may end inside the sequence: report the lead
                // byte invalid instead of reading past the buffer
                if (end != nullptr && q >= end)
                {
                    return invalid(p);
                }
                const auto cc = static_cast<unsigned char>(*q);
                if ((cc & 0xC0) != 0x80)
                {
                    return invalid(p);  // missing continuation: report the lead byte as invalid
                }
                cp = (cp << 6) | (cc & 0x3F);
                ++q;
            }

            if (cp < cp_min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            {
                return invalid(p);  // overlong encoding, out of range, or a surrogate
            }

            p = q;
            return cp;
        }
    }  // namespace

    char32_t decode_utf8_next(const char *&p)
    {
        return decode_bounded(p, nullptr);
    }

    char32_t decode_utf8_next(const char *&p, const char *end)
    {
        return decode_bounded(p, end);
    }

    char32_t decode_utf8_prev(const char *&p, const char *begin)
    {
        if (p == nullptr || p <= begin)
        {
            return replacement;  // exhausted: nothing to consume
        }

        // walk back over continuation bytes to the prospective lead
        const char *lead = p - 1;
        while (lead >= begin &&
               (static_cast<unsigned char>(*lead) & 0xC0) == 0x80)
        {
            --lead;
        }
        if (lead < begin)
        {
            p = p - 1;  // only continuation bytes behind: one stray byte
            return replacement;
        }

        const auto c0 = static_cast<unsigned char>(*lead);
        int extra = 0;
        char32_t cp = 0;
        char32_t cp_min = 0;
        if (c0 < 0x80)
        {
            cp = c0;
        }
        else if ((c0 & 0xE0) == 0xC0)
        {
            extra = 1;
            cp = c0 & 0x1F;
            cp_min = 0x80;
        }
        else if ((c0 & 0xF0) == 0xE0)
        {
            extra = 2;
            cp = c0 & 0x0F;
            cp_min = 0x800;
        }
        else if ((c0 & 0xF8) == 0xF0)
        {
            extra = 3;
            cp = c0 & 0x07;
            cp_min = 0x10000;
        }
        else
        {
            p = p - 1;  // a stray continuation (4+ in a row) or bad lead
            return replacement;
        }

        // the walk must have landed exactly one sequence back: a
        // shorter run is a truncated sequence, a longer one cannot
        // happen (the walk stops at the first non-continuation byte)
        if (static_cast<int>(p - lead) != extra + 1)
        {
            p = p - 1;
            return replacement;
        }

        for (int i = 1; i <= extra; ++i)
        {
            const auto cc = static_cast<unsigned char>(lead[i]);
            if ((cc & 0xC0) != 0x80)
            {
                p = p - 1;
                return replacement;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }

        if (cp < cp_min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            p = p - 1;  // overlong encoding, out of range, or a surrogate
            return replacement;
        }

        p = lead;
        return cp;
    }

    void append_utf8(std::string &out, const char32_t cp)
    {
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string sanitize_utf8(const char *utf8)
    {
        std::string out;
        if (utf8 == nullptr)
        {
            return out;
        }
        for (const char *p = utf8; *p != '\0';)
        {
            append_utf8(out, decode_bounded(p, nullptr));
        }
        return out;
    }

    std::string sanitize_utf8(const char *utf8, const size_t len)
    {
        std::string out;
        if (utf8 == nullptr || len == 0)
        {
            return out;
        }
        out.reserve(len);
        const char *const end = utf8 + len;
        for (const char *p = utf8; p < end;)
        {
            append_utf8(out, decode_bounded(p, end));
        }
        return out;
    }
}  // namespace zb::ui
