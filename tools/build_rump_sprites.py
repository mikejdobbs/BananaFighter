#!/usr/bin/env python3
"""Pack painted style-A Rump frames into sprites/rump.bmp and the Rump shot cell."""
from __future__ import annotations

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

import make_sprites as ms  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402
from tools import sprite_packer_util as spu  # noqa: E402

SPRITES = os.path.join(ROOT, "sprites")
FRAMES = os.path.join(ROOT, "tools", "refs", "rump_style_a_frames")
RUMP_SHOT_INDEX = ms.CHAR_ORDER.index("rump")
CELL = ms.CELL
COLS = ms.COLS
ROWS = ms.ROWS

FRAME_NAMES = [
    "idle",
    "walk_0",
    "walk_1",
    "walk_2",
    "walk_3",
    "jump_0",
    "jump_1",
    "jump_2",
    "jump_3",
    "crouch",
    "crouch_block",
    "crouch_punch",
    "crouch_kick",
    "block",
    "hurt",
    "punch_windup",
    "punch_hit",
    "punch_recover",
    "kick_windup",
    "kick_hit",
    "kick_recover",
    "high_kick",
    "uppercut",
    "hairtoss",
    "bald",
    "plane_0",
    "plane_1",
]


def hairtoss_row() -> list[str]:
    return [
        "punch_windup",
        "hairtoss",
        "hairtoss",
        "hairtoss",
        "bald",
        "idle",
    ]


def toupee_icon() -> Image.Image:
    im = Image.new("RGBA", (96, 96), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    gold = (245, 205, 90, 255)
    shade = (210, 165, 55, 255)
    d.polygon([(20, 58), (76, 52), (82, 44), (70, 36), (48, 34), (26, 40)], fill=gold)
    d.polygon([(30, 48), (68, 44), (58, 38), (40, 40)], fill=shade)
    d.ellipse((44, 38, 56, 48), fill=(255, 220, 140, 255))
    return im.resize((48, 48), Image.LANCZOS)


def main():
    cache = {name: spu.load_frame(FRAMES, name) for name in FRAME_NAMES}
    rows, ship = spu.standard_sheet_rows(special=hairtoss_row(), ship=("plane_0", "plane_1"))
    assert len(rows) == ROWS - 1

    os.makedirs(SPRITES, exist_ok=True)
    sheet_path = os.path.join(SPRITES, "rump.bmp")
    ms.save_argb(
        spu.build_sheet_from_rows(cache, rows, ship, cols=COLS, rows_total=ROWS, cell=CELL),
        sheet_path,
    )
    print("wrote", sheet_path, flush=True)

    shots_path = os.path.join(SPRITES, "shots.bmp")
    if os.path.isfile(shots_path):
        shots = Image.open(shots_path).convert("RGBA")
    else:
        shots = Image.new("RGBA", (48 * len(ms.CHAR_ORDER), 48), (0, 0, 0, 0))
    if shots.size != (48 * len(ms.CHAR_ORDER), 48):
        raise SystemExit(f"unexpected shots.bmp size {shots.size}")
    shots.paste(toupee_icon(), (RUMP_SHOT_INDEX * 48, 0))
    ms.save_argb(shots, shots_path)
    print("patched Rump cell in", shots_path, flush=True)


if __name__ == "__main__":
    main()
