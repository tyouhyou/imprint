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
    }  // namespace

    char32_t decode_utf8_next(const char *&p)
    {
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

    std::u16string utf8_to_utf16(const char *utf8)
    {
        std::u16string out;
        if (utf8 == nullptr)
        {
            return out;
        }
        for (const char *p = utf8; *p != '\0';)
        {
            const char32_t cp = decode_utf8_next(p);
            if (cp > 0xFFFF)
            {
                // supplementary plane: surrogate pair
                const char32_t v = cp - 0x10000;
                out += static_cast<char16_t>(0xD800 + (v >> 10));
                out += static_cast<char16_t>(0xDC00 + (v & 0x3FF));
            }
            else
            {
                out += static_cast<char16_t>(cp);
            }
        }
        return out;
    }

    std::string utf16_to_utf8(const std::u16string &u16)
    {
        std::string out;
        out.reserve(u16.size());
        for (size_t i = 0; i < u16.size(); ++i)
        {
            char32_t cp = u16[i];
            if (cp >= 0xD800 && cp <= 0xDBFF)
            {
                // lead surrogate: consume the trailing one when present
                const char32_t hi = cp - 0xD800;
                if (i + 1 < u16.size() && u16[i + 1] >= 0xDC00 && u16[i + 1] <= 0xDFFF)
                {
                    cp = 0x10000 + (hi << 10) + (u16[i + 1] - 0xDC00);
                    ++i;
                }
                else
                {
                    // lone lead surrogate: invalid, substitute
                    cp = 0xFFFD;
                }
            }
            else if (cp >= 0xDC00 && cp <= 0xDFFF)
            {
                cp = 0xFFFD;  // lone trailing surrogate
            }

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
        return out;
    }
}  // namespace zb::ui
