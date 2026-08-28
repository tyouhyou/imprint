#!/usr/bin/env python3
"""Font subset generator (E batch).

Scans source files for string literals, collects the code units the
sources actually use (beyond the built-in ASCII range 32..95 handled by
imcore's static 5x7 table), intersects them with the hand-drawn glyph
table (extra_glyphs.py) and writes a sorted runtime table
(subset_glyphs.hpp). Characters used by the sources but not drawn in the
extra table are reported as warnings: they render as zero-width skips
(see docs/code-contract.md section 2.4). Code units that come only from
.ui files never warn: a design file may carry strings the app does not
draw at runtime.

Usage:
  font_subset.py --extras <table.py> --out <header.hpp> [source...]

The generator never fails the build: a missing Python or a scan failure
falls back to an ASCII-only table.
"""

import argparse
import os
import re
import sys

# a string literal with simple escapes; enough for the project's style
_STRING_LITERAL = re.compile(r'"(?:[^"\\]|\\.)*"')

_SOURCES = (".cpp", ".hpp", ".c", ".h", ".cc", ".hh", ".ui")


def scan_ui_file(path):
    """Yields non-ASCII code units from text= and items= attributes in .ui file."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
    except OSError:
        return set()
    found = set()
    # join continuation lines (backslash at end of line)
    raw_lines = content.splitlines()
    lines = []
    for line in raw_lines:
        line = line.rstrip()
        if line.endswith("\\"):
            if lines:
                lines[-1] += line[:-1]
            else:
                lines.append(line[:-1])
        else:
            lines.append(line)
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        # find all key="value" pairs for text= and items=
        # handles escaped quotes and backslashes inside the value
        for m in re.finditer(r'\b(text|items)\s*=\s*"((?:[^"\\]|\\.)*)"', line):
            raw = m.group(2)
            # decode escape sequences: \" -> ", \\ -> \, others kept literally
            val = raw.replace('\\"', '"').replace('\\\\', '\\')
            for ch in val:
                cp = ord(ch)
                if cp >= 128:
                    found.add(cp)
    return found


def scan_literals(path):
    """Yields the non-ASCII code units found in the file's string literals."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return
    text = data.decode("utf-8", errors="replace")
    found = set()
    for lit in _STRING_LITERAL.finditer(text):
        raw = lit.group(0)[1:-1]
        # decode the escape sequences: plain characters and \xNN go into
        # a byte stream that is then decoded as UTF-8; \u escapes are
        # already code units and are collected separately
        bytes_out = bytearray()
        units = []
        i = 0
        n = len(raw)
        while i < n:
            c = raw[i]
            if c == "\\" and i + 1 < n:
                nxt = raw[i + 1]
                if nxt in "\\\"'":
                    bytes_out += nxt.encode("utf-8")
                    i += 2
                elif nxt == "n":
                    bytes_out += b"\n"
                    i += 2
                elif nxt == "t":
                    bytes_out += b"\t"
                    i += 2
                elif nxt == "u" and i + 5 < n:
                    try:
                        units.append(chr(int(raw[i + 2:i + 6], 16)))
                        i += 6
                    except ValueError:
                        i += 2
                elif nxt == "x" and i + 2 < n:
                    j = i + 2
                    while j < n and j < i + 4 and raw[j] in "0123456789abcdefABCDEF":
                        j += 1
                    try:
                        bytes_out.append(int(raw[i + 2:j], 16) & 0xFF)
                        i = j
                    except ValueError:
                        i += 2
                else:
                    bytes_out += nxt.encode("utf-8")
                    i += 2
            else:
                bytes_out += c.encode("utf-8")
                i += 1
        for ch in bytes(bytes_out).decode("utf-8", errors="ignore"):
            if ord(ch) >= 128:
                found.add(ord(ch))
        for ch in units:
            if ord(ch) >= 128:
                found.add(ord(ch))
    return found


def collect_sources(paths):
    """Expands directories (recursively) into a source file list."""
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, _dirs, names in os.walk(p):
                for name in sorted(names):
                    if name.endswith(_SOURCES):
                        files.append(os.path.join(root, name))
        elif os.path.isfile(p):
            files.append(p)
    return files


def load_extras(path):
    namespace = {}
    with open(path, "r", encoding="utf-8") as f:
        exec(f.read(), namespace)  # noqa: S102 -- trusted build-time table
    return namespace["EXTRA_GLYPHS"]


def main():
    parser = argparse.ArgumentParser(description="imprint font subset generator")
    parser.add_argument("--extras", required=True, help="hand-drawn glyph table (.py)")
    parser.add_argument("--out", required=True, help="generated header path")
    parser.add_argument("sources", nargs="*", help="source files or directories")
    args = parser.parse_args()

    extras = load_extras(args.extras)
    extras_by_cp = {ord(ch): rows for ch, rows in extras.items()}

    used = set()      # string literals in the sources
    ui_used = set()   # text=/items= attributes in .ui files
    for path in collect_sources(args.sources):
        if path.endswith(".ui"):
            ui_used |= scan_ui_file(path)
        else:
            used |= scan_literals(path)

    # warn only about source literals: a .ui string may legitimately use
    # a glyph the app never draws at runtime (A-17)
    missing = sorted(cp for cp in used if cp not in extras_by_cp)
    if missing:
        sys.stderr.write(
            "font_subset: sources use code units not drawn in %s: %s\n"
            % (args.extras, " ".join("U+%04X" % cp for cp in missing))
        )

    selected = sorted(extras_by_cp.keys() & (used | ui_used))

    lines = []
    lines.append("// GENERATED by tools/font_subset.py -- do not edit.")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("namespace zb::ui")
    lines.append("{")
    lines.append("    struct SubsetGlyph")
    lines.append("    {")
    lines.append("        char16_t ch;      // the code unit")
    lines.append("        uint8_t rows[7];  // 5x7 bitmap, row 0 on top")
    lines.append("    };")
    lines.append("")
    lines.append("    // sorted by ch (binary search), only the code units")
    lines.append("    // the sources use and the extra table draws")
    lines.append("    inline constexpr SubsetGlyph kSubsetGlyphs[] = {")
    for cp in selected:
        rows = ", ".join("0b%05d" % int(format(r, "b") if r else 0) for r in extras_by_cp[cp])
        lines.append("        {u'\\u%04X', {%s}}," % (cp, rows))
    lines.append("    };")
    lines.append("")
    lines.append("    inline constexpr size_t kSubsetGlyphCount =")
    lines.append("        sizeof(kSubsetGlyphs) / sizeof(kSubsetGlyphs[0]);")
    lines.append("}")
    lines.append("")

    body = "\n".join(lines)
    out_path = args.out
    tmp_path = out_path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(body)
    if not os.path.exists(out_path) or open(out_path, "rb").read() != open(tmp_path, "rb").read():
        os.replace(tmp_path, out_path)
    else:
        os.unlink(tmp_path)


if __name__ == "__main__":
    main()