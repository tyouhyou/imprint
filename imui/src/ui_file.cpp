#include "ui_file.hpp"

#include <cstddef>
#include <utility>
#include <vector>

#include "logging.hpp"

namespace zb::ui
{
    namespace
    {
        constexpr int kTabWidth = 4;

        bool ident_char(const char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'
                   || (c >= '0' && c <= '9');
        }
        void skip_sp(const char **q, const char *end)
        {
            while (*q < end && (**q == ' ' || **q == '\t'))
            {
                ++*q;
            }
        }

        // reads one quoted string; *p must point at the opening quote.
        // On success, advances *p past the closing quote. Supported
        // escapes: \" and \\; any other backslash is kept literally.
        bool read_string(const char **p, const char *end, std::string &out)
        {
            const char *s = *p;
            ++s;  // past the opening "
            out.clear();
            while (s < end)
            {
                const char c = *s;
                if (c == '"')
                {
                    *p = s + 1;
                    return true;
                }
                if (c == '\\' && s + 1 < end && (s[1] == '"' || s[1] == '\\'))
                {
                    out.push_back(s[1]);
                    s += 2;
                    continue;
                }
                out.push_back(c);
                ++s;
            }
            return false;  // unterminated
        }

        bool parse_int(const std::string &t, long long &out)
        {
            std::size_t i = 0;
            bool neg = false;
            if (i < t.size() && t[i] == '-')
            {
                neg = true;
                ++i;
            }
            if (i >= t.size() || !(t[i] >= '0' && t[i] <= '9'))
            {
                return false;
            }
            long long v = 0;
            for (; i < t.size(); ++i)
            {
                if (!(t[i] >= '0' && t[i] <= '9'))
                {
                    return false;
                }
                v = v * 10 + (t[i] - '0');
            }
            out = neg ? -v : v;
            return true;
        }

        bool is_container_tag(const std::string &t)
        {
            return t == "panel" || t == "column" || t == "row";
        }

        // one parsed line: indentation + node
        struct parsed_line
        {
            int depth = 0;  // overwritten before use
            ui_node node;
        };

        // builds the tree under `parent`: every following line with a
        // strictly deeper indentation becomes a subtree (skipping levels
        // is legal); returns how many lines were consumed. The recursive
        // push/pop pair keeps ui_node* stable: children are only added to
        // the node currently being filled, never to a live vector.
        std::size_t fill(ui_node &parent, const std::vector<parsed_line> &lines,
                         std::size_t i, const int parent_depth)
        {
            const std::size_t start = i;
            while (i < lines.size())
            {
                const parsed_line &rec = lines[i];
                if (rec.depth <= parent_depth)
                {
                    break;
                }
                parent.children.push_back(std::move(rec.node));
                const std::size_t consumed =
                    fill(parent.children.back(), lines, i + 1, rec.depth);
                i += 1 + consumed;
            }
            return i - start;
        }
    }  // namespace

    /*
     * One line: [indent] tag [key=value ...] [# comment].
     * Indentation decides nesting (deeper line = child; tabs count as
     * kTabWidth columns); skipping indentation levels is legal.
     * Bare tokens are forbidden (the format has no implicit properties):
     * they are dropped with a warning. Text values need quotes; numbers
     * and true/false are bare. Unknown tags survive parsing (the
     * materializer skips them).
     *
     * A line ending in a single backslash continues on the next physical
     * line (shell style): the backslash and newline are dropped, the
     * next line's leading indentation is stripped, and blank/comment
     * lines in between are skipped. An even run of trailing backslashes
     * is literal text and does not continue.
     */
    ui_node parse_ui_text(const char *text, bool *ok)
    {
        if (ok)
        {
            *ok = false;
        }
        std::vector<parsed_line> lines;

        const char *p = text ? text : "";
        if (text != nullptr && text[0] == '\xEF' && text[1] == '\xBB' && text[2] == '\xBF')
        {
            p += 3;  // UTF-8 BOM
        }

        int line_no = 0;

        // reads the next physical line that is not blank or a comment;
        // returns its [start, end) span and advances p/line_no
        auto next_content_line = [&]() -> std::pair<const char *, const char *> {
            for (;;)
            {
                if (*p == '\0')
                {
                    return {p, p};  // end of text: empty line, no loop
                }
                const char *line_start = p;
                while (*p && *p != '\n')
                {
                    ++p;
                }
                const char *line_end = p;
                if (*p == '\n')
                {
                    ++p;
                }
                ++line_no;
                const char *q = line_start;
                while (q < line_end && (*q == ' ' || *q == '\t'))
                {
                    ++q;
                }
                if (q >= line_end || *q == '#')
                {
                    continue;  // blank or comment line
                }
                return {line_start, line_end};
            }
        };

        while (*p)
        {
            const int first_line = line_no + 1;

            // join continuation lines into one logical line
            std::string content;
            {
                const char *line_start = nullptr;
                const char *line_end = nullptr;
                std::tie(line_start, line_end) = next_content_line();
                for (;;)
                {
                    // count trailing backslashes: odd run = continuation
                    const char *bs = line_end;
                    while (bs > line_start && bs[-1] == '\\')
                    {
                        --bs;
                    }
                    const bool is_cont = ((line_end - bs) & 1) == 1;
                    content.append(line_start, is_cont ? bs : line_end);
                    if (!is_cont)
                    {
                        break;
                    }
                    std::tie(line_start, line_end) = next_content_line();
                    const char *strip = line_start;
                    while (strip < line_end && (*strip == ' ' || *strip == '\t'))
                    {
                        ++strip;
                    }
                    line_start = strip;
                }
            }

            const char *line_end = content.data() + content.size();
            const char *q = content.data();
            int depth = 0;
            while (q < line_end && (*q == ' ' || *q == '\t'))
            {
                depth += *q == '\t' ? kTabWidth : 1;
                ++q;
            }
            if (q >= line_end || *q == '#')
            {
                continue;  // blank or comment line
            }

            // tag
            const char *tag_begin = q;
            while (q < line_end && ident_char(*q))
            {
                ++q;
            }
            if (q == tag_begin)
            {
                LW << "ui_file: line " << first_line
                   << ": missing widget tag; line dropped";
                continue;
            }
            parsed_line rec;
            rec.depth = depth;
            rec.node.type.assign(tag_begin, q);
            skip_sp(&q, line_end);

            bool dropped = false;
            bool have_id = false;
            while (q < line_end && !dropped)
            {
                if (*q == '#')
                {
                    break;  // trailing comment
                }
                const char *key_begin = q;
                while (q < line_end && ident_char(*q))
                {
                    ++q;
                }
                const std::string key(key_begin, q);
                if (q >= line_end || *q != '=' || key.empty())
                {
                    // bare token (no '='): forbidden, drop it
                    const char *tok_begin = q;
                    while (q < line_end && *q != ' ' && *q != '\t' && *q != '#')
                    {
                        ++q;
                    }
                    LW << "ui_file: line " << first_line << ": bare token '"
                       << std::string(tok_begin, q)
                       << "' (use key=value); token dropped";
                    skip_sp(&q, line_end);
                    continue;
                }
                ++q;  // '='

                // value: quoted string | integer | true | false
                std::string str_value;
                long long int_value = 0;
                prop_value value;
                bool value_ok = false;

                if (q < line_end && *q == '"')
                {
                    if (read_string(&q, line_end, str_value))
                    {
                        value = str_value;
                        value_ok = true;
                    }
                    else
                    {
                        LW << "ui_file: line " << first_line
                           << ": unterminated string; line dropped";
                        dropped = true;
                        break;
                    }
                }
                else
                {
                    const char *val_begin = q;
                    while (q < line_end && *q != ' ' && *q != '\t' && *q != '#')
                    {
                        ++q;
                    }
                    const std::string token(val_begin, q);
                    if (token == "true")
                    {
                        value = true;
                        value_ok = true;
                    }
                    else if (token == "false")
                    {
                        value = false;
                        value_ok = true;
                    }
                    else if (parse_int(token, int_value))
                    {
                        value = int_value;
                        value_ok = true;
                    }
                    else
                    {
                        LW << "ui_file: line " << first_line << ": bad value '"
                           << token
                           << "' (text values need quotes); attribute dropped";
                    }
                }

                if (value_ok)
                {
                    if (key == "id")
                    {
                        if (const auto *s = std::get_if<std::string>(&value))
                        {
                            if (!have_id)
                            {
                                rec.node.id = *s;
                                have_id = true;
                            }
                        }
                    }
                    else if (key == "flex")
                    {
                        if (const auto *i = std::get_if<long long>(&value))
                        {
                            rec.node.flex_grow = static_cast<int>(*i);
                        }
                        else
                        {
                            LW << "ui_file: line " << first_line << ": flex needs an integer";
                        }
                    }
                    else if (key == "items")
                    {
                        if (const auto *s = std::get_if<std::string>(&value))
                        {
                            rec.node.items.push_back(*s);
                        // further quoted strings on the same key
                        skip_sp(&q, line_end);
                        while (q < line_end && *q == '"')
                        {
                            if (!read_string(&q, line_end, str_value))
                            {
                                // an unterminated string does not advance
                                // q: stop or this loop never ends
                                LW << "ui_file: line " << first_line
                                   << ": unterminated items string dropped";
                                break;
                            }
                            rec.node.items.push_back(std::move(str_value));
                            skip_sp(&q, line_end);
                        }
                        }
                        else
                        {
                            LW << "ui_file: line " << first_line
                               << ": items must be quoted strings";
                        }
                    }
                    else
                    {
                        rec.node.props.emplace_back(key, std::move(value));
                    }
                }
                skip_sp(&q, line_end);
            }

            if (!dropped)
            {
                lines.push_back(std::move(rec));
            }
        }

        // link the flat lines into a tree by indentation
        ui_node root;
        root.type = "root";
        fill(root, lines, 0, -1);

        if (ok)
        {
            *ok = !root.children.empty();
        }
        // a single top-level container becomes the document root itself,
        // so its spacing/padding/wrap apply to the build() host
        if (root.children.size() == 1 && is_container_tag(root.children[0].type))
        {
            return std::move(root.children[0]);
        }
        return root;
    }
}  // namespace zb::ui