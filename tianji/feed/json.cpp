#include "json.hpp"

#include <cmath>
#include <cstdlib>

#include "logging.hpp"

namespace zb::tianji
{
    namespace
    {
        // untrusted-input hardening: the sibling parsers cap recursion
        // (warn-once + drop); 32 nesting levels is far past any v1
        // message shape (the deepest documented field is 3 levels)
        constexpr int kMaxDepth = 32;

        void skip_ws(const char *&p, const char *end)
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                               *p == '\r'))
            {
                ++p;
            }
        }

        void append_utf8(std::string &out, const char32_t cp)
        {
            if (cp < 0x80)
            {
                out.push_back(static_cast<char>(cp));
            }
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        int hex4(const char *&p, const char *end)
        {
            int v = 0;
            for (int i = 0; i < 4; ++i)
            {
                if (p >= end)
                {
                    return -1;
                }
                const char c = *p++;
                v <<= 4;
                if (c >= '0' && c <= '9')
                {
                    v |= c - '0';
                }
                else if (c >= 'a' && c <= 'f')
                {
                    v |= c - 'a' + 10;
                }
                else if (c >= 'A' && c <= 'F')
                {
                    v |= c - 'A' + 10;
                }
                else
                {
                    return -1;
                }
            }
            return v;
        }

        bool parse_string_body(const char *&p, const char *end, std::string &out)
        {
            // `p` stands on the opening quote; consumes through the
            // closing one. Raw control bytes (< 0x20) are rejected —
            // the wire is JSON lines, a raw newline is a framing error
            out.clear();
            if (p >= end || *p != '"')
            {
                return false;
            }
            ++p;
            while (p < end)
            {
                const unsigned char c = static_cast<unsigned char>(*p);
                if (c == '"')
                {
                    ++p;
                    return true;
                }
                if (c == '\\')
                {
                    ++p;
                    if (p >= end)
                    {
                        return false;
                    }
                    switch (*p)
                    {
                    case '"':
                        out.push_back('"');
                        ++p;
                        break;
                    case '\\':
                        out.push_back('\\');
                        ++p;
                        break;
                    case '/':
                        out.push_back('/');
                        ++p;
                        break;
                    case 'b':
                        out.push_back('\b');
                        ++p;
                        break;
                    case 'f':
                        out.push_back('\f');
                        ++p;
                        break;
                    case 'n':
                        out.push_back('\n');
                        ++p;
                        break;
                    case 'r':
                        out.push_back('\r');
                        ++p;
                        break;
                    case 't':
                        out.push_back('\t');
                        ++p;
                        break;
                    case 'u':
                    {
                        ++p;
                        const int hi = hex4(p, end);
                        if (hi < 0)
                        {
                            return false;
                        }
                        if (hi >= 0xD800 && hi <= 0xDBFF && p + 1 < end &&
                            p[0] == '\\' && p[1] == 'u')
                        {
                            const char *save = p;
                            p += 2;
                            const int lo = hex4(p, end);
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                            {
                                append_utf8(out, 0x10000 +
                                                     ((static_cast<char32_t>(hi) -
                                                       0xD800)
                                                      << 10) +
                                                     (static_cast<char32_t>(lo) -
                                                      0xDC00));
                            }
                            else
                            {
                                p = save;
                                append_utf8(out, 0xFFFD);  // lone lead
                            }
                        }
                        else if (hi >= 0xD800 && hi <= 0xDFFF)
                        {
                            append_utf8(out, 0xFFFD);  // lone half
                        }
                        else
                        {
                            append_utf8(out, static_cast<char32_t>(hi));
                        }
                        break;
                    }
                    default:
                        return false;
                    }
                    continue;
                }
                if (c < 0x20)
                {
                    return false;  // raw control byte: framing error
                }
                out.push_back(static_cast<char>(c));
                ++p;
            }
            return false;  // unterminated
        }

        bool parse_number(const char *&p, const char *end, double &out)
        {
            // strtod on a NUL-terminated copy would scan past `end`;
            // the v1 numbers are small integers — a bounded scan
            const char *start = p;
            if (p < end && *p == '-')
            {
                ++p;
            }
            bool digits = false;
            while (p < end && *p >= '0' && *p <= '9')
            {
                digits = true;
                ++p;
            }
            if (p < end && *p == '.')
            {
                ++p;
                while (p < end && *p >= '0' && *p <= '9')
                {
                    digits = true;
                    ++p;
                }
            }
            if (!digits)
            {
                p = start;
                return false;
            }
            // exponent part
            if (p < end && (*p == 'e' || *p == 'E'))
            {
                const char *save = p;
                ++p;
                if (p < end && (*p == '+' || *p == '-'))
                {
                    ++p;
                }
                if (p < end && *p >= '0' && *p <= '9')
                {
                    while (p < end && *p >= '0' && *p <= '9')
                    {
                        ++p;
                    }
                }
                else
                {
                    p = save;  // no exponent digits: rewind, number ends
                }
            }
            char buf[64];
            const size_t n = static_cast<size_t>(p - start);
            if (n >= sizeof(buf))
            {
                return false;  // degenerate numeric literal
            }
            for (size_t i = 0; i < n; ++i)
            {
                buf[i] = start[i];
            }
            buf[n] = '\0';
            out = std::strtod(buf, nullptr);
            return true;
        }
    }  // namespace

    Json Json::boolean(const bool v)
    {
        Json j;
        j.type_ = Type::Bool;
        j.bool_ = v;
        return j;
    }

    Json Json::number(const double v)
    {
        Json j;
        j.type_ = Type::Number;
        j.num_ = v;
        return j;
    }

    Json Json::str(std::string v)
    {
        Json j;
        j.type_ = Type::String;
        j.str_ = std::move(v);
        return j;
    }

    Json Json::array()
    {
        Json j;
        j.type_ = Type::Array;
        return j;
    }

    Json Json::object()
    {
        Json j;
        j.type_ = Type::Object;
        return j;
    }

    bool Json::as_bool(const bool fallback) const
    {
        return type_ == Type::Bool ? bool_ : fallback;
    }

    long long Json::as_int(const long long fallback) const
    {
        if (type_ != Type::Number || !std::isfinite(num_))
        {
            return fallback;
        }
        // clamp into long long (the unchecked-narrowing rule)
        if (num_ >= 9.2233720368547758e18)
        {
            return fallback;
        }
        if (num_ <= -9.2233720368547758e18)
        {
            return fallback;
        }
        return static_cast<long long>(num_ + (num_ >= 0 ? 0.5 : -0.5));
    }

    double Json::as_number(const double fallback) const
    {
        return type_ == Type::Number ? num_ : fallback;
    }

    const Json *Json::find(const char *key) const
    {
        if (type_ != Type::Object)
        {
            return nullptr;
        }
        for (const auto &m : obj_)
        {
            if (m.first == key)
            {
                return &m.second;
            }
        }
        return nullptr;
    }

    std::string Json::get_str(const char *key, const char *fallback) const
    {
        const Json *v = find(key);
        return v != nullptr && v->type_ == Type::String ? v->str_
                                                        : std::string(fallback);
    }

    long long Json::get_int(const char *key, const long long fallback) const
    {
        const Json *v = find(key);
        return v != nullptr ? v->as_int(fallback) : fallback;
    }

    bool Json::get_bool(const char *key, const bool fallback) const
    {
        const Json *v = find(key);
        return v != nullptr ? v->as_bool(fallback) : fallback;
    }

    void Json::push_back(Json v)
    {
        if (type_ == Type::Array)
        {
            arr_.push_back(std::move(v));
        }
    }

    void Json::set(std::string key, Json v)
    {
        if (type_ != Type::Object)
        {
            return;
        }
        for (auto &m : obj_)
        {
            if (m.first == key)
            {
                m.second = std::move(v);
                return;
            }
        }
        obj_.emplace_back(std::move(key), std::move(v));
    }

    Json Json::parse(const char *&p, const char *end, std::string *error)
    {
        return parse_value(p, end, error, 0);
    }

    Json Json::parse_value(const char *&p, const char *end,
                           std::string *error, const int depth)
    {
            const auto fail = [&](const char *msg)
            {
                if (error != nullptr)
                {
                    *error = msg;
                }
                return Json();
            };
            if (depth > Json::kMaxDepth)
            {
                return fail("nesting too deep");
            }
            skip_ws(p, end);
            if (p >= end)
            {
                return fail("empty input");
            }
            switch (*p)
            {
            case '{':
            {
                ++p;
                Json j = object();
                skip_ws(p, end);
                if (p < end && *p == '}')
                {
                    ++p;
                    return j;
                }
                for (;;)
                {
                    skip_ws(p, end);
                    std::string key;
                    if (p >= end || !parse_string_body(p, end, key))
                    {
                        return fail("bad object key");
                    }
                    skip_ws(p, end);
                    if (p >= end || *p != ':')
                    {
                        return fail("expected ':'");
                    }
                    ++p;
                    std::string sub;
                    Json v = parse_value(p, end, &sub, depth + 1);
                    if (!sub.empty())
                    {
                        if (error != nullptr)
                        {
                            *error = sub;
                        }
                        return Json();
                    }
                    j.obj_.emplace_back(std::move(key), std::move(v));
                    skip_ws(p, end);
                    if (p < end && *p == ',')
                    {
                        ++p;
                        continue;
                    }
                    if (p < end && *p == '}')
                    {
                        ++p;
                        return j;
                    }
                    return fail("expected ',' or '}'");
                }
            }
            case '[':
            {
                ++p;
                Json j = array();
                skip_ws(p, end);
                if (p < end && *p == ']')
                {
                    ++p;
                    return j;
                }
                for (;;)
                {
                    std::string sub;
                    Json v = parse_value(p, end, &sub, depth + 1);
                    if (!sub.empty())
                    {
                        if (error != nullptr)
                        {
                            *error = sub;
                        }
                        return Json();
                    }
                    j.arr_.push_back(std::move(v));
                    skip_ws(p, end);
                    if (p < end && *p == ',')
                    {
                        ++p;
                        continue;
                    }
                    if (p < end && *p == ']')
                    {
                        ++p;
                        return j;
                    }
                    return fail("expected ',' or ']'");
                }
            }
        case '"':
        {
            Json j;
            j.type_ = Type::String;
            if (!parse_string_body(p, end, j.str_))
            {
                return fail("bad string");
            }
            return j;
        }
        case 't':
            if (end - p >= 4 && p[1] == 'r' && p[2] == 'u' && p[3] == 'e')
            {
                p += 4;
                return boolean(true);
            }
            return fail("bad literal");
        case 'f':
            if (end - p >= 5 && p[1] == 'a' && p[2] == 'l' && p[3] == 's' &&
                p[4] == 'e')
            {
                p += 5;
                return boolean(false);
            }
            return fail("bad literal");
        case 'n':
            if (end - p >= 4 && p[1] == 'u' && p[2] == 'l' && p[3] == 'l')
            {
                p += 4;
                return Json();
            }
            return fail("bad literal");
        default:
        {
            if (*p == '-' || (*p >= '0' && *p <= '9'))
            {
                Json j;
                j.type_ = Type::Number;
                if (!parse_number(p, end, j.num_))
                {
                    return fail("bad number");
                }
                return j;
            }
            return fail("unexpected byte");
        }
        }
    }

    std::string Json::dump() const
    {
        std::string out;
        switch (type_)
        {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += bool_ ? "true" : "false";
            break;
        case Type::Number:
        {
            // v1 numbers are small integers; integral values dump
            // without the decimal noise
            if (std::isfinite(num_) && num_ == static_cast<double>(static_cast<long long>(num_)))
            {
                out += std::to_string(static_cast<long long>(num_));
            }
            else
            {
                out += std::to_string(num_);
            }
            break;
        }
        case Type::String:
            out += '"';
            for (const char c : str_)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    out += c;
                    break;
                }
            }
            out += '"';
            break;
        case Type::Array:
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i)
            {
                if (i != 0)
                {
                    out += ',';
                }
                out += arr_[i].dump();
            }
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t i = 0; i < obj_.size(); ++i)
            {
                if (i != 0)
                {
                    out += ',';
                }
                out += Json::str(obj_[i].first).dump();
                out += ':';
                out += obj_[i].second.dump();
            }
            out += '}';
            break;
        }
        return out;
    }
}  // namespace zb::tianji
