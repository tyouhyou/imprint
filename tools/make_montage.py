#!/usr/bin/env python3
"""
make_montage -- assemble assets/showcase/montage.png from the
assets/showcase_html/{linux,nds,wasm}.png captures.

Usage: python3 tools/make_montage.py   (run from the repository root)

Three tiles in one row, shared height, aspect preserved (no crop), platform
label above each tile. Pure stdlib + Pillow.
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "showcase", "montage.png")
SRC = os.path.join(ROOT, "assets", "showcase_html")

TILE_H = 420
GAP = 10
LABEL_H = 26
PAD = 12

TILES = [
    ("linux.png", "linux"),
    ("nds.png", "nds"),
    ("wasm.png", "wasm"),
]


def main():
    font = ImageFont.load_default()
    images = []
    for name, label in TILES:
        img = Image.open(os.path.join(SRC, name)).convert("RGB")
        w, h = img.size
        tw = max(1, round(w * TILE_H / h))
        images.append((label, img.resize((tw, TILE_H), Image.LANCZOS)))

    total_w = sum(im.size[0] for _, im in images) + GAP * (len(images) + 1)
    board = Image.new("RGB", (total_w, PAD + LABEL_H + TILE_H + PAD), (26, 26, 26))
    draw = ImageDraw.Draw(board)

    x = GAP
    y = PAD + LABEL_H
    for label, im in images:
        tw = draw.textlength(label, font=font)
        draw.text((x + (im.size[0] - tw) // 2, PAD + 6),
                  label, fill=(220, 220, 220), font=font)
        board.paste(im, (x, y))
        x += im.size[0] + GAP

    board.save(OUT)
    print(f"montage: {OUT} {board.size[0]}x{board.size[1]}")


if __name__ == "__main__":
    main()
