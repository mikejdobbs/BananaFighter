#!/usr/bin/env python3
"""Pack painted style-A JV frames into sprites/jv.bmp and the JV shot cell.

Art is full drawings in tools/refs/jv_style_a_frames/. This script only knocks
out the gray background, scales, and places cells. It does not draw a skeleton.
"""
from __future__ import annotations

import os
import sys
from collections import deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

import make_sprites as ms  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402
from tools import sprite_packer_util as spu  # noqa: E402

SPRITES = os.path.join(ROOT, "sprites")
FRAMES = os.path.join(ROOT, "tools", "refs", "jv_style_a_frames")
JV_SHOT_INDEX = ms.CHAR_ORDER.index("jv")
CELL = ms.CELL
COLS = ms.COLS
ROWS = ms.ROWS


def _gray_bg(r: int, g: int, b: int) -> bool:
    if max(r, g, b) - min(r, g, b) > 26:
        return False
    avg = (r + g + b) / 3
    return 55 < avg < 190


def knock_background(im: Image.Image) -> Image.Image:
    """Flood-fill flat gray from the edges so the painted figure keeps its alpha."""
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    seen = bytearray(w * h)
    q: deque[tuple[int, int]] = deque()
    for x in range(w):
        q.append((x, 0))
        q.append((x, h - 1))
    for y in range(h):
        q.append((0, y))
        q.append((w - 1, y))
    while q:
        x, y = q.popleft()
        if x < 0 or y < 0 or x >= w or y >= h:
            continue
        i = y * w + x
        if seen[i]:
            continue
        seen[i] = 1
        r, g, b, _a = px[x, y]
        if not _gray_bg(r, g, b):
            continue
        px[x, y] = (0, 0, 0, 0)
        q.append((x + 1, y))
        q.append((x - 1, y))
        q.append((x, y + 1))
        q.append((x, y - 1))
    return im


def keep_largest(im: Image.Image) -> Image.Image:
    """Drop stray specks so a duck's bbox is the body, not a dot above the hair."""
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    seen = bytearray(w * h)
    comps: list[list[tuple[int, int]]] = []
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if seen[i] or px[x, y][3] < 16:
                continue
            q: deque[tuple[int, int]] = deque([(x, y)])
            seen[i] = 1
            comp: list[tuple[int, int]] = []
            while q:
                cx, cy = q.popleft()
                comp.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if nx < 0 or ny < 0 or nx >= w or ny >= h:
                        continue
                    j = ny * w + nx
                    if not seen[j] and px[nx, ny][3] >= 16:
                        seen[j] = 1
                        q.append((nx, ny))
            comps.append(comp)
    biggest = max((len(c) for c in comps), default=0)
    # Keep every large piece (a gray gap can split an arm). Drop only specks.
    keep = {p for c in comps if len(c) >= max(400, biggest // 40) for p in c}
    for y in range(h):
        for x in range(w):
            if px[x, y][3] and (x, y) not in keep:
                px[x, y] = (0, 0, 0, 0)
    return im


def load_frame(name: str) -> Image.Image:
    path = os.path.join(FRAMES, f"{name}.png")
    if not os.path.isfile(path):
        raise SystemExit(f"missing style-A frame: {path}")
    return keep_largest(knock_background(Image.open(path)))


def _paste_scaled(im: Image.Image, scale: float, bob: int) -> Image.Image:
    canvas = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    bb = im.getbbox()
    if not bb:
        return canvas
    crop = im.crop(bb)
    nw = max(1, int(round(crop.width * scale)))
    nh = max(1, int(round(crop.height * scale)))
    crop = crop.resize((nw, nh), Image.LANCZOS)
    x = (CELL - nw) // 2
    y = CELL - nh - 4 - bob
    canvas.paste(crop, (x, max(0, y)), crop)
    return canvas


def standing_scale(idle: Image.Image) -> float:
    """Pixels-per-source-pixel taken from the idle figure. Ducks reuse this
    so a shorter pose stays shorter instead of being stretched or shrunk."""
    bb = idle.getbbox()
    return (CELL - 10) / (bb[3] - bb[1])


def place(im: Image.Image, bob: int = 0) -> Image.Image:
    """Fit a standing figure in the cell, feet on the bottom."""
    bb = im.getbbox()
    if not bb:
        return Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    scale = min((CELL - 10) / (bb[3] - bb[1]), (CELL - 6) / (bb[2] - bb[0]))
    return _paste_scaled(im, scale, bob)


def place_slouch(im: Image.Image, scale: float) -> Image.Image:
    """Same body size as idle. A bent pose sits lower; it is not scaled down."""
    return _paste_scaled(im, scale, 0)


def bone_icon() -> Image.Image:
    im = Image.new("RGBA", (96, 96), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    bone = (236, 224, 200, 255)
    shade = (196, 176, 148, 255)
    d.line([(18, 50), (78, 46)], fill=bone, width=10)
    d.ellipse((8, 34, 30, 58), fill=bone)
    d.ellipse((66, 30, 90, 56), fill=bone)
    d.ellipse((12, 38, 24, 50), fill=shade)
    d.ellipse((72, 34, 84, 48), fill=shade)
    return im.resize((48, 48), Image.LANCZOS)


def _phase(windup: str, hit: str, recover: str) -> list[str]:
    return [windup, windup, hit, hit, recover, recover]


def sheet_rows() -> list[list[str]]:
    punch = _phase("punch_windup", "punch_hit", "punch_recover")
    kick = _phase("kick_windup", "kick_hit", "kick_recover")
    high = _phase("kick_windup", "high_kick", "kick_recover")
    bone = ["punch_windup", "bone", "bone", "bone", "punch_recover", "idle"]
    upper = ["punch_windup", "uppercut", "uppercut", "uppercut", "punch_recover", "idle"]
    air_p = ["jump_0", "jump_1", "punch_hit", "punch_hit", "jump_2", "jump_3"]
    air_k = ["jump_0", "jump_1", "kick_hit", "kick_hit", "jump_2", "jump_3"]
    air_hk = ["jump_0", "jump_1", "high_kick", "high_kick", "jump_2", "jump_3"]
    duck_p = _phase("crouch", "crouch_punch", "crouch")
    duck_k = _phase("crouch", "crouch_kick", "crouch")
    duck_sweep = _phase("crouch", "crouch_sweep", "crouch")
    rows = [
        ["idle"] * 6,
        [f"walk_{i}" for i in range(8)],
        ["jump_0", "jump_1", "jump_2", "jump_2", "jump_3", "idle"],
        ["block", "block"],
        ["hurt", "hurt", "hurt", "fall_0", "fall_1", "fall_2", "fall_knees", "idle"],
        [],
        ["crouch", "crouch"],
        punch,
        punch,
        punch,
        kick,
        kick,
        high,
        bone,
        upper,
        upper,
        air_p,
        air_k,
        ["crouch_block", "crouch_block"],
        duck_p,
        duck_p,
        duck_p,
        duck_k,
        duck_k,
        duck_sweep,
        air_p,
        air_p,
        air_k,
        air_hk,
    ]
    assert len(rows) == ROWS - 1
    return rows


def build_sheet(cache: dict[str, Image.Image]) -> Image.Image:
    img = Image.new("RGBA", (CELL * COLS, CELL * ROWS), (0, 0, 0, 0))
    idle_bob = (0, 2, 3, 2, 0, 1)
    slouch = standing_scale(cache["idle"])
    for r, names in enumerate(sheet_rows()):
        for col, name in enumerate(names):
            if r == 1:
                cell = spu.place_walk(cache[name], cache["idle"], cell=CELL)
            elif name.startswith("fall_"):
                cell = spu.place_walk(cache[name], cache["idle"], cell=CELL)
            elif name == "crouch_sweep":
                cell = place(cache[name])
            elif name.startswith("crouch"):
                cell = place_slouch(cache[name], slouch)
            else:
                bob = idle_bob[col] if r == 0 else 0
                cell = place(cache[name], bob)
            img.paste(cell, (col * CELL, r * CELL))
    img.paste(place(cache["truck_0"]), (0, (ROWS - 1) * CELL))
    img.paste(place(cache["truck_1"]), (CELL, (ROWS - 1) * CELL))
    return img


def main():
    needed = [
        "idle",
        "walk_0",
        "walk_1",
        "walk_2",
        "walk_3",
        "walk_4",
        "walk_5",
        "walk_6",
        "walk_7",
        "jump_0",
        "jump_1",
        "jump_2",
        "jump_3",
        "crouch",
        "crouch_block",
        "crouch_punch",
        "crouch_kick",
        "crouch_sweep",
        "block",
        "hurt",
        "fall_0",
        "fall_1",
        "fall_2",
        "fall_knees",
        "punch_windup",
        "punch_hit",
        "punch_recover",
        "kick_windup",
        "kick_hit",
        "kick_recover",
        "high_kick",
        "uppercut",
        "bone",
        "truck_0",
        "truck_1",
    ]
    cache = {name: load_frame(name) for name in needed}
    os.makedirs(SPRITES, exist_ok=True)
    sheet_path = os.path.join(SPRITES, "jv.bmp")
    ms.save_argb(build_sheet(cache), sheet_path)
    print("wrote", sheet_path, flush=True)

    shots_path = os.path.join(SPRITES, "shots.bmp")
    if os.path.isfile(shots_path):
        shots = Image.open(shots_path).convert("RGBA")
    else:
        shots = Image.new("RGBA", (48 * len(ms.CHAR_ORDER), 48), (0, 0, 0, 0))
    if shots.size != (48 * len(ms.CHAR_ORDER), 48):
        raise SystemExit(f"unexpected shots.bmp size {shots.size}")
    shots.paste(bone_icon(), (JV_SHOT_INDEX * 48, 0))
    ms.save_argb(shots, shots_path)
    print("patched JV cell in", shots_path, flush=True)


if __name__ == "__main__":
    main()
