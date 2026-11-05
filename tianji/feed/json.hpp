#pragma once

/*
 * Minimal JSON DOM for the tianji feed protocol (TJ-C2, v1 subset).
 *
 * The wire is the tianji RoomServer's: one JSON line per request, one
 * line per response (an ARRAY of message objects). The shapes this
 * must read are the documented v1 messages (tianji docs/07
 * "接口契约"): flat-ish objects with string/number/bool fields and
 * small arrays (recent[], knowledge[], legal_actions[], props[]).
 *
 * Untrusted-input rules (the review-3/4 parser family): the parser
 * never reads past its buffer, recursion is capped (depth > cap fails
 * the parse, one LW), a degenerate input fails with an error instead
 * of a throw, and the caller skips the message. Strings stay UTF-8
 * byte sequences: \uXXXX escapes decode (surrogate pairs combine, a
 * lone half becomes U+FFFD — the same rule as the framework text
 * decoder), everything else passes through untouched.
 *
 * Parsing never throws; on failure the returned node is Null and
 * `error` (optional) names the reason.
 */

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace zb::tianji
{
    class Json
    {
    public:
        enum class Type
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };

        // object members keep document order (messages are small; the
        // lookup is a linear scan)
        using Member = std::pair<std::string, Json>;

        Json() = default;

        static Json boolean(const bool v);
        static Json number(const double v);
        static Json str(std::string v);
        static Json array();
        static Json object();

        Type type() const { return type_; }
        bool is_null() const { return type_ == Type::Null; }

        bool as_bool(const bool fallback = false) const;
        // rounded, range-checked (out-of-range / non-finite -> fallback)
        long long as_int(const long long fallback = 0) const;
        double as_number(const double fallback = 0.0) const;
        const std::string &as_str() const { return str_; }

        const std::vector<Json> &items() const { return arr_; }
        const std::vector<Member> &members() const { return obj_; }

        // object lookup: nullptr when absent or not an object
        const Json *find(const char *key) const;
        // typed convenience getters (absent / wrong type -> fallback)
        std::string get_str(const char *key, const char *fallback = "") const;
        long long get_int(const char *key, const long long fallback = 0) const;
        bool get_bool(const char *key, const bool fallback = false) const;

        void push_back(Json v);
        void set(std::string key, Json v);

        /*
         * Parses one JSON value from [p, end). On success `*p` stands
         * just past the value; trailing whitespace is NOT consumed
         * (the caller checks the line is exhausted). On failure returns
         * Null, `*p` is unspecified past the failure point, and
         * `error` (when given) names the reason. Nesting deeper than
         * kMaxDepth fails the parse (untrusted-input cap).
         */
        static constexpr int kMaxDepth = 32;
        static Json parse(const char *&p, const char *end,
                          std::string *error = nullptr);

    private:
        // the depth-carrying recursive worker (a member so container
        // construction and the private state stay in class scope)
        static Json parse_value(const char *&p, const char *end,
                                std::string *error, int depth);

    public:

        // serializes back to one compact line (the mock server and the
        // request path use it; strings re-escape minimally)
        std::string dump() const;

    private:
        Type type_ = Type::Null;
        bool bool_ = false;
        double num_ = 0.0;
        std::string str_;
        std::vector<Json> arr_;
        std::vector<Member> obj_;
    };
}  // namespace zb::tianji
