#!/usr/bin/env python3
"""
make_montage -- assemble assets/showcase/montage.png from the per-platform
showcase captures (Batch V-3).

Usage: python3 tools/make_montage.py   (run from the repository root)

Tiles: assets/showcase/{win,mac,linux,nds}.png, center-cropped to 4:3 and
scaled to a uniform tile, laid out 2x2 with captions. Pure stdlib + Pillow
(the same dependency the GIF re-record verification already uses).
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "showcase", "montage.png")

TILE_W, TILE_H = 400, 300
GAP = 8
CAPTION_H = 22
TITLE_H = 40

TILES = [
    ("win.png", "Windows (Win32)"),
    ("mac.png", "macOS (AppKit)"),
    ("linux.png", "Linux (X11)"),
    ("nds.png", "Nintendo DS (melonDS)"),
]

TITLE = "Imprint UI showcase -- one UI source tree, every target"


def center_crop_43(img):
    """center-crop to 4:3 (the tile aspect)"""
    w, h = img.size
    tw, th = (w, w * 3 // 4) if w * 3 <= h * 4 else (h * 4 // 3, h)
    left, top = (w - tw) // 2, (h - th) // 2
    return img.crop((left, top, left + tw, top + th))


def main():
    font = ImageFont.load_default()
    cols, rows = 2, 2
    width = cols * TILE_W + (cols + 1) * GAP
    height = TITLE_H + rows * (TILE_H + CAPTION_H) + (rows + 1) * GAP
    board = Image.new("RGB", (width, height), (26, 26, 26))
    draw = ImageDraw.Draw(board)
    draw.text((GAP + 2, 12), TITLE, fill=(240, 240, 240), font=font)

    for n, (name, caption) in enumerate(TILES):
        col, row = n % cols, n // cols
        x = GAP + col * (TILE_W + GAP)
        y = TITLE_H + GAP + row * (TILE_H + CAPTION_H + GAP)
        img = Image.open(os.path.join(ROOT, "assets", "showcase", name)).convert("RGB")
        tile = center_crop_43(img).resize((TILE_W, TILE_H), Image.LANCZOS)
        board.paste(tile, (x, y))
        # center the caption under the tile
        tw = draw.textlength(caption, font=font)
        draw.text((x + (TILE_W - tw) // 2, y + TILE_H + 4),
                  caption, fill=(200, 200, 200), font=font)

    board.save(OUT)
    print(f"montage: {OUT} {board.size[0]}x{board.size[1]}")


if __name__ == "__main__":
    main()
