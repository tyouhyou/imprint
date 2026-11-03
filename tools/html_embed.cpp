/*
 * html_embed — build-time HTML design file packer.
 *
 * Usage: html_embed <out_header> <file.html>...
 *
 * The ui_embed precedent for .ui documents: reads each HTML document,
 * validates it with the *same* parser the app uses (parse_html), then
 * generates a C++ header embedding the raw document bytes:
 *
 *   static const embedded_html_file kHtmlFiles[] = { {"space.html", ...}, ... };
 *   static inline const embedded_html_file *find_html_file(const char *name);
 *
 * Every <img src> in a document names a file next to that document
 * (docs/html-path.md "Build-time images"); the referenced files are
 * packed into a parallel name -> bytes table beside the document bytes:
 *
 *   static const embedded_image_file kImages[] = { {"art.png", ...}, ... };
 *   static inline const embedded_image_file *find_image_file(const char *name);
 *
 * A document that does not parse (yields no widget), carries a tolerance
 * warning, or references an image the build cannot read makes the tool
 * exit non-zero, so the CMake custom command fails the build: the
 * "build-time validation" guarantee, without a second parser. Embedded
 * document bytes are the raw UTF-8 text plus one trailing 0x00 sentinel
 * (the size field excludes the sentinel): parse_html scans for a NUL
 * terminator, so the embedded array must carry the same shape pass 1
 * validated. Image bytes are packed raw (no sentinel). At runtime the
 * app feeds the documents to parse_html + build() and attaches the
 * images itself (find_image_file + set_image_png) — the exact code path
 * desktop apps use with external files, so NDS (no filesystem) is just
 * another source.
 *
 * The generated header only needs the structs below; it does not include
 * any framework header (the consumer decides how to parse the bytes).
 */
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

#include "html.hpp"
#include "logging.hpp"
#include "ui_builder.hpp"

namespace
{
    // The logging channel has no default handler: Logging::log() drops
    // the message when s_log_handler is empty. Without installing one,
    // every tolerance warning the parser emits (off-whitelist element or
    // declaration, malformed value, dropped subtree) is discarded and the
    // build-time validation reports nothing — the failure mode this tool
    // exists to prevent. Mirror warnings onto stderr and count them; pass
    // 1 exits non-zero when the count is non-zero (docs/html-path.md
    // "Tolerance", code-contract 5.3).
    int g_warnings = 0;

    void install_log_mirror(const char *tool)
    {
        zb::Logging::set_log_handle(
            [tool](const zb::Logging_Level level, const std::string &message)
            {
                if (!zb::Logging::suppressed(zb::Logging_Level::warn) &&
                    level >= zb::Logging_Level::warn)
                {
                    ++g_warnings;
                }
                std::fprintf(stderr, "%s: %s: %s\n", tool,
                             level >= zb::Logging_Level::error ? "error" : "warning",
                             message.c_str());
            });
    }

    // "command center.html" -> command_center_html
    std::string symbol_of(const std::string &name)
    {
        std::string out;
        for (const char c : name)
        {
            const bool ident = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            out.push_back(ident ? c : '_');
        }
        if (out.empty())
        {
            out = "html";
        }
        if (!(out[0] == '_' || std::isalpha(static_cast<unsigned char>(out[0]))))
        {
            out.insert(out.begin(), '_');
        }
        return out;
    }

    std::string file_base(const std::string &path)
    {
        const std::size_t slash = path.find_last_of("/\\");
        std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
        const std::size_t dot = base.rfind('.');
        return dot == std::string::npos ? base : base.substr(0, dot);
    }

    // "dir/space.html" -> "space.html": the lookup name is the basename
    // (with extension), so find_html_file matches regardless of the
    // directory the build ran from
    std::string file_name(const std::string &path)
    {
        const std::size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // "dir/space.html" -> "dir": image srcs resolve relative to the
    // document's directory (html-path "Build-time images")
    std::string dir_of(const std::string &path)
    {
        const std::size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }

    // the image_view srcs of one parsed document, first-seen order,
    // deduped. The parse gate has already rejected any document carrying
    // an img without a usable src, so every hit here has content.
    void collect_image_srcs(const zb::ui::ui_node &n, std::vector<std::string> &out)
    {
        if (n.type == "image_view")
        {
            for (const auto &p : n.props)
            {
                if (p.first == "src")
                {
                    if (const auto *s = std::get_if<std::string>(&p.second))
                    {
                        if (!s->empty() &&
                            std::find(out.begin(), out.end(), *s) == out.end())
                        {
                            out.push_back(*s);
                        }
                    }
                }
            }
        }
        for (const auto &c : n.children)
        {
            collect_image_srcs(c, out);
        }
    }

    // the table key becomes a C string literal: reject anything the
    // generator cannot spell safely
    bool spellable_name(const std::string &name)
    {
        for (const char c : name)
        {
            if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20)
            {
                return false;
            }
        }
        return !name.empty();
    }

    bool read_file(const char *path, std::vector<unsigned char> &out)
    {
        FILE *f = std::fopen(path, "rb");
        if (f == nullptr)
        {
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        out.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
        const bool ok =
            out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
        std::fclose(f);
        return ok;
    }
}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: html_embed <out_header> <file.html>...\n");
        return 2;
    }

    install_log_mirror("html_embed");

    const std::vector<std::string> inputs(argv + 2, argv + argc);

    // pass 1: validate every document with the library parser — the
    // document must parse AND yield at least one known widget (the
    // runtime zb_app_create_from_ui gate), not merely carry nodes. The
    // image_view srcs are collected in the same walk (first-seen order).
    std::vector<std::vector<std::string>> doc_images;
    for (const std::string &path : inputs)
    {
        std::vector<unsigned char> bytes;
        bool ok_read = read_file(path.c_str(), bytes);
        bool ok_parse = false;
        std::vector<std::string> srcs;
        if (ok_read)
        {
            bytes.push_back(0);  // NUL-terminate for the parser
            bool ok = false;
            const zb::ui::ui_node doc = zb::ui::parse_html(
                reinterpret_cast<const char *>(bytes.data()), &ok);
            ok_parse = ok && zb::ui::materializes_widget(doc);
            if (ok_parse)
            {
                collect_image_srcs(doc, srcs);
            }
        }
        if (!ok_read || !ok_parse)
        {
            std::fprintf(stderr, "html_embed: %s: %s\n", path.c_str(),
                         ok_read ? "document yields no widget" : "cannot read file");
            return 1;
        }
        doc_images.push_back(std::move(srcs));
    }

    // a document that parsed but produced warnings is still drift: every
    // LW row in the tolerance table means a declaration the renderer
    // ignored. Fail the build rather than embed it.
    if (g_warnings > 0)
    {
        std::fprintf(stderr,
                     "html_embed: %d parser warning(s) — the document parses "
                     "but carries constructs the renderer ignores (see "
                     "docs/html-path.md \"Tolerance\"); fix the document or "
                     "add the construct to the whitelist\n",
                     g_warnings);
        return 1;
    }

    // pass 1.5: pack the <img> images (html-path "Build-time images").
    // Each src is read relative to its document's directory; the same
    // src name in two documents must resolve to the same file. A missing
    // or unreadable file exits non-zero — the same authoring-error gate
    // as the parser warnings: an embedded document cannot reference an
    // image the build did not pack.
    struct packed_image
    {
        std::string name;  // the src value as the document wrote it
        std::string sym;
        std::string resolved;  // the document-relative resolution
        std::vector<unsigned char> bytes;
    };
    std::vector<packed_image> images;
    for (std::size_t i = 0; i < inputs.size(); ++i)
    {
        const std::string &path = inputs[i];
        const std::string dir = dir_of(path);
        for (const std::string &src : doc_images[i])
        {
            if (!spellable_name(src))
            {
                std::fprintf(stderr,
                             "html_embed: %s: image src is not a usable resource name\n",
                             path.c_str());
                return 1;
            }
            const std::string resolved = dir.empty() ? src : dir + "/" + src;

            bool packed = false;
            for (const packed_image &prev : images)
            {
                if (prev.name == src)
                {
                    if (prev.resolved != resolved)
                    {
                        std::fprintf(stderr,
                                     "html_embed: src \"%s\" resolves to two files (%s, %s)\n",
                                     src.c_str(), prev.resolved.c_str(), resolved.c_str());
                        return 1;
                    }
                    packed = true;  // same name, same file: pack once
                    break;
                }
            }
            if (packed)
            {
                continue;
            }

            packed_image img;
            img.name = src;
            img.resolved = resolved;
            if (!read_file(resolved.c_str(), img.bytes) || img.bytes.empty())
            {
                std::fprintf(stderr, "html_embed: %s: image \"%s\" cannot be read (%s)\n",
                             path.c_str(), src.c_str(), resolved.c_str());
                return 1;
            }
            img.sym = symbol_of(src) + "_img";
            for (const packed_image &prev : images)
            {
                if (prev.sym == img.sym)
                {
                    std::fprintf(stderr,
                                 "html_embed: images %s and %s collide (same symbol)\n",
                                 prev.resolved.c_str(), resolved.c_str());
                    return 1;
                }
            }
            images.push_back(std::move(img));
        }
    }

    // pass 2: emit the header
    FILE *out = std::fopen(argv[1], "w");
    if (out == nullptr)
    {
        std::fprintf(stderr, "html_embed: cannot write %s\n", argv[1]);
        return 2;
    }

    std::fprintf(out, "#pragma once\n");
    std::fprintf(out, "// generated by html_embed — do not edit\n");
    std::fprintf(out, "// each document data[] ends with one 0x00 sentinel beyond size;\n");
    std::fprintf(out, "// parse_html scans for the NUL terminator. Image data[] is raw.\n");
    std::fprintf(out, "#include <cstddef>\n");
    std::fprintf(out, "#include <cstring>\n");
    std::fprintf(out, "struct embedded_html_file\n{\n"
                      "    const char *name;\n"
                      "    const unsigned char *data;\n"
                      "    const std::size_t size;\n"
                      "};\n\n");

    struct doc
    {
        std::string path;
        std::string sym;
        std::string name;
        std::vector<unsigned char> bytes;
    };
    std::vector<doc> docs;
    for (const std::string &path : inputs)
    {
        doc d;
        d.path = path;
        d.name = file_name(path);
        d.sym = symbol_of(file_base(path)) + "_html";
        if (!read_file(path.c_str(), d.bytes))
        {
            std::fprintf(stderr, "html_embed: %s: cannot read file\n", path.c_str());
            std::fclose(out);
            return 1;
        }
        for (const auto &prev : docs)
        {
            if (prev.sym == d.sym || prev.name == d.name)
            {
                std::fprintf(stderr, "html_embed: %s and %s collide (%s)\n",
                             prev.path.c_str(), path.c_str(),
                             prev.sym == d.sym ? "same basename" : "same file name");
                std::fclose(out);
                return 1;
            }
        }
        // match the shape pass 1 validated: the parser needs the NUL
        // terminator, the size field below reports the document length
        d.bytes.push_back(0);
        docs.push_back(std::move(d));
    }

    for (const doc &d : docs)
    {
        std::fprintf(out, "static const unsigned char %s[] =\n{\n", d.sym.c_str());
        for (std::size_t i = 0; i < d.bytes.size(); ++i)
        {
            std::fprintf(out, "0x%02x,", d.bytes[i]);
            if (i % 16 == 15)
            {
                std::fprintf(out, "\n");
            }
        }
        std::fprintf(out, "\n};\n\n");
    }

    std::fprintf(out, "static const embedded_html_file kHtmlFiles[] =\n{\n");
    for (const doc &d : docs)
    {
        std::fprintf(out, "    {\"%s\", %s, sizeof(%s) - 1},\n", d.name.c_str(),
                     d.sym.c_str(), d.sym.c_str());
    }
    std::fprintf(out, "};\n\n");

    std::fprintf(out, "static inline const embedded_html_file *find_html_file(const char *name)\n"
                      "{\n"
                      "    for (const auto &f : kHtmlFiles)\n"
                      "    {\n"
                      "        if (f.name != nullptr && name != nullptr\n"
                      "            && std::strcmp(f.name, name) == 0)\n"
                      "        {\n"
                      "            return &f;\n"
                      "        }\n"
                      "    }\n"
                      "    return nullptr;\n"
                      "}\n");

    // the image table lands beside the document bytes, in the same
    // self-contained style; emitted only when the build packed images
    if (!images.empty())
    {
        std::fprintf(out, "struct embedded_image_file\n{\n"
                          "    const char *name;\n"
                          "    const unsigned char *data;\n"
                          "    const std::size_t size;\n"
                          "};\n\n");
        for (const packed_image &img : images)
        {
            std::fprintf(out, "static const unsigned char %s[] =\n{\n", img.sym.c_str());
            for (std::size_t i = 0; i < img.bytes.size(); ++i)
            {
                std::fprintf(out, "0x%02x,", img.bytes[i]);
                if (i % 16 == 15)
                {
                    std::fprintf(out, "\n");
                }
            }
            std::fprintf(out, "\n};\n\n");
        }
        std::fprintf(out, "static const embedded_image_file kImages[] =\n{\n");
        for (const packed_image &img : images)
        {
            // image bytes are raw: sizeof covers exactly the packed size
            std::fprintf(out, "    {\"%s\", %s, sizeof(%s)},\n", img.name.c_str(),
                         img.sym.c_str(), img.sym.c_str());
        }
        std::fprintf(out, "};\n\n");
        std::fprintf(out, "static inline const embedded_image_file *find_image_file(const char *name)\n"
                          "{\n"
                          "    for (const auto &f : kImages)\n"
                          "    {\n"
                          "        if (f.name != nullptr && name != nullptr\n"
                          "            && std::strcmp(f.name, name) == 0)\n"
                          "        {\n"
                          "            return &f;\n"
                          "        }\n"
                          "    }\n"
                          "    return nullptr;\n"
                          "}\n");
    }
    std::fclose(out);
    return 0;
}
