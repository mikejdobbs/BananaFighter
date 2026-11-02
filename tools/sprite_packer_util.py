"""Shared helpers for packing painted fighter frames into 8×30 sprite sheets."""
from __future__ import annotations

from collections import deque

from PIL import Image, ImageDraw

CELL = 192


def gray_bg(r: int, g: int, b: int) -> bool:
    if max(r, g, b) - min(r, g, b) > 26:
        return False
    avg = (r + g + b) / 3
    return 55 < avg < 190


def knock_background(im: Image.Image) -> Image.Image:
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
        if not gray_bg(r, g, b):
            continue
        px[x, y] = (0, 0, 0, 0)
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    return im


def keep_largest(im: Image.Image) -> Image.Image:
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
    if not comps:
        return im

    def _bbox(comp: list[tuple[int, int]]) -> tuple[int, int, int, int]:
        xs = [p[0] for p in comp]
        ys = [p[1] for p in comp]
        return min(xs), min(ys), max(xs), max(ys)

    main = max(comps, key=len)
    keep = set(main)
    _, my0, _, my1 = _bbox(main)
    main_h = max(1, my1 - my0)
    for comp in comps:
        if comp is main:
            continue
        if len(comp) < max(400, len(main) // 40):
            continue
        x0, y0, x1, y1 = _bbox(comp)
        ch, cw = y1 - y0, x1 - x0
        # Drop a second full-size figure (e.g. a stray referee) but keep hair/bone props.
        if ch > main_h * 0.55 and cw > w * 0.12:
            continue
        keep.update(comp)
    for y in range(h):
        for x in range(w):
            if px[x, y][3] and (x, y) not in keep:
                px[x, y] = (0, 0, 0, 0)
    return im


def load_frame(frames_dir: str, name: str) -> Image.Image:
    import os

    path = os.path.join(frames_dir, f"{name}.png")
    if not os.path.isfile(path):
        raise SystemExit(f"missing frame: {path}")
    return keep_largest(knock_background(Image.open(path)))


def paste_scaled(im: Image.Image, scale: float, bob: int, cell: int = CELL) -> Image.Image:
    canvas = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
    bb = im.getbbox()
    if not bb:
        return canvas
    crop = im.crop(bb)
    nw = max(1, int(round(crop.width * scale)))
    nh = max(1, int(round(crop.height * scale)))
    crop = crop.resize((nw, nh), Image.LANCZOS)
    x = (cell - nw) // 2
    y = cell - nh - 4 - bob
    canvas.paste(crop, (x, max(0, y)), crop)
    return canvas


def standing_scale(idle: Image.Image, cell: int = CELL) -> float:
    bb = idle.getbbox()
    return (cell - 10) / (bb[3] - bb[1])


def place(im: Image.Image, bob: int = 0, cell: int = CELL) -> Image.Image:
    bb = im.getbbox()
    if not bb:
        return Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
    scale = min((cell - 10) / (bb[3] - bb[1]), (cell - 6) / (bb[2] - bb[0]))
    return paste_scaled(im, scale, bob, cell)


def place_slouch(im: Image.Image, scale: float, cell: int = CELL) -> Image.Image:
    return paste_scaled(im, scale, 0, cell)


def foot_bottom(im: Image.Image, *, center_frac: float = 0.6) -> int | None:
    """Lowest opaque scanline, using only the central band of the image."""
    im = im.convert("RGBA")
    w, h = im.size
    if w <= 0 or h <= 0:
        return None
    px = im.load()
    x0 = int(w * (0.5 - center_frac / 2))
    x1 = int(w * (0.5 + center_frac / 2))
    x0 = max(0, x0)
    x1 = min(w, max(x0 + 1, x1))
    for y in range(h - 1, -1, -1):
        for x in range(x0, x1):
            if px[x, y][3] >= 16:
                return y
    return None


def paste_foot_anchored(
    im: Image.Image,
    scale: float,
    bob: int = 0,
    *,
    floor_y: int | None = None,
    cell: int = CELL,
) -> Image.Image:
    canvas = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
    bb = im.getbbox()
    if not bb:
        return canvas
    if floor_y is None:
        floor_y = cell - 4
    crop = im.crop(bb)
    orig_h = max(1, bb[3] - bb[1])
    nw = max(1, int(round(crop.width * scale)))
    nh = max(1, int(round(orig_h * scale)))
    crop = crop.resize((nw, nh), Image.LANCZOS)
    foot_y = foot_bottom(im)
    if foot_y is None:
        foot_y = bb[3] - 1
    foot_from_top = (foot_y - bb[1]) / orig_h * nh
    x = (cell - nw) // 2
    y = int(round(floor_y - bob - foot_from_top))
    canvas.paste(crop, (x, max(0, y)), crop)
    return canvas


def place_walk(im: Image.Image, idle: Image.Image, bob: int = 0, cell: int = CELL) -> Image.Image:
    scale = standing_scale(idle, cell)
    return paste_foot_anchored(im, scale, bob, cell=cell)


def phase(windup: str, hit: str, recover: str) -> list[str]:
    return [windup, windup, hit, hit, recover, recover]


def standard_sheet_rows(*, special: list[str], ship: tuple[str, str]) -> list[list[str]]:
    """Same 29-row layout as JV; `special` replaces the projectile row (bone/hairtoss)."""
    punch = phase("punch_windup", "punch_hit", "punch_recover")
    kick = phase("kick_windup", "kick_hit", "kick_recover")
    high = phase("kick_windup", "high_kick", "kick_recover")
    upper = ["punch_windup", "uppercut", "uppercut", "uppercut", "punch_recover", "idle"]
    air_p = ["jump_0", "jump_1", "punch_hit", "punch_hit", "jump_2", "jump_3"]
    air_k = ["jump_0", "jump_1", "kick_hit", "kick_hit", "jump_2", "jump_3"]
    air_hk = ["jump_0", "jump_1", "high_kick", "high_kick", "jump_2", "jump_3"]
    duck_p = phase("crouch", "crouch_punch", "crouch")
    duck_k = phase("crouch", "crouch_kick", "crouch")
    duck_sweep = phase("crouch", "crouch_sweep", "crouch")
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
        special,
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
    return rows, ship


def build_sheet_from_rows(
    cache: dict[str, Image.Image],
    rows: list[list[str]],
    ship: tuple[str, str],
    *,
    cols: int,
    rows_total: int,
    cell: int = CELL,
) -> Image.Image:
    img = Image.new("RGBA", (cell * cols, cell * rows_total), (0, 0, 0, 0))
    idle_bob = (0, 2, 3, 2, 0, 1)
    slouch = standing_scale(cache["idle"], cell)
    for r, names in enumerate(rows):
        for col, name in enumerate(names):
            if r == 1:
                cell_im = place_walk(cache[name], cache["idle"], cell=cell)
            elif name.startswith("fall_"):
                cell_im = place_walk(cache[name], cache["idle"], cell=cell)
            elif name == "crouch_sweep":
                cell_im = place(cache[name], 0, cell)
            elif name.startswith("crouch"):
                cell_im = place_slouch(cache[name], slouch, cell)
            else:
                bob = idle_bob[col] if r == 0 else 0
                cell_im = place(cache[name], bob, cell)
            img.paste(cell_im, (col * cell, r * cell))
    img.paste(place(cache[ship[0]], cell=cell), (0, (rows_total - 1) * cell))
    img.paste(place(cache[ship[1]], cell=cell), (cell, (rows_total - 1) * cell))
    return img
