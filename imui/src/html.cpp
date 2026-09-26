#include "html.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "logging.hpp"

namespace zb::ui
{
    namespace
    {
        // -------------------------------------------------------------------
        // the whitelist (docs/html-path.md is the contract). Anything outside
        // these sets constructs nothing. The widget tags emitted below are
        // the shared ui_builder table keys, so build() materializes the
        // result with no HTML knowledge.
        // -------------------------------------------------------------------

        bool is_structural(const std::string &t)
        {
            return t == "html" || t == "head";
        }
        bool is_whitelisted_tag(const std::string &t)
        {
            return t == "div" || t == "p" || t == "span" || t == "label" ||
                   t == "small" || t == "button" || t == "checkbox" || t == "radio" ||
                   t == "br" || t == "toggle" || t == "gauge" ||
                   t == "knob" || t == "trend" || t == "meter" ||
                   t == "svg" || t == "vectordial";
            // NOTE: line/text/g are svg-context only (handled in
            // handle_open while in_svg()); elsewhere they stay
            // off-whitelist and skip with content dropped
        }
        // elements whose bare text becomes an anonymous label: the container
        // tags, and body (the document container)
        bool is_container_html(const std::string &t)
        {
            return t == "div" || t == "body";
        }
        // presentation attributes a transparent svg <g> folds onto its
        // descendant line/text (nearest ancestor wins; the element's own
        // attribute wins over all)
        bool is_svg_inheritable(const std::string &a)
        {
            return a == "stroke" || a == "stroke-width" ||
                   a == "stroke-linecap" || a == "opacity" ||
                   a == "fill" || a == "font-size" ||
                   a == "text-anchor";
        }
        bool is_known_css_prop(const std::string &p)
        {
            return p == "display" || p == "flex-direction" || p == "width" ||
                   p == "height" || p == "flex" || p == "gap" ||
                   p == "padding" || p == "flex-wrap" ||
                   p == "justify-content" || p == "align-items" ||
                   p == "align-self" ||
                   p == "background" || p == "background-color" ||
                   p == "border" || p == "border-radius" || p == "color" ||
                   p == "position" || p == "top" || p == "left" ||
                   p == "right" || p == "bottom" || p == "transform" ||
                   p == "font-size" || p == "aspect-ratio" ||
                   p == "letter-spacing" || p == "font-weight" ||
                   p == "text-shadow" || p == "opacity" ||
                   p == "line-height" ||
                   p == "border-top" || p == "box-shadow" ||
                   p == "margin" || p == "margin-top" ||
                   p == "margin-right" || p == "margin-bottom" ||
                   p == "margin-left" || p == "padding-top" ||
                   p == "padding-right" || p == "padding-bottom" ||
                   p == "padding-left" || p == "transform-origin";
        }

        // --- text helpers --------------------------------------------------

        // the six entities of the subset; anything else (&#...; included)
        // stays literal -- an explicit contract boundary. `&nbsp;` folds
        // to a plain space (no nowrap/collapse distinction until H-1).
        // On success `p` is advanced past the trailing ';'.
        bool append_entity(const char *&p, const char *end, std::string &out)
        {
            static const struct
            {
                const char *name;
                const char *repl;
            } kEntities[] = {
                {"amp;", "&"}, {"lt;", "<"}, {"gt;", ">"},
                {"quot;", "\""}, {"#39;", "'"}, {"nbsp;", " "},
            };
            for (const auto &e : kEntities)
            {
                const std::size_t len = std::strlen(e.name);
                if (static_cast<std::size_t>(end - p) >= len + 1 &&
                    std::string(p + 1, len) == e.name)
                {
                    out += e.repl;
                    p += len + 1;
                    return true;
                }
            }
            return false;
        }

        // expands the entity subset in [begin,end) into `out`
        void append_decoded(const char *begin, const char *end, std::string &out)
        {
            const char *z = begin;
            while (z < end)
            {
                if (*z == '&')
                {
                    const char *before = z;
                    if (append_entity(z, end, out))
                    {
                        continue;
                    }
                    z = before;  // not one of the subset: stays literal
                }
                out += *z++;
            }
        }

        // normalizes free text like HTML: trim the edges, collapse runs of
        // whitespace into a single space, decode the entity subset. The
        // pending space is emitted in front of the next token (a literal or
        // an entity), so `A &amp; B` collapses to `A & B`.
        std::string normalize_text(const char *begin, const char *end)
        {
            std::string out;
            bool pending_space = false;
            const char *p = begin;
            while (p != end)
            {
                const char c = *p;
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    pending_space = true;
                    ++p;
                    continue;
                }
                if (pending_space && !out.empty())
                {
                    out += ' ';
                }
                pending_space = false;
                if (c == '&' && append_entity(p, end, out))
                {
                    continue;  // p advanced past the entity
                }
                out += c;
                ++p;
            }
            return out;
        }

        // H-1: the wrapping variant for paragraph text — same whitespace
        // collapse as normalize_text, but a '\n' (emitted by <br> inside a
        // paragraph, and by multi-line source text) survives as a real hard
        // break and swallows the pending whitespace around it, so <br>
        // never double-spaces (html-path §Text wrapping: consecutive breaks
        // keep their empty lines).
        std::string normalize_wrapped(const char *begin, const char *end)
        {
            std::string out;
            bool pending_space = false;
            const char *p = begin;
            while (p != end)
            {
                const char c = *p;
                if (c == '\n')
                {
                    out += '\n';
                    pending_space = false;  // the break ends pending spaces
                    ++p;
                    continue;
                }
                if (c == ' ' || c == '\t' || c == '\r')
                {
                    pending_space = true;
                    ++p;
                    continue;
                }
                if (pending_space && !out.empty() && out.back() != '\n')
                {
                    out += ' ';
                }
                pending_space = false;
                if (c == '&' && append_entity(p, end, out))
                {
                    continue;  // p advanced past the entity
                }
                out += c;
                ++p;
            }
            return out;
        }

        void ascii_lower_inplace(std::string &s)
        {
            for (char &c : s)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
        }

        // -------------------------------------------------------------------
        // the tokenizer (B6): raw input -> token stream. It owns every
        // lexical rule (comments/doctype skipping, tag-name casing,
        // quote-aware token ends, attribute values with entity decoding)
        // and carries 1-based line numbers for located warnings. It knows
        // nothing about the whitelist or the tree.
        // -------------------------------------------------------------------

        struct Token
        {
            enum class Kind
            {
                eof,
                text,  // raw span (normalization is the tree layer's job)
                open,
                close,
            };
            Kind kind = Kind::eof;
            std::string name;  // lower-cased tag name (open/close)
            std::vector<std::pair<std::string, std::string>> attrs;  // open
            bool self_closing = false;  // open only
            std::string text;           // text only
            int line = 1;               // 1-based line where it starts
        };

        class Tokenizer
        {
          public:
            Tokenizer(const char *begin, const char *end)
                : p_(begin), end_(end)
            {
            }

            Token next()
            {
                for (;;)
                {
                    if (p_ >= end_)
                    {
                        return Token{};
                    }
                    if (*p_ != '<')
                    {
                        return read_text();
                    }
                    // a tag, comment, or doctype begins at '<'
                    const char *q = p_ + 1;
                    if (q < end_ && *q == '!')
                    {
                        skip_bang();  // comments/doctypes are silent
                        continue;
                    }
                    Token t;
                    if (read_tag(t))
                    {
                        return t;
                    }
                    // a lone '<' or malformed token: consumed, move on
                }
            }

          private:
            const char *p_;
            const char *end_;
            int line_ = 1;

            void count_lines(const char *from, const char *to)
            {
                for (const char *z = from; z < to; ++z)
                {
                    if (*z == '\n')
                    {
                        ++line_;
                    }
                }
            }

            Token read_text()
            {
                Token t;
                t.kind = Token::Kind::text;
                t.line = line_;
                const char *start = p_;
                while (p_ < end_ && *p_ != '<')
                {
                    ++p_;
                }
                t.text.assign(start, p_);
                count_lines(start, p_);
                return t;
            }

            // comment <!-- ... --> or <!DOCTYPE ...>: consumed silently
            void skip_bang()
            {
                const char *start = p_;
                const char *q = p_ + 1;
                if (q + 2 < end_ && q[1] == '-' && q[2] == '-')
                {
                    const char *close = q + 3;
                    while (close + 2 < end_ &&
                           !(close[0] == '-' && close[1] == '-' && close[2] == '>'))
                    {
                        ++close;
                    }
                    p_ = (close + 2 < end_) ? close + 3 : end_;
                }
                else
                {
                    while (p_ < end_ && *p_ != '>')
                    {
                        ++p_;
                    }
                    if (p_ < end_)
                    {
                        ++p_;
                    }
                }
                count_lines(start, p_);
            }

            static bool is_name_char(const char c)
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-';
            }

            static bool is_attr_char(const char c)
            {
                return is_name_char(c) || c == '_' || c == ':';
            }

            static bool is_space(const char c)
            {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r';
            }

            // Reads one tag token into `t`; returns false for a lone '<'
            // or malformed token (consumed, no token produced).
            bool read_tag(Token &t)
            {
                t.line = line_;
                const char *start = p_;
                const char *q = p_ + 1;
                const bool closing = (q < end_ && *q == '/');
                if (closing)
                {
                    ++q;
                }
                const char *name_b = q;
                while (q < end_ && is_name_char(*q))
                {
                    ++q;
                }
                t.name.assign(name_b, q);
                ascii_lower_inplace(t.name);

                // the token end: '>' (with '/' before it marking
                // self-closing), honoring quoted attribute values. C6:
                // both quote styles open a quoted span; only the matching
                // quote closes it (a '"' inside '...' stays literal).
                const char *scan = q;
                char quote = 0;
                while (scan < end_ && !(quote == 0 && *scan == '>'))
                {
                    if (quote == 0 && (*scan == '"' || *scan == '\''))
                    {
                        quote = *scan;
                    }
                    else if (*scan == quote)
                    {
                        quote = 0;
                    }
                    ++scan;
                }
                const bool self_closing =
                    (scan < end_) && (scan > q) && (scan[-1] == '/');
                const char *attr_end = (scan < end_ ? scan : end_);
                const char *token_end =
                    std::min(end_, (scan < end_ ? scan + 1 : end_));

                if (closing || t.name.empty())
                {
                    // a closing tag (possibly nameless: ignored downstream)
                    // or a lone '<' / malformed token: consume and move on
                    t.kind = Token::Kind::close;
                    p_ = token_end;
                    count_lines(start, p_);
                    return !t.name.empty();
                }

                t.kind = Token::Kind::open;
                t.self_closing = self_closing;

                // attributes (unknown ones are kept; the tree layer drops
                // what is off-whitelist silently)
                const char *attr_p = q;
                while (attr_p < attr_end)
                {
                    while (attr_p < attr_end && is_space(*attr_p))
                    {
                        ++attr_p;
                    }
                    if (attr_p >= attr_end)
                    {
                        break;
                    }
                    const char *ka = attr_p;
                    while (attr_p < attr_end && is_attr_char(*attr_p))
                    {
                        ++attr_p;
                    }
                    std::string key(ka, attr_p);
                    ascii_lower_inplace(key);
                    if (key.empty())
                    {
                        ++attr_p;  // junk: move on
                        continue;
                    }
                    while (attr_p < attr_end && is_space(*attr_p))
                    {
                        ++attr_p;
                    }
                    std::string value;
                    if (attr_p < attr_end && *attr_p == '=')
                    {
                        ++attr_p;
                        while (attr_p < attr_end &&
                               (*attr_p == ' ' || *attr_p == '\t'))
                        {
                            ++attr_p;
                        }
                        // C6: single-quoted values mirror double-quoted
                        // ones (entity decoding included)
                        if (attr_p < attr_end &&
                            (*attr_p == '"' || *attr_p == '\''))
                        {
                            const char qc = *attr_p;
                            ++attr_p;
                            const char *vb = attr_p;
                            while (attr_p < attr_end && *attr_p != qc)
                            {
                                ++attr_p;
                            }
                            const char *ve = attr_p;
                            if (attr_p < attr_end)
                            {
                                ++attr_p;  // the closing quote
                            }
                            append_decoded(vb, ve, value);
                        }
                        else
                        {
                            // C4: an unquoted value ends at whitespace or
                            // '/': `<meter value=30/>` means value "30",
                            // self-closed (author intent over the letter of
                            // HTML5, where the '/' would join the value)
                            const char *vb = attr_p;
                            while (attr_p < attr_end && !is_space(*attr_p) &&
                                   *attr_p != '/')
                            {
                                ++attr_p;
                            }
                            append_decoded(vb, attr_p, value);
                        }
                    }
                    else
                    {
                        value = key;  // valueless boolean attribute
                    }
                    t.attrs.emplace_back(std::move(key), std::move(value));
                }

                p_ = token_end;
                count_lines(start, p_);
                return true;
            }
        };

        // -------------------------------------------------------------------
        // element tree (intermediate) + <style> rules
        // -------------------------------------------------------------------

        struct Elem
        {
            std::string tag;
            std::vector<std::pair<std::string, std::string>> attrs;
            std::string text;  // raw accumulation (leaves), normalized at close
            std::vector<std::unique_ptr<Elem>> children;
            int line = 1;  // 1-based source line of the open tag (B6)

            const std::string &attr(const std::string &name) const
            {
                for (const auto &a : attrs)
                {
                    if (a.first == name)
                    {
                        return a.second;
                    }
                }
                static const std::string kEmpty;
                return kEmpty;
            }
        };

        struct Decl
        {
            std::string prop;
            std::string value;
            bool important = false;  // B5: trailing "!important" tier
        };
        // one compound of a selector chain: optional tag plus #id and
        // .class parts (tag lowercased, id/classes case-sensitive)
        struct Compound
        {
            std::string tag;
            std::string id;
            std::vector<std::string> classes;
        };
        // a selector chain in descendant order (last part = subject);
        // vars_only (exact ":root") collects --* and never matches.
        // pseudo marks a pseudo-element rule (H-10: 1 = ::before,
        // 2 = ::after); it never matches an element itself, only its
        // generated box through fold_pseudo.
        struct Selector
        {
            std::vector<Compound> parts;
            bool vars_only = false;
            int pseudo = 0;
            int ids = 0, classes = 0, tags = 0;  // specificity sums
        };
        struct Rule
        {
            std::vector<Selector> selectors;  // comma group
            std::vector<Decl> decls;
        };
        using Rules = std::vector<Rule>;
        // an element's matchable face for descendant chains
        struct Ancestor
        {
            std::string tag;
            std::string id;
            std::vector<std::string> classes;
        };
        using VarMap = std::vector<std::pair<std::string, std::string>>;

        // B5: a trailing "!important" (ASCII case-insensitive,
        // whitespace tolerated around it) lifts the declaration into the
        // important tier instead of the value. "red!" or "a!b" are not
        // markers -- the value keeps them and fails parsing as usual.
        bool take_important(std::string &val)
        {
            const std::size_t bang = val.find_last_of('!');
            if (bang == std::string::npos)
            {
                return false;
            }
            std::string rest = val.substr(bang + 1);
            rest.erase(0, rest.find_first_not_of(" \t\n\r"));
            rest.erase(rest.find_last_not_of(" \t\n\r") + 1);
            for (char &c : rest)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            if (rest != "important")
            {
                return false;
            }
            val.erase(bang);
            val.erase(val.find_last_not_of(" \t\n\r") + 1);
            return true;
        }

        // an inline `style="..."` value parses exactly like a rule body
        void parse_declarations(const char *begin, const char *end,
                                std::vector<Decl> &out)
        {
            // CSS /* */ comments never reach a prop or value
            // (html-path.md): without this a comment glues to its
            // neighbor declaration and the neighbor silently drops
            // (series7 :root lost every commented var's successor)
            auto skip_space = [](const char *&p, const char *end) {
                for (;;)
                {
                    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                                       *p == '\r'))
                    {
                        ++p;
                    }
                    if (p + 1 < end && p[0] == '/' && p[1] == '*')
                    {
                        p += 2;
                        while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
                        {
                            ++p;
                        }
                        if (p + 1 < end)
                        {
                            p += 2;
                        }
                        continue;
                    }
                    break;
                }
            };
            // a comment inside a shorthand value must not poison the
            // whole declaration either
            auto strip_comments = [](std::string v) {
                std::string out;
                for (std::size_t i = 0; i < v.size();)
                {
                    if (v[i] == '/' && i + 1 < v.size() && v[i + 1] == '*')
                    {
                        i += 2;
                        while (i + 1 < v.size() && !(v[i] == '*' && v[i + 1] == '/'))
                        {
                            ++i;
                        }
                        i += 2;
                    }
                    else
                    {
                        out.push_back(v[i]);
                        ++i;
                    }
                }
                return out;
            };
            const char *p = begin;
            while (p < end)
            {
                skip_space(p, end);
                if (p < end && *p == ';')
                {
                    ++p;
                    continue;
                }
                if (p >= end)
                {
                    break;
                }
                const char *prop_b = p;
                while (p < end && *p != ':' && *p != ';')
                {
                    ++p;
                }
                if (p >= end || *p != ':')
                {
                    continue;  // no colon: drop the junk segment
                }
                std::string prop(prop_b, p);
                ++p;  // ':'
                const char *val_b = p;
                while (p < end && *p != ';' && *p != '}')
                {
                    // a comment may hold ';' or '}' (comment text is
                    // never syntax); the strip below removes the span
                    if (p + 1 < end && p[0] == '/' && p[1] == '*')
                    {
                        p += 2;
                        while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
                        {
                            ++p;
                        }
                        if (p + 1 < end)
                        {
                            p += 2;
                        }
                        continue;
                    }
                    ++p;
                }
                std::string val = strip_comments(std::string(val_b, p));
                prop.erase(0, prop.find_first_not_of(" \t\n\r"));
                prop.erase(prop.find_last_not_of(" \t\n\r") + 1);
                val.erase(0, val.find_first_not_of(" \t\n\r"));
                val.erase(val.find_last_not_of(" \t\n\r") + 1);
                for (char &c : prop)
                {
                    if (c >= 'A' && c <= 'Z')
                    {
                        c = static_cast<char>(c - 'A' + 'a');
                    }
                }
                if (!prop.empty())
                {
                    bool important = false;
                    if (!val.empty())
                    {
                        important = take_important(val);
                    }
                    out.push_back({std::move(prop), std::move(val), important});
                }
            }
        }

        void skip_css_ws(const char *&p, const char *end)
        {
            for (;;)
            {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                                   *p == '\r'))
                {
                    ++p;
                }
                if (p + 1 < end && p[0] == '/' && p[1] == '*')
                {
                    p += 2;
                    while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
                    {
                        ++p;
                    }
                    if (p + 1 < end)
                    {
                        p += 2;
                    }
                    continue;
                }
                return;
            }
        }

        // consumes a rule body starting just past '{': nested blocks
        // (at-rules), /* comments */, and "..." strings are honored so a
        // rejected rule cannot swallow the rules after it (C1). Returns
        // just past the matching '}' (or end on unterminated input).
        const char *skip_rule_body(const char *p, const char *end)
        {
            int depth = 1;
            while (p < end && depth > 0)
            {
                if (p + 1 < end && p[0] == '/' && p[1] == '*')
                {
                    p += 2;
                    while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
                    {
                        ++p;
                    }
                    if (p + 1 < end)
                    {
                        p += 2;
                    }
                    continue;
                }
                if (*p == '"')
                {
                    ++p;
                    while (p < end && *p != '"')
                    {
                        if (*p == '\\' && p + 1 < end)
                        {
                            p += 2;
                        }
                        else
                        {
                            ++p;
                        }
                    }
                    if (p < end)
                    {
                        ++p;
                    }
                    continue;
                }
                if (*p == '{')
                {
                    ++depth;
                }
                else if (*p == '}')
                {
                    --depth;
                }
                ++p;
            }
            return p;
        }

        // the class attribute as a kept-case list (HTML classes are
        // case-sensitive; empties skipped)
        std::vector<std::string> class_list(const std::string &attr)
        {
            std::vector<std::string> out;
            std::size_t i = 0;
            while (i < attr.size())
            {
                while (i < attr.size() && (attr[i] == ' ' || attr[i] == '\t' ||
                                           attr[i] == '\n' || attr[i] == '\r' ||
                                           attr[i] == '\f'))
                {
                    ++i;
                }
                std::size_t j = i;
                while (j < attr.size() && attr[j] != ' ' && attr[j] != '\t' &&
                       attr[j] != '\n' && attr[j] != '\r' && attr[j] != '\f')
                {
                    ++j;
                }
                if (j > i)
                {
                    out.push_back(attr.substr(i, j - i));
                }
                i = j;
            }
            return out;
        }

        static bool is_sel_name(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_';
        }

        std::string ascii_lower(std::string s);  // defined with the B4 helpers below
        bool parse_svg_num(const std::string &s, long long &out);  // defined by widget_type

        // one compound: [tag][#id][.class]* in any order. ':'/'['/'>'
        // and friends fail it (pseudo/attribute/combinators stay out).
        bool parse_compound(const std::string &s, Compound &out)
        {
            std::size_t i = 0;
            if (i < s.size() &&
                ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z')))
            {
                std::size_t j = i + 1;
                while (j < s.size() && is_sel_name(s[j]))
                {
                    ++j;
                }
                out.tag = ascii_lower(s.substr(i, j - i));
                i = j;
            }
            while (i < s.size())
            {
                const char kind = s[i];
                if (kind != '#' && kind != '.')
                {
                    return false;
                }
                ++i;
                std::size_t j = i;
                while (j < s.size() && is_sel_name(s[j]))
                {
                    ++j;
                }
                if (j == i)
                {
                    return false;  // C5: a bare '#' or '.' matches nothing
                }
                if (kind == '#')
                {
                    if (!out.id.empty())
                    {
                        return false;  // one id per compound
                    }
                    out.id = s.substr(i, j - i);
                }
                else
                {
                    out.classes.push_back(s.substr(i, j - i));
                }
                i = j;
            }
            return !out.tag.empty() || !out.id.empty() || !out.classes.empty();
        }

        // a comma group into selectors; pseudo parts are reported through
        // saw_pseudo (one LW per rule at the call site), every other
        // failure is silent. At-rules (@media) never reach here.
        bool parse_selector_group(const std::string &sel,
                                  std::vector<Selector> &out, bool &saw_pseudo)
        {
            saw_pseudo = false;
            std::size_t i = 0;
            while (i < sel.size())
            {
                while (i < sel.size() && (sel[i] == ' ' || sel[i] == '\t' ||
                                          sel[i] == '\n' || sel[i] == '\r'))
                {
                    ++i;
                }
                std::size_t j = i;
                while (j < sel.size() && sel[j] != ',')
                {
                    ++j;
                }
                std::string part = sel.substr(i, j - i);
                part.erase(0, part.find_first_not_of(" \t\n\r"));
                const std::size_t tail = part.find_last_not_of(" \t\n\r");
                if (tail == std::string::npos)
                {
                    i = j + 1;  // empty part: skipped silently
                    continue;
                }
                part.erase(tail + 1);
                if (ascii_lower(part) == ":root")
                {
                    Selector s;
                    s.vars_only = true;
                    out.push_back(std::move(s));
                    i = j + 1;
                    continue;
                }
                // H-10 pseudo mark: a trailing ::before/::after
                // (legacy single-colon included) strips to its base and
                // falls through to the chain parse; anything else
                // pseudo stays inert
                int pseudo = 0;
                if (part.find(':') != std::string::npos ||
                    part.find('[') != std::string::npos ||
                    part.find('(') != std::string::npos)
                {
                    const std::string low = ascii_lower(part);
                    const char *const tails[4] = {"::before", "::after",
                                                  ":before", ":after"};
                    for (int ti = 0; ti < 4 && pseudo == 0; ++ti)
                    {
                        const std::string tail = tails[ti];
                        if (low.size() > tail.size() &&
                            low.compare(low.size() - tail.size(),
                                        tail.size(), tail) == 0)
                        {
                            std::string base = part.substr(
                                0, part.size() - tail.size());
                            const std::size_t end =
                                base.find_last_not_of(" \t\n\r");
                            if (end != std::string::npos)
                            {
                                base.erase(end + 1);
                            }
                            if (!base.empty() &&
                                base.find(':') == std::string::npos &&
                                base.find('[') == std::string::npos &&
                                base.find('(') == std::string::npos)
                            {
                                pseudo = (ti % 2 == 0) ? 1 : 2;
                                part = base;
                            }
                        }
                    }
                    if (pseudo == 0)
                    {
                        if (part.find(':') != std::string::npos)
                        {
                            saw_pseudo = true;
                        }
                        i = j + 1;  // pseudo/attribute/function: inert part
                        continue;
                    }
                }
                // descendant chain: compounds separated by whitespace
                Selector s;
                bool ok = true;
                std::size_t k = 0;
                while (k < part.size() && ok)
                {
                    while (k < part.size() && (part[k] == ' ' || part[k] == '\t' ||
                                               part[k] == '\n' || part[k] == '\r'))
                    {
                        ++k;
                    }
                    if (k >= part.size())
                    {
                        break;
                    }
                    std::size_t m = k;
                    while (m < part.size() && part[m] != ' ' && part[m] != '\t' &&
                           part[m] != '\n' && part[m] != '\r')
                    {
                        ++m;
                    }
                    Compound c;
                    if (!parse_compound(part.substr(k, m - k), c))
                    {
                        ok = false;  // '>'/'*'/junk: the part is inert
                        break;
                    }
                    if (!c.id.empty())
                    {
                        ++s.ids;
                    }
                    s.classes += static_cast<int>(c.classes.size());
                    if (!c.tag.empty())
                    {
                        ++s.tags;
                    }
                    s.parts.push_back(std::move(c));
                    k = m;
                }
                if (ok && !s.parts.empty())
                {
                    s.pseudo = pseudo;
                    out.push_back(std::move(s));
                }
                i = j + 1;
            }
            return !out.empty();
        }

        bool compound_matches(const Compound &c, const std::string &tag,
                              const std::string &id,
                              const std::vector<std::string> &classes)
        {
            if (!c.tag.empty() && c.tag != tag)
            {
                return false;
            }
            if (!c.id.empty() && c.id != id)
            {
                return false;
            }
            for (const std::string &k : c.classes)
            {
                bool found = false;
                for (const std::string &have : classes)
                {
                    if (have == k)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    return false;
                }
            }
            return true;
        }

        // ancestors root-first; preceding parts match in order (not
        // necessarily adjacent)
        bool selector_matches(const Selector &s, const std::string &tag,
                               const std::string &id,
                               const std::vector<std::string> &classes,
                               const std::vector<Ancestor> &ancestors)
        {
            if (s.vars_only || s.parts.empty())
            {
                return false;
            }
            if (!compound_matches(s.parts.back(), tag, id, classes))
            {
                return false;
            }
            std::size_t a = ancestors.size();
            for (std::size_t k = s.parts.size() - 1; k-- > 0;)
            {
                bool found = false;
                while (a > 0)
                {
                    --a;
                    if (compound_matches(s.parts[k], ancestors[a].tag,
                                         ancestors[a].id, ancestors[a].classes))
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    return false;
                }
            }
            return true;
        }

        void collect_vars(const Rules &rules, VarMap &vars)
        {
            for (const Rule &r : rules)
            {
                bool root_rule = false;
                for (const Selector &s : r.selectors)
                {
                    if (s.vars_only)
                    {
                        root_rule = true;
                        break;
                    }
                }
                if (!root_rule)
                {
                    continue;
                }
                for (const Decl &d : r.decls)
                {
                    if (d.prop.size() > 2 && d.prop[0] == '-' && d.prop[1] == '-')
                    {
                        bool found = false;
                        for (auto &v : vars)
                        {
                            if (v.first == d.prop)
                            {
                                v.second = d.value;
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            vars.emplace_back(d.prop, d.value);
                        }
                    }
                }
            }
        }

        // textual var() substitution (one nesting level through fallbacks
        // and chained variables, depth-capped); false = unknown name
        // without fallback or unbalanced input: the declaration drops
        bool subst_vars(const std::string &s, const VarMap &vars,
                        std::string &out, int depth = 0)
        {
            if (depth > 8)
            {
                return false;
            }
            std::string res;
            std::size_t i = 0;
            while (i < s.size())
            {
                const std::size_t v = s.find("var(", i);
                if (v == std::string::npos)
                {
                    res.append(s, i, std::string::npos);
                    break;
                }
                res.append(s, i, v - i);
                std::size_t j = v + 4;
                int nested = 1;
                while (j < s.size() && nested > 0)
                {
                    if (s[j] == '(')
                    {
                        ++nested;
                    }
                    else if (s[j] == ')')
                    {
                        --nested;
                    }
                    ++j;
                }
                if (nested != 0)
                {
                    return false;
                }
                const std::string inner = s.substr(v + 4, j - v - 5);
                // split name/fallback at the top-level comma
                int lvl = 0;
                std::size_t comma = std::string::npos;
                for (std::size_t k = 0; k < inner.size(); ++k)
                {
                    if (inner[k] == '(')
                    {
                        ++lvl;
                    }
                    else if (inner[k] == ')')
                    {
                        --lvl;
                    }
                    else if (inner[k] == ',' && lvl == 0)
                    {
                        comma = k;
                        break;
                    }
                }
                std::string name = comma == std::string::npos
                                       ? inner
                                       : inner.substr(0, comma);
                name.erase(0, name.find_first_not_of(" \t\n\r"));
                const std::size_t ntail = name.find_last_not_of(" \t\n\r");
                if (ntail == std::string::npos)
                {
                    return false;
                }
                name.erase(ntail + 1);
                const std::string *found = nullptr;
                for (const auto &entry : vars)
                {
                    if (entry.first == name)
                    {
                        found = &entry.second;
                        break;
                    }
                }
                std::string replacement;
                if (found != nullptr)
                {
                    if (!subst_vars(*found, vars, replacement, depth + 1))
                    {
                        return false;
                    }
                }
                else if (comma != std::string::npos)
                {
                    std::string fb = inner.substr(comma + 1);
                    fb.erase(0, fb.find_first_not_of(" \t\n\r"));
                    const std::size_t fbtail = fb.find_last_not_of(" \t\n\r");
                    if (fbtail == std::string::npos ||
                        !subst_vars(fb.erase(fbtail + 1), vars, replacement,
                                    depth + 1))
                    {
                        return false;
                    }
                }
                else
                {
                    return false;
                }
                res += replacement;
                i = j;
            }
            out = res;
            return true;
        }

        // parses the concatenated <style> text into selector groups
        // (parse_selector_group above; the contract lives in
        // docs/html-path.md). At-rules and wholly-invalid groups are
        // consumed silently so following rules survive (C1); only
        // pseudo selectors warn, once per skipped part.
        void parse_css(const std::string &css, Rules &rules)
        {
            const char *begin = css.data();
            const char *end = begin + css.size();
            const char *p = begin;
            while (p < end)
            {
                skip_css_ws(p, end);
                if (p >= end)
                {
                    break;
                }
                const char *sel_b = p;
                while (p < end && *p != '{')
                {
                    ++p;
                }
                if (p >= end)
                {
                    break;  // unclosed rule: stop
                }
                std::string sel(sel_b, p);
                sel.erase(0, sel.find_first_not_of(" \t\n\r"));
                sel.erase(sel.find_last_not_of(" \t\n\r") + 1);
                ++p;  // '{'
                if (sel.empty())
                {
                    p = skip_rule_body(p, end);
                    continue;
                }
                Rule r;
                bool saw_pseudo = false;
                if (sel[0] == '@')
                {
                    p = skip_rule_body(p, end);
                    continue;  // at-rules (@media): inert, never match
                }
                if (!parse_selector_group(sel, r.selectors, saw_pseudo))
                {
                    p = skip_rule_body(p, end);
                    continue;  // wholly invalid: inert, body consumed (C1)
                }
                if (saw_pseudo)
                {
                    LW << "html: pseudo-selectors in '" << sel
                       << "' are not supported; those parts were ignored";
                }
                const char *d_b = p;
                while (p < end && *p != '}')
                {
                    ++p;
                }
                parse_declarations(d_b, p, r.decls);
                if (p < end)
                {
                    ++p;  // '}'
                }
                rules.push_back(std::move(r));
            }
        }

        // style resolution (H-5 cascade): every matching rule contributes
        // its declarations ordered by specificity (ids, classes, tags)
        // then document order; the inline style crowns the list. Custom
        // properties (--*) never enter the folded list; values substitute
        // var() first, and a failed substitution drops its declaration.
        void fold_style(const Elem &e, const Rules &rules, const VarMap &vars,
                        const std::vector<Ancestor> &ancestors,
                        std::vector<Decl> &folded)
        {
            const std::string &id = e.attr("id");
            const std::vector<std::string> classes = class_list(e.attr("class"));
            struct Hit
            {
                int ids = 0, classes = 0, tags = 0;
                std::size_t order = 0;
                const Rule *rule = nullptr;
            };
            std::vector<Hit> hits;
            for (std::size_t i = 0; i < rules.size(); ++i)
            {
                int bi = -1, bc = 0, bt = 0;
                bool any = false;
                for (const Selector &s : rules[i].selectors)
                {
                    // H-10: pseudo-element selectors never style the
                    // element itself (fold_pseudo owns them)
                    if (s.pseudo != 0)
                    {
                        continue;
                    }
                    if (!selector_matches(s, e.tag, id, classes, ancestors))
                    {
                        continue;
                    }
                    any = true;
                    if (s.ids > bi ||
                        (s.ids == bi && (s.classes > bc ||
                                         (s.classes == bc && s.tags > bt))))
                    {
                        bi = s.ids;
                        bc = s.classes;
                        bt = s.tags;
                    }
                }
                if (any)
                {
                    hits.push_back({bi, bc, bt, i, &rules[i]});
                }
            }
            std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
                if (a.ids != b.ids)
                {
                    return a.ids < b.ids;
                }
                if (a.classes != b.classes)
                {
                    return a.classes < b.classes;
                }
                if (a.tags != b.tags)
                {
                    return a.tags < b.tags;
                }
                return a.order < b.order;
            });
            for (const Hit &h : hits)
            {
                for (const Decl &d : h.rule->decls)
                {
                    if (d.prop.size() > 2 && d.prop[0] == '-' && d.prop[1] == '-')
                    {
                        continue;  // custom properties live in the VarMap only
                    }
                    std::string value;
                    if (!subst_vars(d.value, vars, value))
                    {
                        continue;  // unknown var without fallback: dropped
                    }
                    folded.push_back({d.prop, value, d.important});
                }
            }
            const std::string &inline_style = e.attr("style");
            if (!inline_style.empty())
            {
                std::vector<Decl> inline_decls;
                parse_declarations(inline_style.data(),
                                   inline_style.data() + inline_style.size(),
                                   inline_decls);
                for (const Decl &d : inline_decls)
                {
                    if (d.prop.size() > 2 && d.prop[0] == '-' && d.prop[1] == '-')
                    {
                        continue;
                    }
                    std::string value;
                    if (!subst_vars(d.value, vars, value))
                    {
                        continue;
                    }
                    folded.push_back({d.prop, value, d.important});
                }
            }
        }

        // pseudo-element fold (H-10): like fold_style but only
        // selectors carrying the wanted mark contribute, ordered by
        // the same specificity-then-document cascade. Inline styles
        // never apply to generated boxes. Empty out = no rule matched.
        void fold_pseudo(const Elem &e, const Rules &rules,
                         const VarMap &vars,
                         const std::vector<Ancestor> &ancestors,
                         const int kind, std::vector<Decl> &folded)
        {
            const std::string &id = e.attr("id");
            const std::vector<std::string> classes = class_list(e.attr("class"));
            struct Hit
            {
                int ids = 0, classes = 0, tags = 0;
                std::size_t order = 0;
                const Rule *rule = nullptr;
            };
            std::vector<Hit> hits;
            for (std::size_t i = 0; i < rules.size(); ++i)
            {
                int bi = -1, bc = 0, bt = 0;
                bool any = false;
                for (const Selector &s : rules[i].selectors)
                {
                    if (s.pseudo != kind)
                    {
                        continue;
                    }
                    if (!selector_matches(s, e.tag, id, classes, ancestors))
                    {
                        continue;
                    }
                    any = true;
                    if (s.ids > bi ||
                        (s.ids == bi && (s.classes > bc ||
                                         (s.classes == bc && s.tags > bt))))
                    {
                        bi = s.ids;
                        bc = s.classes;
                        bt = s.tags;
                    }
                }
                if (any)
                {
                    hits.push_back({bi, bc, bt, i, &rules[i]});
                }
            }
            std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
                if (a.ids != b.ids)
                {
                    return a.ids < b.ids;
                }
                if (a.classes != b.classes)
                {
                    return a.classes < b.classes;
                }
                if (a.tags != b.tags)
                {
                    return a.tags < b.tags;
                }
                return a.order < b.order;
            });
            for (const Hit &h : hits)
            {
                for (const Decl &d : h.rule->decls)
                {
                    if (d.prop.size() > 2 && d.prop[0] == '-' && d.prop[1] == '-')
                    {
                        continue;
                    }
                    std::string value;
                    if (!subst_vars(d.value, vars, value))
                    {
                        continue;
                    }
                    folded.push_back({d.prop, value, d.important});
                }
            }
        }

        // the last occurrence of a property in the ordered decl list wins,
        // with the B5 tier rule: the last !important match beats every
        // normal one; inside a tier the buckets keep tag -> #id -> inline
        // document order (no cascade, no specificity)
        const std::string *fold_lookup(const std::vector<Decl> &folded,
                                       const std::string &prop)
        {
            const std::string *best = nullptr;
            const std::string *best_important = nullptr;
            for (const Decl &d : folded)
            {
                if (d.prop == prop)
                {
                    best = &d.value;
                    if (d.important)
                    {
                        best_important = &d.value;
                    }
                }
            }
            return (best_important != nullptr) ? best_important : best;
        }

        // validity-exact integer scan (B3): "-1" is a value, not a
        // malformed marker. Same tolerant grammar as parse_int below.
        bool parse_int_value(const std::string &s, long long &out)
        {
            if (s.empty())
            {
                return false;
            }
            std::size_t i = 0;
            bool neg = false;
            if (s[i] == '-')
            {
                neg = true;
                ++i;
            }
            if (i >= s.size())
            {
                return false;  // a lone sign is malformed, not zero
            }
            long long v = 0;
            for (; i < s.size(); ++i)
            {
                const char c = s[i];
                if (c < '0' || c > '9')
                {
                    return false;
                }
                if (v > (9223372036854775807LL - (c - '0')) / 10)
                {
                    return false;
                }
                v = v * 10 + (c - '0');
            }
            out = neg ? -v : v;
            return true;
        }

        long long parse_int(const std::string &s, const long long fallback)
        {
            long long v = 0;
            return parse_int_value(s, v) ? v : fallback;
        }

        // B4: CSS keyword values are ASCII case-insensitive (HTML
        // semantics); ids and all other values keep their case
        std::string ascii_lower(std::string s)
        {
            ascii_lower_inplace(s);
            return s;
        }

        // C3: gap/padding take a bare integer (legacy tolerance, locked
        // by tests) or an Npx length (the contracted form); units fold
        // case like everywhere (B4); negatives stay unset
        bool parse_gap_value(const std::string &s, long long &out)
        {
            const std::string v = ascii_lower(s);
            if (v.size() >= 2 && v.back() == 'x' && v[v.size() - 2] == 'p')
            {
                return parse_int_value(v.substr(0, v.size() - 2), out) &&
                       out >= 0;
            }
            return parse_int_value(v, out) && out >= 0;
        }

        // P-1 paint: split a background value at top-level commas
        // (paren depth 0). Gradient args and rgba() commas live inside
        // parens and never split; layers come out in listed order.
        void split_layers(const std::string &s, std::vector<std::string> &out)
        {
            std::string cur;
            int depth = 0;
            for (const char c : s)
            {
                if (c == '(')
                {
                    ++depth;
                }
                else if (c == ')' && depth > 0)
                {
                    --depth;
                }
                if (c == ',' && depth == 0)
                {
                    out.push_back(cur);
                    cur.clear();
                }
                else
                {
                    cur.push_back(c);
                }
            }
            out.push_back(cur);
        }

        std::string css_trim(std::string v)
        {
            v.erase(0, v.find_first_not_of(" \t\n\r"));
            const std::size_t t = v.find_last_not_of(" \t\n\r");
            if (t == std::string::npos)
            {
                return std::string{};
            }
            v.erase(t + 1);
            return v;
        }

        // H-7c: parse flex-basis value (auto, Npx, N%)
        // Returns true on success, sets flex_basis_px >= 0 for px, flex_basis_pct 1..100 for %
        bool parse_flex_basis(ui_node &n, const std::string &s)
        {
            const std::string v = ascii_lower(css_trim(s));
            if (v == "auto")
            {
                n.flex_basis_px = -1;
                n.flex_basis_pct = 0;
                return true;
            }
            if (v.size() >= 2 && v.back() == 'x' && v[v.size() - 2] == 'p')
            {
                long long px = 0;
                if (parse_int_value(v.substr(0, v.size() - 2), px) && px >= 0)
                {
                    n.flex_basis_px = static_cast<int>(px);
                    n.flex_basis_pct = 0;
                    return true;
                }
            }
            if (!v.empty() && v.back() == '%')
            {
                long long pct = 0;
                if (parse_int_value(v.substr(0, v.size() - 1), pct) && pct >= 1 && pct <= 100)
                {
                    n.flex_basis_px = -1;
                    n.flex_basis_pct = static_cast<int>(pct);
                    return true;
                }
            }
            return false;
        }

        // H-7c: parse flex shorthand (1/2/3 tokens + none)
        // flex: <grow> | <grow> <shrink> | <grow> <shrink> <basis> | none
        // Bad tokens keep their channel's current value; the return
        // reports whether every token landed (the caller warns once)
        bool parse_flex_shorthand(ui_node &n, const std::string &s)
        {
            const std::string v = ascii_lower(css_trim(s));
            if (v == "none")
            {
                n.flex_grow = 0;
                n.flex_shrink = 0;
                n.flex_basis_px = -1;
                n.flex_basis_pct = 0;
                return true;
            }
            // Split by whitespace
            std::vector<std::string> tokens;
            std::string cur;
            for (const char c : v)
            {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    if (!cur.empty())
                    {
                        tokens.push_back(cur);
                        cur.clear();
                    }
                }
                else
                {
                    cur.push_back(c);
                }
            }
            if (!cur.empty())
            {
                tokens.push_back(cur);
            }
            if (tokens.empty() || tokens.size() > 3)
            {
                return false;
            }
            // First token = grow (required)
            bool clean = true;
            long long g = 0;
            if (parse_int_value(tokens[0], g) && g >= 0)
            {
                n.flex_grow = static_cast<int>(g);
            }
            else
            {
                clean = false;
            }
            // Second token = shrink (optional, default 0 per our deviation)
            if (tokens.size() >= 2)
            {
                long long sh = 0;
                if (parse_int_value(tokens[1], sh) && sh >= 0)
                {
                    n.flex_shrink = static_cast<int>(sh);
                }
                else
                {
                    clean = false;
                }
            }
            // Third token = basis (optional, default auto per our deviation)
            if (tokens.size() >= 3)
            {
                clean = parse_flex_basis(n, tokens[2]) && clean;
            }
            return clean;
        }

        // one gradient stop: "<color> [<pos>]" — the color may hold
        // inner spaces (rgba(…) with spaces), so the position splits at
        // the last space; pos "N%" -> pct, bare -> absent (-1), anything
        // else (px/deg/double-position) is unsupported for P-1
        bool parse_stop(const std::string &s, std::string &color, long long &pct)
        {
            const std::string t = css_trim(s);
            if (t.empty())
            {
                return false;
            }
            const std::size_t sp = t.rfind(' ');
            if (sp == std::string::npos)
            {
                color = t;
                pct = -1;
                return true;
            }
            const std::string tail = css_trim(t.substr(sp + 1));
            if (!tail.empty() && tail.back() == '%')
            {
                long long p = 0;
                if (!parse_svg_num(tail.substr(0, tail.size() - 1), p) ||
                    p < 0 || p > 100)
                {
                    return false;
                }
                color = css_trim(t.substr(0, sp));
                pct = p;
                return !color.empty();
            }
            return false;
        }

        // linear-gradient args (already top-level split): angle/direction
        // first, then stops. >2 stops keep first+last (contract); a
        // single stop is a solid. False = unsupported (warn + skip layer).
        bool parse_linear_layer(ui_node &n, const std::vector<std::string> &args)
        {
            if (args.empty())
            {
                return false;
            }
            std::size_t first = 0;
            bool horizontal = false;  // CSS default: to bottom
            const std::string head = css_trim(ascii_lower(args[0]));
            const std::size_t lp = head.find('(');
            if (lp == std::string::npos)
            {
                if (head.size() > 3 && head.compare(head.size() - 3, 3, "deg") == 0)
                {
                    long long deg = 0;
                    if (!parse_svg_num(head.substr(0, head.size() - 3), deg))
                    {
                        return false;
                    }
                    deg = ((deg % 360) + 360) % 360;
                    if (deg == 90 || deg == 270)
                    {
                        horizontal = true;
                    }
                    else if (deg != 0 && deg != 180)
                    {
                        return false;
                    }
                    first = 1;
                }
                else if (head == "to right" || head == "to left")
                {
                    horizontal = true;
                    first = 1;
                }
                else if (head == "to top" || head == "to bottom")
                {
                    first = 1;
                }
                // otherwise the head is a color: no angle, vertical
            }
            else
            {
                return false;  // a function where the angle belongs
            }
            std::vector<std::string> colors;
            std::vector<long long> pos;
            for (std::size_t i = first; i < args.size(); ++i)
            {
                std::string c;
                long long p = -1;
                if (!parse_stop(args[i], c, p))
                {
                    return false;
                }
                colors.push_back(c);
                pos.push_back(p);
            }
            if (colors.empty())
            {
                return false;
            }
            if (colors.size() == 1)
            {
                n.prop("background", colors[0]);
                return true;
            }
            // linear stop offsets are P-1-out: the ramp always spans
            // full (contract); N-stop keeps the end colors
            n.prop("bg_lin_from", colors.front());
            n.prop("bg_lin_to", colors.back());
            n.prop("bg_lin_h", horizontal);
            // P-2c: exactly 3 stops with a %/bare middle also land a
            // mid section (bare = 50); >3 stops stay ends-only
            if (colors.size() == 3 && pos[1] >= -1 && pos[1] <= 100)
            {
                n.prop("bg_lin3_mid", colors[1]);
                n.prop("bg_lin3_p", pos[1] < 0 ? 50 : pos[1]);
            }
            // N-stop linear (4..8): the full list rides bg_linN_pN/cN
            // with resolved percent positions (absent distribute evenly
            // between specified neighbors, ends defaulting 0/100,
            // clamped non-decreasing); >8 stays ends-only
            if (colors.size() >= 4 && colors.size() <= 8)
            {
                std::vector<long long> res = pos;
                if (res.front() < 0)
                {
                    res.front() = 0;
                }
                if (res.back() < 0 || res.back() > 100)
                {
                    res.back() = 100;
                }
                std::size_t run = 0;
                while (run < res.size())
                {
                    if (res[run] >= 0)
                    {
                        ++run;
                        continue;
                    }
                    std::size_t lo = run;
                    while (run < res.size() && res[run] < 0)
                    {
                        ++run;
                    }
                    // res[lo-1] and res[run] are specified (ends
                    // defaulted above); spread the gap evenly
                    const long long a = res[lo - 1];
                    const long long b = res[run];
                    const std::size_t gap = run - lo + 1;
                    for (std::size_t k = lo; k < run; ++k)
                    {
                        res[k] = a + (b - a) * static_cast<long long>(k - lo + 1) /
                                         static_cast<long long>(gap);
                    }
                }
                n.prop("bg_linN_n", static_cast<long long>(colors.size()));
                long long prev = 0;
                for (std::size_t i = 0; i < colors.size(); ++i)
                {
                    long long p = res[i] < 0 ? 0 : res[i];
                    if (p > 100)
                    {
                        p = 100;
                    }
                    if (p < prev)
                    {
                        p = prev;
                    }
                    prev = p;
                    n.prop("bg_linN_p" + std::to_string(i), p);
                    n.prop("bg_linN_c" + std::to_string(i), colors[i]);
                }
            }
            return true;
        }

        // radial-gradient: circle at X% Y% + stops (same first+last
        // rule); ellipse/px centers/extents are P-1-out
        bool parse_radial_layer(ui_node &n, const std::vector<std::string> &args)
        {
            if (args.empty())
            {
                return false;
            }
            long long cx = 50, cy = 50;
            std::size_t first = 0;
            const std::string head = css_trim(ascii_lower(args[0]));
            if (head.find("circle") != std::string::npos)
            {
                first = 1;
                const std::size_t at = head.find("at");
                if (at != std::string::npos)
                {
                    std::string rest = css_trim(head.substr(at + 2));
                    const std::size_t sp = rest.find(' ');
                    if (sp == std::string::npos)
                    {
                        return false;
                    }
                    const std::string xs = css_trim(rest.substr(0, sp));
                    const std::string ys = css_trim(rest.substr(sp + 1));
                    if (xs.empty() || xs.back() != '%' || ys.empty() ||
                        ys.back() != '%')
                    {
                        return false;
                    }
                    if (!parse_svg_num(xs.substr(0, xs.size() - 1), cx) ||
                        !parse_svg_num(ys.substr(0, ys.size() - 1), cy) ||
                        cx < 0 || cx > 100 || cy < 0 || cy > 100)
                    {
                        return false;
                    }
                }
            }
            else if (head.find('(') == std::string::npos)
            {
                // a bare color head means circle-at-center with the head
                // as the first stop (shape keyword omitted)
            }
            else
            {
                return false;
            }
            std::vector<std::string> colors;
            std::vector<long long> pos;
            for (std::size_t i = first; i < args.size(); ++i)
            {
                std::string c;
                long long p = -1;
                if (!parse_stop(args[i], c, p))
                {
                    return false;
                }
                colors.push_back(c);
                pos.push_back(p);
            }
            if (colors.empty())
            {
                return false;
            }
            if (colors.size() == 1)
            {
                n.prop("background", colors[0]);
                return true;
            }
            // radial stop offsets rescale the ramp (contract P-1);
            // bare ends default to 0/100
            const long long fp = pos.front() < 0 ? 0 : pos.front();
            const long long tp = pos.back() < 0 ? 100 : pos.back();
            n.prop("bg_rad_cx", cx);
            n.prop("bg_rad_cy", cy);
            n.prop("bg_rad_from", colors.front());
            n.prop("bg_rad_from_p", fp);
            n.prop("bg_rad_to", colors.back());
            n.prop("bg_rad_to_p", tp);
            return true;
        }

        // one background layer value: solid passthrough,
        // linear/radial/conic gradients, everything else (repeating-*,
        // url() textures, junk) unsupported. True when a prop landed.
        bool parse_conic_layer(ui_node &n,
                               const std::vector<std::string> &args);
        bool parse_bg_layer(ui_node &n, const std::string &layer)
        {
            const std::string t = css_trim(layer);
            if (t.empty())
            {
                return false;
            }
            const std::string low = ascii_lower(t);
            const std::size_t lp = low.find('(');
            if (lp == std::string::npos)
            {
                n.prop("background", t);  // solid (parse_color decides)
                return true;
            }
            const std::string fn = css_trim(low.substr(0, lp));
            if (fn != "linear-gradient" && fn != "radial-gradient" &&
                fn != "conic-gradient")
            {
                // rgb()/rgba() are solid colors in function clothing;
                // repeating layers ride their own overlay pass below
                // (the fold calls parse_repeating_layer directly), url()
                // and junk are unsupported
                if (fn == "rgb" || fn == "rgba")
                {
                    n.prop("background", t);
                    return true;
                }
                return false;
            }
            if (low.back() != ')')
            {
                return false;
            }
            std::vector<std::string> args;
            split_layers(t.substr(lp + 1, t.size() - lp - 2), args);
            if (fn == "linear-gradient")
            {
                return parse_linear_layer(n, args);
            }
            if (fn == "radial-gradient")
            {
                return parse_radial_layer(n, args);
            }
            return parse_conic_layer(n, args);
        }

        // whether a background prop (solid, base gradient, or
        // repeating overlay) already landed — gates the
        // no-supported-layer warning
        bool has_bg_prop(const ui_node &n)
        {
            for (const auto &p : n.props)
            {
                if (p.first == "background" || p.first == "bg_lin_from" ||
                    p.first == "bg_rad_from" || p.first == "bg_con_from" ||
                    p.first == "bg_rep_n")
                {
                    return true;
                }
            }
            return false;
        }

        // a top-level repeating-linear-gradient layer (the overlay
        // pass matches this before parsing)
        bool is_repeating_layer(const std::string &layer)
        {
            const std::string t = css_trim(ascii_lower(layer));
            return t.compare(0, 26, "repeating-linear-gradient(") == 0 &&
                   !t.empty() && t.back() == ')';
        }

        // one px length (Npx or bare N); % and junk fail
        bool parse_px_len(const std::string &tok, long long &v)
        {
            std::string t = css_trim(ascii_lower(tok));
            if (!t.empty() && t.back() == '%')
            {
                return false;
            }
            if (t.size() > 2 && t.compare(t.size() - 2, 2, "px") == 0)
            {
                t = t.substr(0, t.size() - 2);
            }
            if (!parse_svg_num(t, v))
            {
                return false;
            }
            v = v < 0 ? 0 : v;
            return true;
        }

        // repeating-linear-gradient (P-2c): P-1 angle rule for the
        // axis, then 2..6 px stops (double-position "C A B" pairs
        // welcome); period = last stop, must be > 0. Stops clamp
        // non-decreasing. True when the overlay props landed.
        bool parse_repeating_layer(ui_node &n, const std::string &layer)
        {
            const std::string t = css_trim(layer);
            const std::size_t lp = ascii_lower(t).find('(');
            if (lp == std::string::npos || t.back() != ')')
            {
                return false;
            }
            std::vector<std::string> args;
            split_layers(t.substr(lp + 1, t.size() - lp - 2), args);
            if (args.empty())
            {
                return false;
            }
            std::size_t first = 0;
            bool horizontal = false;
            const std::string head = css_trim(ascii_lower(args[0]));
            const std::size_t hlp = head.find('(');
            if (hlp == std::string::npos)
            {
                if (head.size() > 3 &&
                    head.compare(head.size() - 3, 3, "deg") == 0)
                {
                    long long deg = 0;
                    if (!parse_svg_num(head.substr(0, head.size() - 3),
                                       deg))
                    {
                        return false;
                    }
                    deg = ((deg % 360) + 360) % 360;
                    if (deg == 90 || deg == 270)
                    {
                        horizontal = true;
                    }
                    else if (deg != 0 && deg != 180)
                    {
                        return false;
                    }
                    first = 1;
                }
                else if (head == "to right" || head == "to left")
                {
                    horizontal = true;
                    first = 1;
                }
                else if (head == "to top" || head == "to bottom")
                {
                    first = 1;
                }
                // otherwise the head is a bare color: no angle (but a
                // bare repeating stop is still unsupported below)
            }
            else
            {
                return false;
            }
            std::vector<std::string> colors;
            std::vector<long long> pos;
            for (std::size_t i = first; i < args.size(); ++i)
            {
                const std::string a = css_trim(args[i]);
                if (a.empty())
                {
                    return false;
                }
                const std::size_t s1 = a.rfind(' ');
                if (s1 == std::string::npos)
                {
                    return false;  // bare repeating stops drop the layer
                }
                long long b = 0;
                if (!parse_px_len(a.substr(s1 + 1), b))
                {
                    return false;
                }
                std::string front = css_trim(a.substr(0, s1));
                const std::size_t s0 = front.rfind(' ');
                if (s0 == std::string::npos)
                {
                    colors.push_back(front);
                    pos.push_back(b);
                    continue;
                }
                // a second trailing length makes a double-position pair
                long long fa = 0;
                if (parse_px_len(front.substr(s0 + 1), fa))
                {
                    std::string c = css_trim(front.substr(0, s0));
                    if (c.empty())
                    {
                        return false;
                    }
                    colors.push_back(c);
                    pos.push_back(fa);
                    colors.push_back(c);
                    pos.push_back(b);
                }
                else
                {
                    colors.push_back(front);
                    pos.push_back(b);
                }
            }
            if (colors.size() < 2 || colors.size() > 6 || pos.back() <= 0)
            {
                return false;
            }
            for (std::size_t i = 1; i < pos.size(); ++i)
            {
                if (pos[i] < pos[i - 1])
                {
                    pos[i] = pos[i - 1];
                }
            }
            n.prop("bg_rep_h", horizontal);
            n.prop("bg_rep_period", pos.back());
            n.prop("bg_rep_n", static_cast<long long>(colors.size()));
            for (std::size_t i = 0; i < colors.size(); ++i)
            {
                n.prop("bg_rep_p" + std::to_string(i), pos[i]);
                n.prop("bg_rep_c" + std::to_string(i), colors[i]);
            }
            return true;
        }

        // conic-gradient (P-2b): "from Ndeg" head (default 0), then 2..4
        // stops "color Ndeg" (bare N = degrees; % positions, `at`
        // centers, and >4 stops drop the layer). Stop positions clamp
        // non-decreasing (CSS clamp rule); first defaults 0, last 360.
        bool parse_conic_layer(ui_node &n, const std::vector<std::string> &args)
        {
            if (args.empty())
            {
                return false;
            }
            std::size_t first = 0;
            long long from = 0;
            const std::string head = css_trim(ascii_lower(args[0]));
            if (head.compare(0, 4, "from") == 0 &&
                (head.size() == 4 || head[4] == ' '))
            {
                std::string deg = css_trim(head.substr(4));
                if (deg.size() < 4 ||
                    deg.compare(deg.size() - 3, 3, "deg") != 0)
                {
                    return false;
                }
                if (!parse_svg_num(deg.substr(0, deg.size() - 3), from))
                {
                    return false;
                }
                first = 1;
            }
            else if (head.compare(0, 3, "at ") == 0 ||
                     head.find(" at ") != std::string::npos ||
                     head.find('(') != std::string::npos)
            {
                return false;  // `at` centers are P-2b-out
            }
            // otherwise the head is the first stop (no `from`)
            std::vector<std::string> colors;
            std::vector<long long> pos;
            for (std::size_t i = first; i < args.size(); ++i)
            {
                const std::string t = css_trim(args[i]);
                if (t.empty())
                {
                    return false;
                }
                const std::size_t sp = t.rfind(' ');
                std::string c;
                long long p = -1;
                if (sp == std::string::npos)
                {
                    c = t;  // bare color: position fills in below
                }
                else
                {
                    c = css_trim(t.substr(0, sp));
                    std::string tail =
                        css_trim(ascii_lower(t.substr(sp + 1)));
                    if (tail.empty() || tail.back() == '%')
                    {
                        return false;  // % positions are P-2b-out
                    }
                    if (tail.size() > 3 &&
                        tail.compare(tail.size() - 3, 3, "deg") == 0)
                    {
                        tail = tail.substr(0, tail.size() - 3);
                    }
                    if (!parse_svg_num(tail, p) || c.empty())
                    {
                        return false;
                    }
                }
                colors.push_back(c);
                pos.push_back(p);
            }
            if (colors.size() < 2 || colors.size() > 4)
            {
                return false;
            }
            if (pos.front() < 0)
            {
                pos.front() = 0;
            }
            if (pos.back() < 0)
            {
                pos.back() = 360;
            }
            // bare runs divide their bracketing span evenly (CSS
            // omitted-position rule, integer steps)
            std::size_t i = 1;
            while (i + 1 < pos.size())
            {
                if (pos[i] >= 0)
                {
                    ++i;
                    continue;
                }
                std::size_t k = i;
                while (k + 1 < pos.size() && pos[k] < 0)
                {
                    ++k;
                }
                const long long step =
                    (pos[k] - pos[i - 1]) /
                    static_cast<long long>(k - i + 1);
                for (std::size_t m = i; m < k; ++m)
                {
                    pos[m] =
                        pos[i - 1] +
                        step * static_cast<long long>(m - i + 1);
                }
                i = k;
            }
            for (std::size_t i = 1; i < pos.size(); ++i)
            {
                if (pos[i] < pos[i - 1])
                {
                    pos[i] = pos[i - 1];  // CSS clamp rule
                }
                if (pos[i] > 360)
                {
                    pos[i] = 360;
                }
            }
            n.prop("bg_con_from", from);
            for (std::size_t i = 0; i < colors.size(); ++i)
            {
                n.prop("bg_con_p" + std::to_string(i), pos[i]);
                n.prop("bg_con_c" + std::to_string(i), colors[i]);
            }
            return true;
        }

        // transform: only translate(X[, Y]) is P-3 (percent of self or
        // px); outputs the raw trimmed components for the builder.
        // Anything else (rotate/scale/…) is unsupported.
        bool parse_translate(const std::string &s, std::string &tx,
                             std::string &ty)
        {
            const std::string t = css_trim(s);
            const std::string low = ascii_lower(t);
            const std::string fn = "translate";
            if (low.compare(0, fn.size(), fn) != 0)
            {
                return false;
            }
            std::string rest = css_trim(t.substr(fn.size()));
            if (rest.size() < 2 || rest.front() != '(' || rest.back() != ')')
            {
                return false;
            }
            rest = rest.substr(1, rest.size() - 2);
            if (rest.find('(') != std::string::npos)
            {
                return false;
            }
            const std::size_t comma = rest.find(',');
            tx = css_trim(rest.substr(0, comma));
            ty = comma == std::string::npos
                     ? std::string{}
                     : css_trim(rest.substr(comma + 1));
            return !tx.empty();
        }

        // border: "Npx solid <color>" (the color keeps inner spaces for
        // rgba()); any other style/grammar drops the border
        bool parse_border(const std::string &s, long long &w, std::string &color)
        {
            const std::string t = css_trim(s);
            const std::size_t s1 = t.find(' ');
            if (s1 == std::string::npos)
            {
                return false;
            }
            const std::string ws = ascii_lower(css_trim(t.substr(0, s1)));
            if (ws.size() < 3 || ws.compare(ws.size() - 2, 2, "px") != 0)
            {
                return false;
            }
            if (!parse_svg_num(ws.substr(0, ws.size() - 2), w) || w < 0)
            {
                return false;
            }
            const std::string rest = css_trim(t.substr(s1 + 1));
            const std::size_t s2 = rest.find(' ');
            if (s2 == std::string::npos)
            {
                return false;
            }
            if (ascii_lower(css_trim(rest.substr(0, s2))) != "solid")
            {
                return false;
            }
            color = css_trim(rest.substr(s2 + 1));
            return !color.empty();
        }
        // opacity (P-2d): "N" 0..1 or "N%" into fixed-point 0..1000
        // (integer-only: up to 3 fraction digits, truncated, clamped).
        // Malformed (empty, junk, bare ".") drops the declaration.
        bool parse_opacity_fixed(const std::string &s, long long &out)
        {
            std::string t = css_trim(ascii_lower(s));
            if (t.empty())
            {
                return false;
            }
            long long scale = 1000;
            if (!t.empty() && t.back() == '%')
            {
                t = css_trim(t.substr(0, t.size() - 1));
                scale = 10;
            }
            const std::size_t dot = t.find('.');
            std::string ip = dot == std::string::npos ? t : t.substr(0, dot);
            std::string fp = dot == std::string::npos ? "" : t.substr(dot + 1);
            if (ip.empty() && fp.empty())
            {
                return false;  // bare "." and friends
            }
            if (ip.empty())
            {
                ip = "0";
            }
            long long whole = 0;
            for (const char c : ip)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
                whole = whole * 10 + (c - '0');
                if (whole > 1000000)
                {
                    break;  // clamps below; no need for exact bigness
                }
            }
            while (fp.size() > 3)
            {
                fp.pop_back();
            }
            while (fp.size() < 3)
            {
                fp.push_back('0');
            }
            long long frac = 0;
            for (const char c : fp)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
                frac = frac * 10 + (c - '0');
            }
            long long v = 0;
            if (scale == 1000)
            {
                v = whole * 1000 + frac;  // "N" 0..1
            }
            else
            {
                v = whole * 10 + frac / 100;  // "N%" (frac thirds truncate)
            }
            out = v < 0 ? 0 : (v > 1000 ? 1000 : v);
            return true;
        }
        // whitespace split that keeps paren groups whole (spaced
        // rgba() colors survive as one token stream to rejoin)
        void split_ws_paren(const std::string &s, std::vector<std::string> &out)
        {
            std::string cur;
            int depth = 0;
            for (const char c : s)
            {
                if (c == '(')
                {
                    ++depth;
                }
                else if (c == ')' && depth > 0)
                {
                    --depth;
                }
                if ((c == ' ' || c == '\t') && depth == 0)
                {
                    if (!cur.empty())
                    {
                        out.push_back(cur);
                        cur.clear();
                    }
                }
                else
                {
                    cur.push_back(c);
                }
            }
            if (!cur.empty())
            {
                out.push_back(cur);
            }
        }
        // one shadow length: Npx or bare (signed); false = not a length
        bool parse_shadow_len(const std::string &tok, long long &v)
        {
            std::string t = css_trim(ascii_lower(tok));
            if (t.empty())
            {
                return false;
            }
            if (t.size() > 2 && t.compare(t.size() - 2, 2, "px") == 0)
            {
                t = t.substr(0, t.size() - 2);
            }
            return parse_svg_num(t, v);
        }
        // one box-shadow entry (P-2e): "[inset] ox oy [blur [spread]]
        // color". Lengths beyond the fourth start the color; a token
        // where a length belongs that is not one starts the color
        // early (so 2-length + color is the common case). False =
        // malformed, skip the entry only.
        bool parse_one_shadow(const std::string &s, bool &inset, long long &ox,
                              long long &oy, long long &blur, long long &spread,
                              std::string &color)
        {
            std::vector<std::string> tok;
            split_ws_paren(css_trim(s), tok);
            std::size_t k = 0;
            inset = false;
            if (k < tok.size() && ascii_lower(tok[k]) == "inset")
            {
                inset = true;
                ++k;
            }
            if (tok.size() - k < 3)
            {
                return false;
            }
            if (!parse_shadow_len(tok[k], ox) ||
                !parse_shadow_len(tok[k + 1], oy))
            {
                return false;
            }
            k += 2;
            blur = 0;
            spread = 0;
            long long v = 0;
            if (k < tok.size() && parse_shadow_len(tok[k], v))
            {
                blur = v < 0 ? 0 : v;
                ++k;
                if (k < tok.size() && parse_shadow_len(tok[k], v))
                {
                    spread = v < 0 ? 0 : v;
                    ++k;
                }
            }
            if (k >= tok.size())
            {
                return false;
            }
            color.clear();
            for (std::size_t i = k; i < tok.size(); ++i)
            {
                if (i > k)
                {
                    color.push_back(' ');
                }
                color += tok[i];
            }
            return !color.empty();
        }
        // text-shadow: "DXpx DYpx [blur] <color>" (a comma list keeps
        // the first shadow only, per contract P-2a). Lengths are Npx or
        // bare numbers; the blur (when 4 tokens) is parsed-and-ignored.
        bool parse_text_shadow(const std::string &s, long long &dx,
                               long long &dy, std::string &color)
        {
            // a comma list keeps the first shadow only — split
            // paren-aware so rgba() commas never divide the list
            std::vector<std::string> shadows;
            split_layers(s, shadows);
            if (shadows.empty())
            {
                return false;
            }
            std::string first = css_trim(shadows.front());
            const auto len_of = [](const std::string &tok, long long &v) {
                std::string t = css_trim(ascii_lower(tok));
                if (t.size() > 2 && t.compare(t.size() - 2, 2, "px") == 0)
                {
                    t = t.substr(0, t.size() - 2);
                }
                return parse_svg_num(t, v);
            };
            const std::size_t s1 = first.find(' ');
            if (s1 == std::string::npos || !len_of(first.substr(0, s1), dx))
            {
                return false;
            }
            const std::string rest = css_trim(first.substr(s1 + 1));
            const std::size_t s2 = rest.find(' ');
            if (s2 == std::string::npos || !len_of(rest.substr(0, s2), dy))
            {
                return false;
            }
            std::string tail = css_trim(rest.substr(s2 + 1));
            if (tail.empty())
            {
                return false;
            }
            // a fourth token is the blur radius: drop it, keep the color
            const std::size_t s3 = tail.find(' ');
            if (s3 != std::string::npos)
            {
                long long blur = 0;
                if (len_of(tail.substr(0, s3), blur))
                {
                    tail = css_trim(tail.substr(s3 + 1));
                }
            }
            color = tail;
            return !color.empty();
        }
        // aspect-ratio: "W / H", "W/H", or a bare number N (= N/1);
        // "auto" and malformed values stay absent
        bool parse_aspect(const std::string &s, long long &w, long long &h)
        {
            const auto trim = [](std::string v) {
                v.erase(0, v.find_first_not_of(" \t\n\r"));
                const std::size_t t = v.find_last_not_of(" \t\n\r");
                if (t == std::string::npos)
                {
                    return std::string{};
                }
                v.erase(t + 1);
                return v;
            };
            const std::string t = trim(ascii_lower(s));
            if (t.empty() || t == "auto")
            {
                return false;
            }
            const std::size_t slash = t.find('/');
            long long a = 0, b = 1;
            if (slash == std::string::npos)
            {
                if (!parse_svg_num(t, a) || a <= 0)
                {
                    return false;
                }
            }
            else
            {
                if (!parse_svg_num(trim(t.substr(0, slash)), a) || a <= 0 ||
                    !parse_svg_num(trim(t.substr(slash + 1)), b) || b <= 0)
                {
                    return false;
                }
            }
            w = a;
            h = b;
            return true;
        }

        // a CSS length: px -> pixels, % -> percent (1..100), "auto"/malformed
        // -> the axis stays measured (tolerance: silently not set).
        // B4: units and "auto" are ASCII case-insensitive (digits and %
        // are unaffected by the fold). Returns whether a prop was stored
        // (C7: lets br keep an explicit style height instead of appending
        // a shadowed duplicate).
        bool apply_length(ui_node &n, const std::string &prop,
                          const std::string &val)
        {
            const std::string v = ascii_lower(val);
            if (v == "auto")
            {
                return false;
            }
            if (v.size() >= 2 && v.back() == 'x' &&
                v[v.size() - 2] == 'p')
            {
                const long long px =
                    parse_int(v.substr(0, v.size() - 2), -1);
                if (px >= 0)
                {
                    n.prop(prop, px);
                    return true;
                }
                return false;
            }
            if (!v.empty() && v.back() == '%')
            {
                const long long pct =
                    parse_int(v.substr(0, v.size() - 1), -1);
                if (pct >= 1 && pct <= 100)
                {
                    n.prop(prop, std::to_string(pct) + "%");
                    return true;
                }
            }
            return false;
        }

        // -------------------------------------------------------------------
        // element -> shared ui_node (the .ui tag table keys)
        // -------------------------------------------------------------------

        std::string widget_type(const Elem &e, const std::vector<Decl> &folded)
        {
            if (e.tag == "div" || e.tag == "body")
            {
                if (const std::string *dir = fold_lookup(folded, "flex-direction"))
                {
                    if (ascii_lower(*dir) == "row")
                    {
                        return "row";
                    }
                    return "column";
                }
                // no direction: a bare display:flex takes the CSS flex
                // default (row); display:block and anything else stay column
                if (const std::string *disp = fold_lookup(folded, "display"))
                {
                    if (ascii_lower(*disp) == "flex")
                    {
                        return "row";
                    }
                }
                return "column";
            }
            if (e.tag == "p" || e.tag == "span" || e.tag == "label" ||
                e.tag == "small")
            {
                return "label";
            }
            if (e.tag == "br")
            {
                return "label";
            }
            if (e.tag == "meter")
            {
                return "progress_bar";
            }
            if (e.tag == "svg" || e.tag == "vectordial")
            {
                return "svg";
            }
            return e.tag;  // button/checkbox/radio/toggle/gauge/knob/trend
        }

        // SVG numbers: optional sign, digits, optional fraction; rounded
        // half away from zero into a clamped int (SVG authors write 2.5)
        bool parse_svg_num(const std::string &s, long long &out)
        {
            std::size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            {
                ++i;
            }
            bool neg = false;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
            {
                neg = s[i] == '-';
                ++i;
            }
            int64_t ip = 0;
            bool any = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            {
                any = true;
                ip = ip * 10 + (s[i] - '0');
                if (ip > 1000000000LL)
                {
                    return false;
                }
                ++i;
            }
            int64_t fp = 0, scale = 1;
            if (i < s.size() && s[i] == '.')
            {
                ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9' && scale < 1000000000LL)
                {
                    fp = fp * 10 + (s[i] - '0');
                    scale *= 10;
                    ++i;
                }
                while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                {
                    ++i;  // beyond precision: kept for validation only
                }
                any = true;
            }
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            {
                ++i;
            }
            if (!any || i != s.size())
            {
                return false;
            }
            int64_t v = ip + (fp * 2 >= scale ? 1 : 0);
            out = neg ? -v : v;
            return true;
        }

        // the parse_svg_num grammar, kept fractional: stroke widths and
        // svg font sizes scale with the viewBox, so rounding 2.5 to 3
        // here would visibly thicken every stroke at draw time
        bool parse_svg_double(const std::string &s, double &out)
        {
            std::size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            {
                ++i;
            }
            bool neg = false;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
            {
                neg = s[i] == '-';
                ++i;
            }
            int64_t ip = 0;
            bool any = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            {
                any = true;
                ip = ip * 10 + (s[i] - '0');
                if (ip > 1000000000LL)
                {
                    return false;
                }
                ++i;
            }
            int64_t fp = 0, scale = 1;
            if (i < s.size() && s[i] == '.')
            {
                ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9' && scale < 1000000000LL)
                {
                    fp = fp * 10 + (s[i] - '0');
                    scale *= 10;
                    ++i;
                }
                while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                {
                    ++i;
                }
                any = true;
            }
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            {
                ++i;
            }
            if (!any || i != s.size())
            {
                return false;
            }
            out = static_cast<double>(ip) + static_cast<double>(fp) / scale;
            out = neg ? -out : out;
            return true;
        }

        // opacity 0..1 (SVG-clamped) into an alpha byte; malformed rides
        // the tolerance default (fully opaque)
        int parse_svg_alpha(const std::string &s)
        {
            std::size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            {
                ++i;
            }
            bool neg = false;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
            {
                neg = s[i] == '-';
                ++i;
            }
            int64_t ip = 0;
            bool any = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            {
                any = true;
                ip = ip * 10 + (s[i] - '0');
                if (ip > 1)
                {
                    break;  // clamps to 1 below; stop accumulating
                }
                ++i;
            }
            int64_t fp = 0, scale = 1;
            if (i < s.size() && s[i] == '.')
            {
                ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9' && scale < 1000000000LL)
                {
                    fp = fp * 10 + (s[i] - '0');
                    scale *= 10;
                    ++i;
                }
                while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                {
                    ++i;
                }
                any = true;
            }
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            {
                ++i;
            }
            if (!any || i != s.size())
            {
                return 255;
            }
            if (neg || (ip == 0 && fp == 0))
            {
                return 0;
            }
            if (ip >= 1)
            {
                return 255;
            }
            return static_cast<int>((fp * 255 + scale / 2) / scale);
        }

        // viewBox splitter: spaces, tabs, newlines and commas separate
        bool split_view_box(const std::string &s, long long out[4])
        {
            long long v[4] = {0, 0, 0, 0};
            int n = 0;
            std::size_t i = 0;
            while (i < s.size() && n < 4)
            {
                while (i < s.size() && (s[i] == ' ' || s[i] == '\t' ||
                                        s[i] == '\n' || s[i] == '\r' ||
                                        s[i] == ','))
                {
                    ++i;
                }
                if (i >= s.size())
                {
                    break;
                }
                std::size_t j = i;
                while (j < s.size() && s[j] != ' ' && s[j] != '\t' &&
                       s[j] != '\n' && s[j] != '\r' && s[j] != ',')
                {
                    ++j;
                }
                if (!parse_svg_num(s.substr(i, j - i), v[n]))
                {
                    return false;
                }
                ++n;
                i = j;
            }
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' ||
                                    s[i] == '\n' || s[i] == '\r' ||
                                    s[i] == ','))
            {
                ++i;
            }
            if (n != 4 || i != s.size())
            {
                return false;
            }
            for (int k = 0; k < 4; ++k)
            {
                out[k] = v[k];
            }
            return true;
        }

        // one svg stroke child into the shared node form; silently dropped
        // when its geometry is malformed (the shared tolerance)
        void convert_svg_line(ui_node &n, const Elem &c)
        {
            long long x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            bool geo = true;
            for (const auto &a : c.attrs)
            {
                long long v = 0;
                if (a.first == "x1" || a.first == "y1" ||
                    a.first == "x2" || a.first == "y2")
                {
                    if (!parse_svg_num(a.second, v))
                    {
                        geo = false;
                        break;
                    }
                    if (a.first == "x1") x1 = v;
                    else if (a.first == "y1") y1 = v;
                    else if (a.first == "x2") x2 = v;
                    else y2 = v;
                }
            }
            if (!geo)
            {
                return;
            }
            const std::string &stroke = c.attr("stroke");
            if (stroke.empty())
            {
                return;  // SVG default: no stroke = invisible
            }
            double width = 1.0;
            for (const auto &a : c.attrs)
            {
                if (a.first == "stroke-width")
                {
                    // fractional on purpose: the width rides the
                    // viewBox scale at draw time
                    if (!parse_svg_double(a.second, width) || width <= 0)
                    {
                        return;  // width 0 = invisible; malformed = dropped
                    }
                }
            }
            ui_node l;
            l.type = "svg_line";
            l.prop("x1", x1).prop("y1", y1).prop("x2", x2).prop("y2", y2);
            l.prop("stroke", stroke);
            l.prop("stroke_w", width);
            if (ascii_lower(c.attr("stroke-linecap")) == "round")
            {
                l.prop("stroke_round", true);
            }
            const std::string &op = c.attr("opacity");
            l.prop("stroke_alpha", op.empty() ? 255LL
                                              : static_cast<long long>(parse_svg_alpha(op)));
            n.children.push_back(std::move(l));
        }

        void convert_svg_text(ui_node &n, const Elem &c)
        {
            if (c.text.empty())
            {
                return;
            }
            long long x = 0, y = 0;
            for (const auto &a : c.attrs)
            {
                long long v = 0;
                if (a.first == "x" || a.first == "y")
                {
                    if (!parse_svg_num(a.second, v))
                    {
                        return;
                    }
                    if (a.first == "x") x = v;
                    else y = v;
                }
            }
            ui_node t;
            t.type = "svg_text";
            t.prop("x", x).prop("y", y);
            t.prop("text", c.text);
            const std::string &fill = c.attr("fill");
            if (!fill.empty())
            {
                t.prop("fill", fill);
            }
            const std::string &op = c.attr("opacity");
            t.prop("fill_alpha", op.empty() ? 255LL
                                            : static_cast<long long>(parse_svg_alpha(op)));
            int anchor = 0;
            const std::string an = ascii_lower(c.attr("text-anchor"));
            if (an == "middle")
            {
                anchor = 1;
            }
            else if (an == "end")
            {
                anchor = 2;
            }
            t.prop("anchor", static_cast<long long>(anchor));
            // svg font-size in viewBox units, kept fractional (it scales
            // with the viewBox at draw time; malformed drops to the
            // seam default like the other tolerant attributes)
            double fs = 0.0;
            const std::string &fsv = c.attr("font-size");
            if (!fsv.empty() && parse_svg_double(fsv, fs) && fs > 0)
            {
                t.prop("text_fs", fs);
            }
            n.children.push_back(std::move(t));
        }

        // --- svg path data (H-6 first cut) ---------------------------------
        // Stroke-only `d` parsing plus adaptive flattening into
        // per-subpath polylines (code-contract §3.3). Commands M m L l
        // H h V v C c S s Q q T t Z z with the SVG grammar (implicit
        // repeats, relative forms); arcs drop the whole path with one
        // warning. Flattening is plain IEEE double with a fixed chord
        // tolerance and depth cap, so every platform flattens alike.

        constexpr double kPathTol = 0.1;      // viewBox-unit chord tolerance
        constexpr int kPathMaxDepth = 12;     // <= 4096 emits per curve

        void flatten_cubic(std::vector<std::pair<double, double>> &out,
                           const double x0, const double y0,
                           const double x1, const double y1,
                           const double x2, const double y2,
                           const double x3, const double y3,
                           const int depth)
        {
            // flatness without sqrt: the control points' squared
            // distance to the chord vs the tolerance (all doubles, no
            // overflow below any real coordinate magnitude)
            const double dx = x3 - x0;
            const double dy = y3 - y0;
            const double len2 = dx * dx + dy * dy;
            const double tol2 = kPathTol * kPathTol;
            bool flat = len2 <= tol2;
            if (!flat)
            {
                const double c1 = (x1 - x0) * dy - (y1 - y0) * dx;
                const double c2 = (x2 - x0) * dy - (y2 - y0) * dx;
                flat = c1 * c1 <= tol2 * len2 && c2 * c2 <= tol2 * len2;
            }
            if (flat || depth >= kPathMaxDepth)
            {
                out.emplace_back(x3, y3);
                return;
            }
            const double x01 = (x0 + x1) / 2, y01 = (y0 + y1) / 2;
            const double x12 = (x1 + x2) / 2, y12 = (y1 + y2) / 2;
            const double x23 = (x2 + x3) / 2, y23 = (y2 + y3) / 2;
            const double xa = (x01 + x12) / 2, ya = (y01 + y12) / 2;
            const double xb = (x12 + x23) / 2, yb = (y12 + y23) / 2;
            const double xm = (xa + xb) / 2, ym = (ya + yb) / 2;
            flatten_cubic(out, x0, y0, x01, y01, xa, ya, xm, ym, depth + 1);
            flatten_cubic(out, xm, ym, xb, yb, x23, y23, x3, y3, depth + 1);
        }

        // one polyline per subpath, closed flag from `Z`
        struct PathSub
        {
            std::vector<std::pair<double, double>> pts;
            bool closed = false;
        };

        bool convert_path_d(const std::string &d, std::vector<PathSub> &out)
        {
            const char *p = d.c_str();
            const char *const end = p + d.size();
            const auto skip_sep = [&p, end] {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                                   *p == '\r' || *p == ','))
                {
                    ++p;
                }
            };
            const auto read_num = [&](double &v) {
                skip_sep();
                const char *b = p;
                if (p < end && (*p == '+' || *p == '-'))
                {
                    ++p;
                }
                bool digits = false;
                while (p < end && *p >= '0' && *p <= '9')
                {
                    ++p;
                    digits = true;
                }
                if (p < end && *p == '.')
                {
                    ++p;
                    while (p < end && *p >= '0' && *p <= '9')
                    {
                        ++p;
                        digits = true;
                    }
                }
                if (!digits)
                {
                    p = b;
                    return false;
                }
                v = std::strtod(b, nullptr);
                return true;
            };
            double cx = 0, cy = 0;       // current point
            double sx = 0, sy = 0;       // subpath start
            double lc_x = 0, lc_y = 0;   // last cubic control (S reflection)
            double lq_x = 0, lq_y = 0;   // last quadratic control (T)
            char cmd = 0;                // last command letter
            char prev = 0;               // command before it (reflection rule)
            PathSub *cur = nullptr;
            const auto open_at = [&]() -> PathSub * {
                out.emplace_back();
                out.back().pts.emplace_back(cx, cy);
                return &out.back();
            };
            while (true)
            {
                skip_sep();
                if (p >= end)
                {
                    break;
                }
                if (*p == '+' || *p == '-' || *p == '.' ||
                    (*p >= '0' && *p <= '9'))
                {
                    // implicit repeat: keep cmd (M already rerouted to L)
                }
                else if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
                {
                    prev = cmd;
                    cmd = *p++;
                }
                else
                {
                    return false;
                }
                const bool rel = cmd >= 'a' && cmd <= 'z' && cmd != 'z';
                switch (cmd)
                {
                case 'M':
                case 'm':
                {
                    double x, y;
                    if (!read_num(x) || !read_num(y))
                    {
                        return false;
                    }
                    cur = nullptr;  // any open subpath ends here
                    cx = rel ? cx + x : x;
                    cy = rel ? cy + y : y;
                    sx = cx;
                    sy = cy;
                    out.emplace_back();
                    out.back().pts.emplace_back(cx, cy);
                    cur = &out.back();
                    // extra pairs are implicit line-tos (SVG rule)
                    cmd = rel ? 'l' : 'L';
                    break;
                }
                case 'L':
                case 'l':
                {
                    double x, y;
                    if (!read_num(x) || !read_num(y))
                    {
                        return false;
                    }
                    if (cur == nullptr)
                    {
                        cur = open_at();
                    }
                    cx = rel ? cx + x : x;
                    cy = rel ? cy + y : y;
                    cur->pts.emplace_back(cx, cy);
                    break;
                }
                case 'H':
                case 'h':
                {
                    double x;
                    if (!read_num(x))
                    {
                        return false;
                    }
                    if (cur == nullptr)
                    {
                        cur = open_at();
                    }
                    cx = rel ? cx + x : x;
                    cur->pts.emplace_back(cx, cy);
                    break;
                }
                case 'V':
                case 'v':
                {
                    double y;
                    if (!read_num(y))
                    {
                        return false;
                    }
                    if (cur == nullptr)
                    {
                        cur = open_at();
                    }
                    cy = rel ? cy + y : y;
                    cur->pts.emplace_back(cx, cy);
                    break;
                }
                case 'C':
                case 'c':
                {
                    double x1, y1, x2, y2, x, y;
                    if (!read_num(x1) || !read_num(y1) ||
                        !read_num(x2) || !read_num(y2) ||
                        !read_num(x) || !read_num(y))
                    {
                        return false;
                    }
                    if (cur == nullptr)
                    {
                        cur = open_at();
                    }
                    if (rel)
                    {
                        x1 += cx; y1 += cy;
                        x2 += cx; y2 += cy;
                        x += cx; y += cy;
                    }
                    flatten_cubic(cur->pts, cx, cy, x1, y1, x2, y2, x, y, 0);
                    lc_x = x2;
                    lc_y = y2;
                    cx = x;
                    cy = y;
                    break;
                }
                case 'S':
                case 's':
                {
                    double x2, y2, x, y;
                    if (!read_num(x2) || !read_num(y2) ||
                        !read_num(x) || !read_num(y))
                    {
                        return false;
                    }
                    if (cur == nullptr)
                    {
                        cur = open_at();
                    }
                    double rx = cx, ry = cy;  // reflection of the last
                    if (prev == 'C' || prev == 'S')  // cubic control
                    {
                        rx = 2 * cx - lc_x;
                        ry = 2 * cy - lc_y;
                    }
                    if (rel)
                    {
                        x2 += cx; y2 += cy;
                        x += cx; y += cy;
                    }
                    flatten_cubic(cur->pts, cx, cy, rx, ry, x2, y2, x, y, 0);
                    lc_x = x2;
                    lc_y = y2;
                    cx = x;
                    cy = y;
                    break;
                }
                case 'Q':
                case 'q':
                {
                    double x1, y1, x, y;
                    if (!read_num(x1) || !read_num(y1) ||
                        !read_num(x) || !read_num(y))
                    {
                        return false;
                    }
                    if (cur == nullptr)
                    {
                        cur = open_at();
                    }
                    if (rel)
                    {
                        x1 += cx; y1 += cy;
                        x += cx; y += cy;
                    }
                    // quadratic -> cubic elevation, one flattener
                    const double c1x = cx + 2.0 / 3 * (x1 - cx);
                    const double c1y = cy + 2.0 / 3 * (y1 - cy);
                    const double c2x = x + 2.0 / 3 * (x1 - x);
                    const double c2y = y + 2.0 / 3 * (y1 - y);
                    flatten_cubic(cur->pts, cx, cy, c1x, c1y, c2x, c2y,
                                  x, y, 0);
                    lq_x = x1;
                    lq_y = y1;
                    cx = x;
                    cy = y;
                    break;
                }
                case 'T':
                case 't':
                {
                    double x, y;
                    if (!read_num(x) || !read_num(y))
                    {
                        return false;
                    }
                    if (cur == nullptr)
                    {
                        cur = open_at();
                    }
                    double rx = cx, ry = cy;
                    if (prev == 'Q' || prev == 'T')
                    {
                        rx = 2 * cx - lq_x;
                        ry = 2 * cy - lq_y;
                    }
                    if (rel)
                    {
                        x += cx; y += cy;
                    }
                    const double c1x = cx + 2.0 / 3 * (rx - cx);
                    const double c1y = cy + 2.0 / 3 * (ry - cy);
                    const double c2x = x + 2.0 / 3 * (rx - x);
                    const double c2y = y + 2.0 / 3 * (ry - y);
                    flatten_cubic(cur->pts, cx, cy, c1x, c1y, c2x, c2y,
                                  x, y, 0);
                    lq_x = rx;
                    lq_y = ry;
                    cx = x;
                    cy = y;
                    break;
                }
                case 'Z':
                case 'z':
                {
                    if (cur != nullptr)
                    {
                        cur->closed = true;
                        cur = nullptr;
                    }
                    cx = sx;
                    cy = sy;
                    break;
                }
                default:
                    return false;  // arcs and anything else: unsupported
                }
            }
            return true;
        }

        // one svg path element into per-subpath svg_path nodes
        // (silently dropped when strokeless, one warning when the data
        // is outside the supported grammar — the shared tolerance)
        void convert_svg_path(ui_node &n, const Elem &c)
        {
            const std::string &stroke = c.attr("stroke");
            if (stroke.empty())
            {
                return;  // SVG default: no stroke = invisible
            }
            const std::string &d = c.attr("d");
            std::vector<PathSub> subs;
            if (d.empty() || !convert_path_d(d, subs))
            {
                LW << "html: line " << c.line << ": <path> d data is not in "
                      "the supported subset (M L H V C S Q T Z); dropped";
                return;
            }
            double width = 1.0;
            for (const auto &a : c.attrs)
            {
                if (a.first == "stroke-width")
                {
                    if (!parse_svg_double(a.second, width) || width <= 0)
                    {
                        return;
                    }
                }
            }
            const bool round = ascii_lower(c.attr("stroke-linecap")) == "round";
            const std::string &op = c.attr("opacity");
            const long long alpha =
                op.empty() ? 255LL
                           : static_cast<long long>(parse_svg_alpha(op));
            for (const PathSub &sub : subs)
            {
                if (sub.pts.empty())
                {
                    continue;
                }
                ui_node pn;
                pn.type = "svg_path";
                std::string pts;
                const auto fmt = [](const double v) {
                    char buf[40];
                    std::snprintf(buf, sizeof buf, "%.3f", v);
                    std::string tok = buf;
                    // trim trailing zeros: "18.500" -> "18.5",
                    // "104.000" -> "104" (round-trips through the
                    // materialize parser either way)
                    const auto dot = tok.find('.');
                    if (dot != std::string::npos)
                    {
                        std::size_t last = tok.size();
                        while (last > dot + 1 && tok[last - 1] == '0')
                        {
                            --last;
                        }
                        if (tok[last - 1] == '.')
                        {
                            --last;
                        }
                        tok.resize(last);
                    }
                    return tok;
                };
                for (const auto &pt : sub.pts)
                {
                    if (!pts.empty())
                    {
                        pts += ' ';
                    }
                    pts += fmt(pt.first);
                    pts += ',';
                    pts += fmt(pt.second);
                }
                pn.prop("pts", std::move(pts));
                pn.prop("stroke", stroke);
                pn.prop("stroke_w", width);
                if (round)
                {
                    pn.prop("stroke_round", true);
                }
                pn.prop("closed", sub.closed);
                pn.prop("stroke_alpha", alpha);
                n.children.push_back(std::move(pn));
            }
        }

        // H-10 pseudo-element box (narrow subset): folds the ::before
        // (kind 1) or ::after (kind 2) rules for the element and lands
        // before_*/after_* paint props on its node (a paint-only box —
        // no layout, no hit-testing). Supported: content:"" only
        // (anything else generates no box); position:absolute
        // required; width/height/left/top/right/bottom, margin
        // longhands (fractional px snap half-away-from-zero via
        // parse_svg_num, negatives drop like the shared tolerance);
        // single-layer background (any supported form, copied with
        // the paint prefix); Npx/50% border-radius; bare rotate()
        // transform (translate() ignored: generated boxes resolve
        // without layout flow); transform-origin as 1-2 %/px/keyword
        // tokens (default 50%). Anything richer is ignored with one
        // warning. Lengths reuse the shared parsers, so boxes resolve
        // like real elements at paint time.
        bool convert_pseudo(const int line, const int kind, const Elem &e,
                            const Rules &rules, const VarMap &vars,
                            const std::vector<Ancestor> &ancestors,
                            ui_node &n)
        {
            std::vector<Decl> folded;
            fold_pseudo(e, rules, vars, ancestors, kind, folded);
            if (folded.empty())
            {
                return false;
            }
            const char *const pname = kind == 1 ? "::before" : "::after";
            const std::string pre = kind == 1 ? "before_" : "after_";
            const std::string *co = fold_lookup(folded, "content");
            if (co == nullptr)
            {
                return false;
            }
            {
                const std::string t = css_trim(*co);
                if (t != "\"\"" && t != "''")
                {
                    LW << "html: line " << line << ": " << pname
                       << " content is not \"\"; no box generated (H-10)";
                    return false;
                }
            }
            if (const std::string *d = fold_lookup(folded, "display"))
            {
                if (ascii_lower(*d) == "none")
                {
                    return false;
                }
            }
            bool absolute = false;
            if (const std::string *ps = fold_lookup(folded, "position"))
            {
                absolute = css_trim(ascii_lower(*ps)) == "absolute";
            }
            if (!absolute)
            {
                LW << "html: line " << line << ": " << pname
                   << " is not absolutely positioned; no box (H-10)";
                return false;
            }
            if (const std::string *w = fold_lookup(folded, "width"))
            {
                apply_length(n, pre + "w", *w);
            }
            if (const std::string *h = fold_lookup(folded, "height"))
            {
                apply_length(n, pre + "h", *h);
            }
            const char *const sides[4] = {"left", "top", "right", "bottom"};
            const char *const side_keys[4] = {"l", "t", "r", "b"};
            for (int si = 0; si < 4; ++si)
            {
                if (const std::string *sv = fold_lookup(folded, sides[si]))
                {
                    const std::string t = css_trim(*sv);
                    if (!t.empty() && ascii_lower(t) != "auto")
                    {
                        n.prop(pre + side_keys[si], t);
                    }
                }
            }
            // margin longhands snap to whole px (halves away from zero);
            // negatives fall out like the shared tolerance, silently:
            // pseudo boxes never reach the shared margin warning
            const char *const mlong[4] = {"margin-left", "margin-top",
                                          "margin-right", "margin-bottom"};
            const char *const mkeys[4] = {"ml", "mt", "mr", "mb"};
            for (int mi = 0; mi < 4; ++mi)
            {
                if (const std::string *mv = fold_lookup(folded, mlong[mi]))
                {
                    std::string t = css_trim(ascii_lower(*mv));
                    if (t.size() > 2 &&
                        t.compare(t.size() - 2, 2, "px") == 0)
                    {
                        t = t.substr(0, t.size() - 2);
                    }
                    long long v = 0;
                    if (parse_svg_num(t, v) && v >= 0)
                    {
                        n.prop(pre + mkeys[mi], v);
                    }
                }
            }
            // single-layer background only; multi-layer drops the paint
            if (const std::string *bg = fold_lookup(folded, "background"))
            {
                std::vector<std::string> layers;
                split_layers(*bg, layers);
                if (layers.size() == 1)
                {
                    ui_node tmp;
                    if (parse_bg_layer(tmp, layers[0]))
                    {
                        for (const auto &p : tmp.props)
                        {
                            n.prop(pre + p.first, p.second);
                        }
                    }
                }
                else
                {
                    LW << "html: line " << line << ": " << pname
                       << " multi-layer background ignored (H-10)";
                }
            }
            else if (const std::string *bc =
                         fold_lookup(folded, "background-color"))
            {
                n.prop(pre + "background", *bc);
            }
            if (const std::string *br = fold_lookup(folded, "border-radius"))
            {
                const std::string t = css_trim(ascii_lower(*br));
                if (t == "50%")
                {
                    n.prop(pre + "radius_half", true);
                }
                else if (t.size() > 2 &&
                         t.compare(t.size() - 2, 2, "px") == 0)
                {
                    long long px = 0;
                    if (parse_svg_num(t.substr(0, t.size() - 2), px) &&
                        px >= 0)
                    {
                        n.prop(pre + "radius_px", px);
                    }
                }
            }
            // transform: one bare rotate(Ndeg); translate() is
            // ignored (generated boxes resolve without layout flow);
            // anything combined drops the whole transform
            if (const std::string *tf = fold_lookup(folded, "transform"))
            {
                std::string tx;
                std::string ty;
                if (parse_translate(*tf, tx, ty))
                {
                    LW << "html: line " << line << ": " << pname
                       << " translate() ignored (H-10)";
                }
                else
                {
                    const std::string t = css_trim(ascii_lower(*tf));
                    long long deg = 0;
                    if (t.size() > 11 &&
                        t.compare(0, 7, "rotate(") == 0 &&
                        t.back() == ')' &&
                        t.compare(t.size() - 4, 3, "deg") == 0 &&
                        parse_svg_num(t.substr(7, t.size() - 7 - 4), deg))
                    {
                        n.prop(pre + "rot_ang", deg);
                    }
                    else
                    {
                        LW << "html: line " << line << ": " << pname
                           << " transform '" << *tf
                           << "' is not translate()/rotate(); ignored";
                    }
                }
            }
            // transform-origin: 1-2 tokens of N%/Npx/bare/keyword
            // (default 50%); keywords map to 0/50/100%
            if (const std::string *to = fold_lookup(folded, "transform-origin"))
            {
                std::vector<std::string> toks;
                std::string cur;
                for (const char c : css_trim(ascii_lower(*to)))
                {
                    if (c == ' ' || c == '\t')
                    {
                        if (!cur.empty())
                        {
                            toks.push_back(cur);
                            cur.clear();
                        }
                    }
                    else
                    {
                        cur.push_back(c);
                    }
                }
                if (!cur.empty())
                {
                    toks.push_back(cur);
                }
                if (!toks.empty() && toks.size() <= 2)
                {
                    bool clean = true;
                    for (std::size_t ti = 0; clean && ti < 2; ++ti)
                    {
                        const std::string tok =
                            ti < toks.size() ? toks[ti] : "50%";
                        long long v = 50;
                        bool pct = true;
                        if (tok == "left" || tok == "top")
                        {
                            v = 0;
                        }
                        else if (tok == "center")
                        {
                            v = 50;
                        }
                        else if (tok == "right" || tok == "bottom")
                        {
                            v = 100;
                        }
                        else
                        {
                            std::string u = tok;
                            pct = false;
                            if (!u.empty() && u.back() == '%')
                            {
                                pct = true;
                                u.pop_back();
                            }
                            else if (u.size() > 2 &&
                                     u.compare(u.size() - 2, 2, "px") == 0)
                            {
                                u = u.substr(0, u.size() - 2);
                            }
                            if (!parse_svg_num(u, v))
                            {
                                clean = false;
                            }
                        }
                        if (clean)
                        {
                            n.prop(pre + (ti == 0 ? "rot_ox" : "rot_oy"), v);
                            n.prop(pre + (ti == 0 ? "rot_ox_pct" : "rot_oy_pct"),
                                   pct);
                        }
                    }
                    if (!clean)
                    {
                        LW << "html: line " << line << ": " << pname
                           << " transform-origin '" << *to << "' ignored";
                    }
                }
                else if (!toks.empty())
                {
                    LW << "html: line " << line << ": " << pname
                       << " transform-origin '" << *to << "' ignored";
                }
            }
            return true;
        }

        // convert_elem is split so the recursive path keeps a small
        // stack frame: the NDS DTCM user stack is only ~16KB and a
        // monolithic convert_elem (~11KB static frame) overflowed at
        // depth >= 2. Each helper runs and returns before the child
        // recursion, so only one helper frame is live at a time.

        void convert_elem_attrs(ui_node &n, const Elem &e)
        {
            // element attributes (the whitelist in docs/html-path.md)
            for (const auto &a : e.attrs)
            {
                const std::string &k = a.first;
                if (k == "id" && n.id.empty())
                {
                    n.id = a.second;
                }
                else if (k == "checked" &&
                         (n.type == "checkbox" || n.type == "radio" ||
                          n.type == "toggle"))
                {
                    n.prop("checked", true);
                }
                else if (k == "group" && n.type == "radio")
                {
                    // B3: a group id is equality-matched (never indexed),
                    // so any sign is a valid id
                    long long g = 0;
                    if (parse_int_value(a.second, g))
                    {
                        n.prop("group", g);
                    }
                }
                else if ((k == "min" || k == "max" || k == "step" ||
                          k == "value") &&
                         (n.type == "gauge" || n.type == "knob" ||
                          n.type == "progress_bar"))
                {
                    // B3: the full integer grammar (negatives included)
                    // reaches the widgets, which clamp value into
                    // [min, max] and collapse a reversed range to a point
                    // themselves (locked by their suites); step stays a
                    // non-negative magnitude
                    long long v = 0;
                    if (parse_int_value(a.second, v) &&
                        (k != "step" || v >= 0))
                    {
                        n.prop(k, v);
                    }
                }
            }

            if (n.type == "label" || n.type == "button" ||
                n.type == "checkbox" || n.type == "radio")
            {
                if (!e.text.empty())
                {
                    n.prop("text", e.text);
                }
            }

        }

        // returns styled_height (a styled height wins over the br spacer)
        bool convert_elem_box(ui_node &n, const Elem &e,
                              const std::vector<Decl> &folded)
        {
            // the CSS subset (known properties from the whitelist)
            if (const std::string *d = fold_lookup(folded, "display"))
            {
                if (ascii_lower(*d) == "none")
                {
                    n.prop("visible", false);
                }
            }
            if (const std::string *w = fold_lookup(folded, "width"))
            {
                apply_length(n, "width", *w);
            }
            // C7: a styled br height wins on its own; the spacer default
            // below only fills an absent axis (no shadowed duplicate)
            bool styled_height = false;
            if (const std::string *h = fold_lookup(folded, "height"))
            {
                styled_height = apply_length(n, "height", *h);
            }
            if (const std::string *ar = fold_lookup(folded, "aspect-ratio"))
            {
                long long aw = 0, ah = 0;
                if (parse_aspect(*ar, aw, ah))
                {
                    n.prop("aspect_w", aw).prop("aspect_h", ah);
                }
            }
            // H-3 margins: 1-4 bare/Npx values with the CSS side
            // mapping, plus per-side longhands that win over it.
            // Negative/auto/malformed warn once and keep 0.
            if (const std::string *mg = fold_lookup(folded, "margin"))
            {
                std::vector<std::string> toks;
                std::string cur;
                for (const char c : css_trim(*mg))
                {
                    if (c == ' ' || c == '\t')
                    {
                        if (!cur.empty())
                        {
                            toks.push_back(cur);
                            cur.clear();
                        }
                    }
                    else
                    {
                        cur.push_back(c);
                    }
                }
                if (!cur.empty())
                {
                    toks.push_back(cur);
                }
                long long v[4] = {0, 0, 0, 0};
                bool clean = !toks.empty() && toks.size() <= 4;
                for (std::size_t ti = 0; clean && ti < toks.size(); ++ti)
                {
                    long long s = 0;
                    clean = parse_gap_value(toks[ti], s);
                    if (clean)
                    {
                        v[ti] = s;
                    }
                }
                if (clean)
                {
                    // CSS side mapping: [all] [v/h] [t/h/b] [t/r/b/l].
                    // Sides with a longhand present are skipped here
                    // (the background-shorthand precedent: the loser
                    // must not emit, prop_of reads the first entry).
                    const long long v0 = v[0];
                    const long long v1 = toks.size() == 1 ? v[0] : v[1];
                    const long long v2 = toks.size() >= 3 ? v[2] : v[0];
                    const long long v3 = toks.size() >= 4 ? v[3] : v1;
                    const long long sides[4] = {v0, v1, v2, v3};
                    const char *const side_props[4] = {"margin_t", "margin_r",
                                                       "margin_b", "margin_l"};
                    const char *const side_long[4] = {"margin-top", "margin-right",
                                                      "margin-bottom", "margin-left"};
                    for (int si = 0; si < 4; ++si)
                    {
                        if (fold_lookup(folded, side_long[si]) == nullptr)
                        {
                            n.prop(side_props[si], sides[si]);
                        }
                    }
                }
                else
                {
                    LW << "html: line " << e.line << ": unsupported margin '"
                       << *mg << "'";
                }
            }
            const char *const margin_sides[4] = {"margin-top", "margin-right",
                                                 "margin-bottom", "margin-left"};
            const char *const margin_props[4] = {"margin_t", "margin_r", "margin_b",
                                                 "margin_l"};
            for (int mi = 0; mi < 4; ++mi)
            {
                if (const std::string *mv = fold_lookup(folded, margin_sides[mi]))
                {
                    long long s = 0;
                    if (parse_gap_value(*mv, s))
                    {
                        n.prop(margin_props[mi], s);
                    }
                    else
                    {
                        LW << "html: line " << e.line << ": unsupported '"
                           << margin_sides[mi] << ": " << *mv << "'";
                    }
                }
            }
            // H-7c: flex-basis (auto = demand, Npx, N%), flex-shrink (int),
            // flex-grow (int), and the flex: shorthand (1/2/3 tokens +
            // none). Malformed values warn once and keep the current
            // values, like the H-7a/H-7b channels above.
            if (const std::string *fx = fold_lookup(folded, "flex"))
            {
                if (!parse_flex_shorthand(n, *fx))
                {
                    LW << "html: line " << e.line << ": unsupported flex '"
                       << *fx << "'";
                }
            }
            else
            {
                if (const std::string *fb = fold_lookup(folded, "flex-basis"))
                {
                    if (!parse_flex_basis(n, *fb))
                    {
                        LW << "html: line " << e.line
                           << ": unsupported flex-basis '" << *fb << "'";
                    }
                }
                if (const std::string *fs = fold_lookup(folded, "flex-shrink"))
                {
                    const long long s = parse_int(*fs, -1);
                    if (s >= 0)
                    {
                        n.flex_shrink = static_cast<int>(s);
                    }
                    else
                    {
                        LW << "html: line " << e.line
                           << ": unsupported flex-shrink '" << *fs << "'";
                    }
                }
            }
            if (const std::string *fg = fold_lookup(folded, "flex-grow"))
            {
                const long long g = parse_int(*fg, -1);
                if (g >= 0)
                {
                    n.flex_grow = static_cast<int>(g);
                }
                else
                {
                    LW << "html: line " << e.line << ": unsupported flex-grow '"
                       << *fg << "'";
                }
            }
            const bool is_container = n.type == "column" || n.type == "row";
            if (is_container)
            {
                if (const std::string *gv = fold_lookup(folded, "gap"))
                {
                    long long s = 0;
                    if (parse_gap_value(*gv, s))
                    {
                        n.prop("spacing", s);
                    }
                }
                if (const std::string *pv = fold_lookup(folded, "padding"))
                {
                    // 1-4 bare/Npx values with the CSS side mapping
                    // (H-3 margin precedent): one value keeps the legacy
                    // uniform "padding" prop, more land per-side so the
                    // FlexPanel content box follows the shorthand
                    std::vector<std::string> toks;
                    std::string cur;
                    for (const char c : css_trim(*pv))
                    {
                        if (c == ' ' || c == '\t')
                        {
                            if (!cur.empty())
                            {
                                toks.push_back(cur);
                                cur.clear();
                            }
                        }
                        else
                        {
                            cur.push_back(c);
                        }
                    }
                    if (!cur.empty())
                    {
                        toks.push_back(cur);
                    }
                    long long v[4] = {0, 0, 0, 0};
                    bool clean = !toks.empty() && toks.size() <= 4;
                    for (std::size_t ti = 0; clean && ti < toks.size(); ++ti)
                    {
                        long long s = 0;
                        clean = parse_gap_value(toks[ti], s);
                        if (clean)
                        {
                            v[ti] = s;
                        }
                    }
                    if (clean)
                    {
                        if (toks.size() == 1)
                        {
                            n.prop("padding", v[0]);
                        }
                        else
                        {
                            // [t|lr] / [t|h|b] / [t|r|b|l]; sides with a
                            // longhand present are skipped there (the
                            // margin shorthand precedent)
                            const long long v0 = v[0];
                            const long long v1 = v[1];
                            const long long v2 = toks.size() >= 3 ? v[2] : v[0];
                            const long long v3 = toks.size() >= 4 ? v[3] : v1;
                            const long long sides[4] = {v0, v1, v2, v3};
                            const char *const side_props[4] = {
                                "padding_t", "padding_r", "padding_b",
                                "padding_l"};
                            const char *const side_long[4] = {
                                "padding-top", "padding-right",
                                "padding-bottom", "padding-left"};
                            for (int si = 0; si < 4; ++si)
                            {
                                if (fold_lookup(folded, side_long[si]) ==
                                    nullptr)
                                {
                                    n.prop(side_props[si], sides[si]);
                                }
                            }
                        }
                    }
                    else
                    {
                        LW << "html: line " << e.line << ": unsupported padding '"
                           << *pv << "'";
                    }
                }
                const char *const padding_sides[4] = {
                    "padding-top", "padding-right", "padding-bottom",
                    "padding-left"};
                const char *const padding_props[4] = {
                    "padding_t", "padding_r", "padding_b", "padding_l"};
                for (int pi = 0; pi < 4; ++pi)
                {
                    if (const std::string *pv2 =
                            fold_lookup(folded, padding_sides[pi]))
                    {
                        long long s = 0;
                        if (parse_gap_value(*pv2, s))
                        {
                            n.prop(padding_props[pi], s);
                        }
                        else
                        {
                            LW << "html: line " << e.line << ": unsupported '"
                               << padding_sides[pi] << ": " << *pv2 << "'";
                        }
                    }
                }
                if (const std::string *wv = fold_lookup(folded, "flex-wrap"))
                {
                    if (ascii_lower(*wv) == "wrap")
                    {
                        n.prop("wrap", true);
                    }
                }
                // H-7a main-axis justification: unknown values warn once
                // and keep the current value (never the declaration)
                if (const std::string *jv = fold_lookup(folded, "justify-content"))
                {
                    const std::string j = ascii_lower(*jv);
                    long long code = -1;
                    if (j == "flex-start" || j == "start" || j == "left")
                    {
                        code = 0;
                    }
                    else if (j == "center")
                    {
                        code = 1;
                    }
                    else if (j == "flex-end" || j == "end" || j == "right")
                    {
                        code = 2;
                    }
                    else if (j == "space-between")
                    {
                        code = 3;
                    }
                    else if (j == "space-around")
                    {
                        code = 4;
                    }
                    if (code >= 0)
                    {
                        n.prop("justify", code);
                    }
                    else
                    {
                        LW << "html: line " << e.line << ": unsupported justify-content '"
                           << *jv << "'";
                    }
                }
                // H-7b cross-axis alignment: container default plus the
                // per-item override (auto = inherit). Baseline and
                // anything else warn once and keep the current value.
                // Absent align-items on an HTML container is the CSS
                // stretch default (H-7 A+B, html-path.md); .ui keeps
                // the FlexPanel start default.
                if (const std::string *av = fold_lookup(folded, "align-items"))
                {
                    const std::string a = ascii_lower(*av);
                    long long code = -1;
                    if (a == "flex-start" || a == "start")
                    {
                        code = 0;
                    }
                    else if (a == "center")
                    {
                        code = 1;
                    }
                    else if (a == "flex-end" || a == "end")
                    {
                        code = 2;
                    }
                    else if (a == "stretch")
                    {
                        code = 3;
                    }
                    if (code >= 0)
                    {
                        n.prop("align", code);
                    }
                    else
                    {
                        LW << "html: line " << e.line << ": unsupported align-items '"
                           << *av << "'";
                    }
                }
                else
                {
                    n.prop("align", 3);
                }
            }
            // align-self rides on the node itself (any element); the
            // builder honors it only under a flex parent. Codes are the
            // FlexPanel::align ordinal + 1, 0 = auto/inherit.
            if (const std::string *sv = fold_lookup(folded, "align-self"))
            {
                const std::string a = ascii_lower(*sv);
                int code = -1;
                if (a == "auto")
                {
                    code = 0;
                }
                else if (a == "flex-start" || a == "start")
                {
                    code = 1;
                }
                else if (a == "center")
                {
                    code = 2;
                }
                else if (a == "flex-end" || a == "end")
                {
                    code = 3;
                }
                else if (a == "stretch")
                {
                    code = 4;
                }
                if (code >= 0)
                {
                    n.self_align(code);
                }
                else
                {
                    LW << "html: line " << e.line << ": unsupported align-self '"
                       << *sv << "'";
                }
            }
            return styled_height;
        }

        void convert_elem_paint(ui_node &n, const Elem &e,
                                const std::vector<Decl> &folded)
        {
            // P-1 paint: background-color stands only when no
            // background shorthand won the fold (CSS: the shorthand
            // resets it; props append and prop_of reads the first, so
            // the loser must not emit at all)
            const std::string *bgs = fold_lookup(folded, "background");
            if (bgs == nullptr)
            {
                if (const std::string *bg = fold_lookup(folded, "background-color"))
                {
                    n.prop("background", *bg);
                }
            }
            else
            {
                // P-1 paint: the background shorthand (solid/linear/
                // radial/conic; contract html-path). Layers split
                // paren-aware and scan base-first — the first supported
                // non-repeating form wins as the base; the overlay pass
                // below lands a repeating texture over it.
                std::vector<std::string> layers;
                split_layers(*bgs, layers);
                for (auto it = layers.rbegin(); it != layers.rend(); ++it)
                {
                    if (parse_bg_layer(n, *it))
                    {
                        break;
                    }
                }
                // P-2c overlay: the topmost parseable repeating layer
                // paints translucently over the base (or alone)
                for (const std::string &layer : layers)
                {
                    if (is_repeating_layer(layer) &&
                        parse_repeating_layer(n, layer))
                    {
                        break;
                    }
                }
                if (!has_bg_prop(n))
                {
                    LW << "html: line " << e.line
                       << ": background has no supported layer; ignored";
                }
            }
            if (const std::string *bd = fold_lookup(folded, "border"))
            {
                long long w = 0;
                std::string c;
                if (parse_border(*bd, w, c))
                {
                    n.prop("border_w", w);
                    n.prop("border_color", c);
                }
            }
            // P-2d: border-top takes the same grammar on its own props
            if (const std::string *bt = fold_lookup(folded, "border-top"))
            {
                long long w = 0;
                std::string c;
                if (parse_border(*bt, w, c))
                {
                    n.prop("border_t_w", w);
                    n.prop("border_t_c", c);
                }
            }
            // P-2d: element opacity folds into the widget's own paint
            // at build time (stored fixed-point 0..1000)
            if (const std::string *op = fold_lookup(folded, "opacity"))
            {
                long long v = 1000;
                if (parse_opacity_fixed(*op, v))
                {
                    n.prop("elem_opacity", v);
                }
            }
            // P-2e box shadows: comma list, first two per kind win
            // (extras warn once and drop); a malformed entry drops
            // alone, never the declaration
            if (const std::string *bs = fold_lookup(folded, "box-shadow"))
            {
                std::vector<std::string> entries;
                split_layers(*bs, entries);
                int outs = 0;
                int ins = 0;
                bool warned = false;
                for (const std::string &sh : entries)
                {
                    bool inset = false;
                    long long ox = 0;
                    long long oy = 0;
                    long long blur = 0;
                    long long spread = 0;
                    std::string c;
                    if (!parse_one_shadow(sh, inset, ox, oy, blur, spread, c))
                    {
                        continue;
                    }
                    if ((inset && ins >= 2) || (!inset && outs >= 2))
                    {
                        if (!warned)
                        {
                            warned = true;
                            LW << "html: line " << e.line
                               << ": box-shadow keeps 2 outer + 2 inset; "
                                  "the rest is dropped";
                        }
                        continue;
                    }
                    const std::string tag =
                        (inset ? "sh_i" : "sh_o") +
                        std::to_string(inset ? ins : outs);
                    n.prop(tag + "_ox", ox);
                    n.prop(tag + "_oy", oy);
                    n.prop(tag + "_blur", blur);
                    n.prop(tag + "_spread", spread);
                    n.prop(tag + "_color", c);
                    if (inset)
                    {
                        ++ins;
                    }
                    else
                    {
                        ++outs;
                    }
                }
            }
            if (const std::string *br = fold_lookup(folded, "border-radius"))
            {
                const std::string t = css_trim(ascii_lower(*br));
                if (t == "50%")
                {
                    n.prop("radius_half", true);
                }
                else if (t.size() > 2 &&
                         t.compare(t.size() - 2, 2, "px") == 0)
                {
                    long long px = 0;
                    if (parse_svg_num(t.substr(0, t.size() - 2), px) &&
                        px >= 0)
                    {
                        n.prop("radius_px", px);
                    }
                }
            }
            // P-3 positioning: relative/absolute establish/leave the flow;
            // offsets pass through raw ("Npx"/"N%"/bare/"auto" resolved
            // by the builder); translate() only, other transforms drop
            if (const std::string *ps = fold_lookup(folded, "position"))
            {
                const std::string t = css_trim(ascii_lower(*ps));
                if (t == "absolute" || t == "relative")
                {
                    n.prop("position", t);
                }
            }
            const char *const sides[4] = {"left", "top", "right", "bottom"};
            const char *const side_props[4] = {"abs_l", "abs_t", "abs_r",
                                               "abs_b"};
            for (int si = 0; si < 4; ++si)
            {
                if (const std::string *sv = fold_lookup(folded, sides[si]))
                {
                    const std::string t = css_trim(*sv);
                    if (!t.empty() && ascii_lower(t) != "auto")
                    {
                        n.prop(side_props[si], t);
                    }
                }
            }
            if (const std::string *tf = fold_lookup(folded, "transform"))
            {
                std::string tx;
                std::string ty;
                if (parse_translate(*tf, tx, ty))
                {
                    if (!tx.empty())
                    {
                        n.prop("translate_x", tx);
                    }
                    if (!ty.empty())
                    {
                        n.prop("translate_y", ty);
                    }
                }
                else
                {
                    LW << "html: line " << e.line << ": transform '"
                       << *tf << "' is not translate(); ignored";
                }
            }
        }

        void convert_elem_text(ui_node &n, const Elem &e,
                               const std::vector<Decl> &folded,
                               const bool styled_height)
        {
            if (const std::string *fg = fold_lookup(folded, "color"))
            {
                n.prop("color", *fg);
            }
            // P-2a text dressing: tracking px, bold (700/600/bold),
            // one solid offset shadow (blur ignored)
            if (const std::string *ls = fold_lookup(folded, "letter-spacing"))
            {
                std::string t = css_trim(ascii_lower(*ls));
                if (t.size() > 2 && t.compare(t.size() - 2, 2, "px") == 0)
                {
                    t = t.substr(0, t.size() - 2);
                }
                long long px = 0;
                if (parse_svg_num(t, px))
                {
                    n.prop("letter_px", px);
                }
            }
            if (const std::string *fw = fold_lookup(folded, "font-weight"))
            {
                const std::string t = css_trim(ascii_lower(*fw));
                if (t == "bold")
                {
                    n.prop("bold", true);
                }
                else
                {
                    long long w = 0;
                    if (parse_svg_num(t, w))
                    {
                        n.prop("bold", w >= 600);
                    }
                }
            }
            if (const std::string *ts = fold_lookup(folded, "text-shadow"))
            {
                long long dx = 0;
                long long dy = 0;
                std::string c;
                if (parse_text_shadow(*ts, dx, dy, c))
                {
                    n.prop("shadow_dx", dx);
                    n.prop("shadow_dy", dy);
                    n.prop("shadow_color", c);
                }
            }
            // per-widget font size (code-contract §2.4): Npx or bare N;
            // malformed/empty drops the declaration silently (Tolerance),
            // range/family tolerance applies at materialize time
            if (const std::string *fs = fold_lookup(folded, "font-size"))
            {
                std::string t = css_trim(ascii_lower(*fs));
                if (t.size() > 2 && t.compare(t.size() - 2, 2, "px") == 0)
                {
                    t = t.substr(0, t.size() - 2);
                }
                long long px = 0;
                if (parse_svg_num(css_trim(t), px) && px > 0)
                {
                    n.prop("font_size", px);
                }
            }
            // small: the 12px caption default (the 16px body default is
            // the TTF_PIXEL_SIZE build default); an explicit font-size
            // above wins
            if (e.tag == "small" && fold_lookup(folded, "font-size") == nullptr)
            {
                n.prop("font_size", 12LL);
            }
            // H-1: a paragraph wraps at its assigned width (html-path
            // §Text wrapping); any other element stays single-line
            if (e.tag == "p")
            {
                n.prop("text_wrap", true);
            }
            // line-height: Npx gives the wrapped-block pitch; 0/Neg/unitless
            // /percent/malformed keeps the provider line metrics (Tolerance)
            if (const std::string *lh = fold_lookup(folded, "line-height"))
            {
                std::string t = css_trim(ascii_lower(*lh));
                if (t.size() > 2 && t.compare(t.size() - 2, 2, "px") == 0)
                {
                    t = t.substr(0, t.size() - 2);
                }
                long long px = 0;
                if (parse_svg_num(css_trim(t), px) && px > 0)
                {
                    n.prop("line_h", px);
                }
                else
                {
                    LW << "html: line " << e.line << ": line-height '" << *lh
                       << "' is not a positive pixel value; kept the "
                          "provider line metrics";
                }
            }

            for (const Decl &d : folded)
            {
                if (!is_known_css_prop(d.prop))
                {
                    LW << "html: style property '" << d.prop
                       << "' is not in the whitelist and was ignored";
                }
            }

            if (e.tag == "br" && !styled_height)
            {
                // a blank-line spacer: an empty label one text line tall
                n.prop("height", 7LL);
            }

        }

        void convert_elem_svg(ui_node &n, const Elem &e)
        {
            // the vector-dial subset: viewBox mapping plus strokes;
            // the generic child recursion below is skipped
            // (svg_line/svg_text are consumed, not widgets)
            long long vb[4] = {0, 0, 0, 0};
            if (split_view_box(e.attr("viewbox"), vb))
            {
                n.prop("vb_x", vb[0]).prop("vb_y", vb[1]);
                n.prop("vb_w", vb[2]).prop("vb_h", vb[3]);
            }
            const std::string par = ascii_lower(e.attr("preserveaspectratio"));
            if (!par.empty() && par != "none")
            {
                LW << "html: line " << e.line << ": preserveAspectRatio '"
                   << e.attr("preserveaspectratio")
                   << "' is not honored; the viewBox stretches like none";
            }
            const std::string &sop = e.attr("opacity");
            if (!sop.empty())
            {
                n.prop("opacity", static_cast<long long>(parse_svg_alpha(sop)));
            }
            for (const auto &c : e.children)
            {
                if (c->tag == "line")
                {
                    convert_svg_line(n, *c);
                }
                else if (c->tag == "path")
                {
                    convert_svg_path(n, *c);
                }
                else if (c->tag == "text")
                {
                    convert_svg_text(n, *c);
                }
                else
                {
                    LW << "html: line " << c->line << ": <" << c->tag
                       << "> inside svg is not in the subset and was ignored";
                }
            }
        }

        ui_node convert_elem(const Elem &e, const Rules &rules,
                             const VarMap &vars,
                             const std::vector<Ancestor> &ancestors)
        {
            ui_node n;
            std::vector<Decl> folded;
            fold_style(e, rules, vars, ancestors, folded);
            n.type = widget_type(e, folded);

            convert_elem_attrs(n, e);
            const bool styled_height = convert_elem_box(n, e, folded);
            convert_elem_paint(n, e, folded);
            convert_elem_text(n, e, folded, styled_height);

            if (n.type == "svg")
            {
                convert_elem_svg(n, e);
                return n;
            }

            // descendant context for the children: this element's tag,
            // id and classes join the chain (svg internals take no
            // stylesheet rules, so the svg branch above returns early)
            Ancestor self{e.tag, e.attr("id"), class_list(e.attr("class"))};
            std::vector<Ancestor> below = ancestors;
            below.push_back(std::move(self));
            for (const auto &c : e.children)
            {
                n.children.push_back(convert_elem(*c, rules, vars, below));
            }
            // H-10 generated boxes land on the originating node
            // (::before paints first, ::after last — see paint order)
            convert_pseudo(e.line, 1, e, rules, vars, ancestors, n);
            convert_pseudo(e.line, 2, e, rules, vars, ancestors, n);
            return n;
        }

        // -------------------------------------------------------------------
        // the page box (B2): the <body> style feeds document-level
        // size/background. Sizes are pixels per axis and independent: %
        // has no parent box to resolve against and auto means "ask the
        // shell", so only Npx values > 0 land in the page (everything
        // else stays absent and falls back downstream, silently).
        // -------------------------------------------------------------------

        void extract_page(const Elem &body, html_page &page)
        {
            const std::string &bs = body.attr("style");
            if (bs.empty())
            {
                return;
            }
            std::vector<Decl> decls;
            parse_declarations(bs.data(), bs.data() + bs.size(), decls);
            for (const Decl &d : decls)
            {
                if (d.prop == "width" || d.prop == "height")
                {
                    const std::string v = ascii_lower(d.value);
                    if (v.size() >= 2 && v.back() == 'x' &&
                        v[v.size() - 2] == 'p')
                    {
                        long long px = 0;
                        if (parse_int_value(
                                v.substr(0, v.size() - 2), px) &&
                            px > 0)
                        {
                            const int clamped = static_cast<int>(
                                std::min<long long>(px, 2147483647LL));
                            if (d.prop == "width")
                            {
                                page.has_width = true;
                                page.width = clamped;
                            }
                            else
                            {
                                page.has_height = true;
                                page.height = clamped;
                            }
                        }
                    }
                }
                else if (d.prop == "background-color")
                {
                    core::Color c{};
                    if (parse_color(d.value, c))
                    {
                        page.has_background = true;
                        page.background = c;
                    }
                }
            }
        }

        // -------------------------------------------------------------------
        // the tree builder (B6): tokens -> element tree. It owns the frame
        // stack, inline merging, and the whitelist modes, and knows nothing
        // about characters, quotes, or comments anymore.
        // -------------------------------------------------------------------

        enum frame_mode : int
        {
            k_build = 0,  // children construct widgets (body/whitelisted)
            k_no_build,   // html/head: children never construct
            k_skip,       // an off-whitelist subtree is dropped
            k_style,      // collecting <style> rule text
            k_svg_group,  // transparent <g> marker inside svg (no Elem)
        };

        struct Frame
        {
            std::string tag;
            int mode = k_build;
            bool pushed = false;  // the elem already sits in the tree
            Elem *elem = nullptr; // the elem being filled (k_build only)
            std::unique_ptr<Elem> owned;  // a held inline elem (merged at close)
        };

        struct Parser
        {
            Rules rules;
            std::string css;  // accumulated raw <style> text
            std::string text_buf;
            int text_line = 1;  // source line of the buffered text (B6)
            std::unique_ptr<Elem> root;  // the body/document container
            std::vector<Frame> frames;
            // transparent <g> presentation maps inside svg, innermost last;
            // each level already merges its ancestors (nearest wins by
            // overwrite), so resolution reads the back only
            std::vector<std::vector<std::pair<std::string, std::string>>> svg_stack;

            bool is_leaf(const Elem &e) const
            {
                return !is_container_html(e.tag);
            }

            // true while any open frame is an svg canvas: line/text/g
            // tokens then take the svg-context path
            bool in_svg() const
            {
                for (const Frame &f : frames)
                {
                    if (f.tag == "svg" || f.tag == "vectordial")
                    {
                        return true;
                    }
                }
                return false;
            }

            // nearest frame that can host a child Elem (skips transparent
            // svg group markers); nullptr past a structural boundary
            Frame *host_frame()
            {
                for (auto it = frames.rbegin(); it != frames.rend(); ++it)
                {
                    if (it->mode == k_build && it->elem != nullptr)
                    {
                        return &(*it);
                    }
                    if (it->mode == k_no_build || it->mode == k_skip)
                    {
                        return nullptr;
                    }
                }
                return nullptr;
            }

            // opens a new element into the current context. Returns the elem
            // and whether it was pushed into the tree (a false `pushed` means
            // the caller adopts `owned` -- text is merged into the leaf parent
            // at close).
            void open_elem(const std::string &tag, Elem *&out_elem,
                           bool &out_pushed, const int line)
            {
                // transparent svg group markers never host (the svg Elem
                // below them does); past a structural boundary nothing hosts
                Frame *host = host_frame();
                if (host == nullptr)
                {
                    out_elem = nullptr;
                    out_pushed = true;
                    return;
                }
                Elem *p = host->elem;
                auto e = std::make_unique<Elem>();
                e->tag = tag;
                e->line = line;
                const bool svg_child = (tag == "line" || tag == "text" ||
                                        tag == "path") &&
                    (p->tag == "svg" || p->tag == "vectordial");
                if (is_leaf(*p))
                {
                    // inline context: br is pushed (the materializer drops it
                    // with a warning); nested inline tags are merged into the
                    // leaf at close; svg canvases and their strokes push so
                    // an svg inside text still reaches the leaf-drop warning
                    if (tag == "br" || is_container_html(tag) ||
                        tag == "svg" || tag == "vectordial" || svg_child)
                    {
                        if (tag == "br")
                        {
                            if (p->tag == "p")
                            {
                                // H-1: a hard break inside a wrapping
                                // paragraph (html-path §Text wrapping);
                                // no spacer child is pushed (the break is
                                // part of the paragraph text)
                                p->text += '\n';
                                out_elem = nullptr;
                                out_pushed = true;
                                return;
                            }
                            // B1: not wrapping — the break degrades to a
                            // word space in the single-line label (the
                            // closer trims the edges, runs collapse),
                            // while the spacer child is dropped by the
                            // materializer
                            LW << "html: line " << line
                               << ": <br> outside a paragraph has no "
                                  "line-break effect; degraded to a space";
                            p->text += ' ';
                        }
                        p->children.push_back(std::move(e));
                        out_elem = p->children.back().get();
                        out_pushed = true;
                        return;
                    }
                    out_elem = e.get();
                    out_pushed = false;
                    host->owned = std::move(e);
                    return;
                }
                p->children.push_back(std::move(e));
                out_elem = p->children.back().get();
                out_pushed = true;
            }

            void flush_text()
            {
                if (text_buf.empty())
                {
                    return;
                }
                Frame &f = frames.back();
                switch (f.mode)
                {
                case k_style:
                    css += text_buf;
                    break;
                case k_build:
                    if (f.elem != nullptr)
                    {
                        if (is_leaf(*f.elem))
                        {
                            f.elem->text += text_buf;  // raw; normalized at close
                        }
                        else
                        {
                            const std::string norm = normalize_text(
                                text_buf.data(),
                                text_buf.data() + text_buf.size());
                            if (!norm.empty())
                            {
                                auto anon = std::make_unique<Elem>();
                                anon->tag = "span";
                                anon->text = norm;
                                anon->line = text_line;
                                f.elem->children.push_back(std::move(anon));
                            }
                        }
                    }
                    break;
                default:
                    break;  // k_no_build / k_skip / k_svg_group: drop
                }
                text_buf.clear();
            }

            // finalizes a build-mode elem just before its frame is closed:
            // normalize leaf text, then merge a held inline elem into the
            // enclosing leaf
            void finalize(Frame &f)
            {
                if (f.mode != k_build || f.elem == nullptr)
                {
                    return;
                }
                if (is_leaf(*f.elem))
                {
                    // H-1: a paragraph keeps its hard breaks; every other
                    // leaf collapses like before
                    f.elem->text = (f.elem->tag == "p"
                                        ? normalize_wrapped(
                                              f.elem->text.data(),
                                              f.elem->text.data() +
                                                  f.elem->text.size())
                                        : normalize_text(
                                              f.elem->text.data(),
                                              f.elem->text.data() +
                                                  f.elem->text.size()));
                }
                if (!f.pushed && frames.size() > 1)
                {
                    Frame &par = frames[frames.size() - 2];
                    if (par.mode == k_build && par.elem != nullptr &&
                        is_leaf(*par.elem))
                    {
                        par.elem->text += f.elem->text;
                    }
                }
            }

            void close_to(const std::string &name)
            {
                // pop to the matching frame; unbalanced intermediates
                // are finalized along the way (tolerant). A popped svg
                // group marker also pops its presentation map, keeping
                // the stack in sync with the frames.
                const auto pop_one = [this] {
                    if (frames.back().mode == k_svg_group &&
                        !svg_stack.empty())
                    {
                        svg_stack.pop_back();
                    }
                    finalize(frames.back());
                    frames.pop_back();
                };
                while (frames.size() > 1 && frames.back().tag != name)
                {
                    pop_one();
                }
                if (frames.size() > 1)
                {
                    pop_one();
                }
                // a closing tag without an open frame is ignored
            }

            void handle_open(const Token &t)
            {
                // decide the frame mode before creating anything (whitelist
                // knowledge lives in the contract doc)
                const std::string &name = t.name;
                int mode = k_build;
                if (is_structural(name) || name == "body")
                {
                    mode = k_no_build;  // html/head never build
                }
                else if (name == "style")
                {
                    mode = k_style;
                }
                else if (name == "g" && in_svg())
                {
                    // transparent group: no Elem, just a marker frame plus
                    // a presentation map merging over the enclosing one
                    mode = k_svg_group;
                    std::vector<std::pair<std::string, std::string>> merged;
                    if (!svg_stack.empty())
                    {
                        merged = svg_stack.back();
                    }
                    for (const auto &a : t.attrs)
                    {
                        if (!is_svg_inheritable(a.first))
                        {
                            continue;
                        }
                        bool found = false;
                        for (auto &m : merged)
                        {
                            if (m.first == a.first)
                            {
                                m.second = a.second;
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            merged.emplace_back(a.first, a.second);
                        }
                    }
                    svg_stack.push_back(std::move(merged));
                }
                else if ((name == "line" || name == "text" ||
                          name == "path") && in_svg())
                {
                    mode = k_build;  // strokes of the canvas (attrs merged below)
                }
                else if (!is_whitelisted_tag(name))
                {
                    const int innermost = frames.back().mode;
                    if (innermost == k_no_build || innermost == k_skip)
                    {
                        mode = k_skip;  // head boilerplate / dropped subtree
                    }
                    else
                    {
                        LW << "html: line " << t.line << ": element <" << name
                           << "> is not in the whitelist; skipped";
                        mode = k_skip;
                    }
                }

                Elem *elem = nullptr;
                bool pushed = false;
                // svg strokes resolve their presentation attributes against
                // the enclosing <g> maps (own attribute wins); everything
                // else keeps the token attributes verbatim
                std::vector<std::pair<std::string, std::string>> resolved;
                const std::vector<std::pair<std::string, std::string>> *attr_src = &t.attrs;
                if (mode == k_build && (name == "line" || name == "text" ||
                                        name == "path") && in_svg() &&
                    !svg_stack.empty())
                {
                    resolved = t.attrs;
                    for (const auto &m : svg_stack.back())
                    {
                        if (m.first == "opacity")
                        {
                            continue;  // multiplied below, not nearest-wins
                        }
                        bool found = false;
                        for (const auto &a : resolved)
                        {
                            if (a.first == m.first)
                            {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            resolved.emplace_back(m.first, m.second);
                        }
                    }
                    // effective opacity is the SVG product over every
                    // enclosing level times the element's own value,
                    // stored back as a decimal the converter re-parses
                    // (3 digits round-trip every alpha byte exactly)
                    const auto find_op = [](const std::vector<std::pair<std::string, std::string>> &v)
                        -> const std::string * {
                        for (const auto &a : v)
                        {
                            if (a.first == "opacity")
                            {
                                return &a.second;
                            }
                        }
                        return nullptr;
                    };
                    int eff = 255;
                    for (const auto &level : svg_stack)
                    {
                        if (const std::string *o = find_op(level))
                        {
                            eff = (parse_svg_alpha(*o) * eff + 127) / 255;
                        }
                    }
                    if (const std::string *o = find_op(resolved))
                    {
                        eff = (parse_svg_alpha(*o) * eff + 127) / 255;
                    }
                    for (auto it = resolved.begin(); it != resolved.end();)
                    {
                        if (it->first == "opacity")
                        {
                            it = resolved.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "%.3f",
                                  static_cast<double>(eff) / 255.0);
                    resolved.emplace_back("opacity", buf);
                    attr_src = &resolved;
                }
                if (mode == k_build)
                {
                    open_elem(name, elem, pushed, t.line);
                    if (elem != nullptr)
                    {
                        for (const auto &a : *attr_src)
                        {
                            elem->attrs.emplace_back(a.first, a.second);
                        }
                    }
                }
                else if (name == "body")
                {
                    // body is the document container: its children land on
                    // the root, so a single top-level container still
                    // becomes the document root (the .ui convention)
                    mode = k_build;
                    elem = root.get();
                    pushed = true;
                    if (elem != nullptr)
                    {
                        for (const auto &a : t.attrs)
                        {
                            elem->attrs.emplace_back(a.first, a.second);
                        }
                    }
                }

                // C2: br is void (HTML semantics): <br>, <br/>, <br />
                // are equivalent and it never takes a close; a stray
                // </br> then finds no frame and is ignored
                const bool closed = t.self_closing || name == "br";
                if (closed)
                {
                    // no frame is pushed for a self-closing tag
                    if (mode == k_build && !pushed)
                    {
                        frames.back().owned.reset();  // held temp: discard
                    }
                    return;
                }
                // the held inline temp lives on the host frame (transparent
                // svg markers never host), so drain it from there
                Frame *host = host_frame();
                frames.push_back({name, mode, pushed, elem,
                                  host != nullptr ? std::move(host->owned)
                                                  : std::unique_ptr<Elem>()});
            }

            void feed(const Token &t)
            {
                switch (t.kind)
                {
                case Token::Kind::text:
                    text_buf += t.text;
                    text_line = t.line;
                    flush_text();
                    break;
                case Token::Kind::close:
                    // C2 follow-up: </br> never matches (void br opens no
                    // frame), so it is purely ignored instead of triggering
                    // the stray-close recovery -- the HTML5 rule
                    if (!t.name.empty() && t.name != "br")
                    {
                        close_to(t.name);
                    }
                    break;
                case Token::Kind::open:
                    handle_open(t);
                    break;
                case Token::Kind::eof:
                    break;
                }
            }
        };
    }  // namespace

    // CSS text-axis inheritance (contract 2.4): color, font-size,
    // letter-spacing and font-weight inherit from the declaring element
    // down through the tree. The converter turns a styled container's
    // text into a CHILD label, so without this pass every declaration
    // on a div/p/body stayed behind (the model500 knob labels all
    // rendered at the bitmap fallback). Own declarations win; the
    // inherited value stamps text-bearing nodes only — containers stay
    // unstamped so no phantom per-size providers get created.
    void inherit_text_props(ui_node &n, const std::string *color,
                            const long long *font_px,
                            const long long *letter_px,
                            const bool *bold)
    {
        // effective values: the node's own declaration beats the
        // inherited one
        const std::string *own_color = nullptr;
        const long long *own_font = nullptr;
        const long long *own_letter = nullptr;
        const bool *own_bold = nullptr;
        bool has_text = false;
        for (const auto &p : n.props)
        {
            if (p.first == "text")
            {
                has_text = true;
            }
            else if (p.first == "color")
            {
                own_color = std::get_if<std::string>(&p.second);
            }
            else if (p.first == "font_size")
            {
                own_font = std::get_if<long long>(&p.second);
            }
            else if (p.first == "letter_px")
            {
                own_letter = std::get_if<long long>(&p.second);
            }
            else if (p.first == "bold")
            {
                own_bold = std::get_if<bool>(&p.second);
            }
        }
        const std::string *eff_color = own_color != nullptr ? own_color : color;
        const long long *eff_font = own_font != nullptr ? own_font : font_px;
        const long long *eff_letter =
            own_letter != nullptr ? own_letter : letter_px;
        const bool *eff_bold = own_bold != nullptr ? own_bold : bold;

        if (has_text)
        {
            if (own_color == nullptr && eff_color != nullptr)
            {
                n.prop("color", *eff_color);
            }
            if (own_font == nullptr && eff_font != nullptr)
            {
                n.prop("font_size", *eff_font);
            }
            if (own_letter == nullptr && eff_letter != nullptr)
            {
                n.prop("letter_px", *eff_letter);
            }
            if (own_bold == nullptr && eff_bold != nullptr)
            {
                n.prop("bold", *eff_bold);
            }
        }

        for (ui_node &c : n.children)
        {
            inherit_text_props(c, eff_color, eff_font, eff_letter, eff_bold);
        }
    }

    ui_node parse_html(const char *html, bool *ok, html_page *page)
    {
        if (ok != nullptr)
        {
            *ok = false;
        }
        Parser ps;
        ps.root = std::make_unique<Elem>();
        ps.root->tag = "body";  // the document container (container_html)
        ps.frames.push_back(
            {std::string{}, k_build, true, ps.root.get(), nullptr});

        const char *begin = html ? html : "";
        Tokenizer tz(begin, begin + (html ? std::strlen(html) : 0));
        for (;;)
        {
            const Token t = tz.next();
            if (t.kind == Token::Kind::eof)
            {
                break;
            }
            ps.feed(t);
        }

        // trailing text at EOF
        ps.flush_text();

        // clean up any unclosed frames
        while (ps.frames.size() > 1)
        {
            ps.finalize(ps.frames.back());
            ps.frames.pop_back();
        }

        // parse the collected <style> text (rules are global)
        parse_css(ps.css, ps.rules);

        // convert: the root's children are the top-level widgets
        ui_node doc;
        doc.type = "root";

        // the page box travels beside the tree (B2); the root Elem is
        // the body container, so its style is the page style
        if (page != nullptr)
        {
            *page = html_page{};
            extract_page(*ps.root, *page);
        }
        VarMap vars;
        collect_vars(ps.rules, vars);
        // the body joins every ancestor chain (html-path.md), so
        // body-anchored descendant selectors match uniformly in kept
        // and hoisted documents alike
        const std::vector<Ancestor> top_chain{Ancestor{
            ps.root->tag, ps.root->attr("id"),
            class_list(ps.root->attr("class"))}};
        // a flex body is kept as the document root itself
        // (html-path.md): the build host takes its container
        // properties and box dress, so the page surround and the
        // viewport-centering wrapper survive; plain bodies hoist
        // exactly as before
        {
            std::vector<Decl> body_folded;
            const std::vector<Ancestor> no_ancestors;
            fold_style(*ps.root, ps.rules, vars, no_ancestors, body_folded);
            const std::string *body_display = fold_lookup(body_folded, "display");
            if (body_display != nullptr && ascii_lower(*body_display) == "flex")
            {
                ui_node kept =
                    convert_elem(*ps.root, ps.rules, vars, no_ancestors);
                inherit_text_props(kept, nullptr, nullptr, nullptr, nullptr);
                if (ok != nullptr)
                {
                    *ok = !kept.children.empty();
                }
                return kept;
            }
        }
        for (const auto &c : ps.root->children)
        {
            doc.children.push_back(convert_elem(*c, ps.rules, vars, top_chain));
        }
        inherit_text_props(doc, nullptr, nullptr, nullptr, nullptr);

        if (ok != nullptr)
        {
            *ok = !doc.children.empty();
        }
        // a single top-level container becomes the document root itself,
        // so its spacing/padding/wrap apply to the build() host (the .ui
        // convention)
        if (doc.children.size() == 1 &&
            (doc.children[0].type == "panel" || doc.children[0].type == "column" ||
             doc.children[0].type == "row"))
        {
            return std::move(doc.children[0]);
        }
        return doc;
    }
}  // namespace zb::ui
