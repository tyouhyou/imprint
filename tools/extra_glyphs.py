# Hand-drawn 5x7 glyphs for code units beyond the built-in ASCII range
# (32..95). The font_subset.py generator (E batch) merges this table with
# the code units the sources actually use, so only the drawn characters
# reach the runtime table.
#
# Convention: row 0 is the top row, bit 4 of a row is the leftmost pixel
# (same as kGlyphs in imcore/src/text/bitmap_provider.cpp). 5x7 cannot
# render CJK readably -- CJK needs at least 12x12 and a build-time font
# rasterizer (subset batch E, option 2, not implemented).

EXTRA_GLYPHS = {
    # e-acute
    u'\u00e9': [
        0b00100,  # accent
        0b01110,  # e
        0b10001,
        0b10001,
        0b11111,
        0b10001,
        0b10001,
    ],
}