"""Build sprites/santa.png from painted poses (Mike-quality fighter art).

Chroma-keys neon green. Crouch uses a lower fit so bent knees read.
Also writes sprites/background_santa.png.
"""

from __future__ import annotations

import os
from collections import deque

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "tools", "refs", "santa")
CELL = 192
COLS = 8
ROWS = 32
HEAD_X = 104
FOOT = 188

POSES = {
    "idle": "santa_idle_green.jpg",
    "walk": "santa_walk.jpg",
    "punch": "santa_punch.jpg",
    "kick": "santa_kick.jpg",
    "jump": "santa_jump.jpg",
    "block": "santa_block.jpg",
    "hurt": "santa_hurt.jpg",
    "down": "santa_down.jpg",
    "cast": "santa_cast.jpg",
    "hammer_up": "santa_hammer_up.jpg",
    "hammer_slam": "santa_hammer_slam.jpg",
    "super": "santa_super.jpg",
    "fly": "santa_fly.jpg",
    "low": "santa_crouch.jpg",
    "crouch_punch": "santa_crouch_punch.jpg",
    "crouch_kick": "santa_crouch_kick.jpg",
    "gift": "santa_gift.jpg",
    "ship": "santa_krampus.jpg",
}

FX_POSES = {"hammer_up", "hammer_slam", "cast", "super", "gift", "ship"}


def key_green(im, keep_fx=False):
    arr = np.array(im.convert("RGB")).astype(np.int16)
    r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
    chroma = g.astype(np.int16) - np.maximum(r, b)
    # Neon green key; keep red suit / gold ribbon safe with red gate
    is_bg = (g > 160) & (r < 110) & (b < 110) & (chroma > 50)
    soft = (g > 140) & (r < 130) & (b < 130) & (chroma > 30) & ~is_bg
    alpha = np.full(g.shape, 255, dtype=np.uint8)
    alpha_f = alpha.astype(np.float64)
    alpha_f = np.where(soft, np.clip(255 - (chroma - 30) * 5, 0, 255), alpha_f)
    alpha = np.where(is_bg, 0, np.clip(alpha_f, 0, 255).astype(np.uint8))
    out = np.zeros(arr.shape[:2] + (4,), dtype=np.uint8)
    out[:, :, 0] = arr[:, :, 0].astype(np.uint8)
    out[:, :, 1] = arr[:, :, 1].astype(np.uint8)
    out[:, :, 2] = arr[:, :, 2].astype(np.uint8)
    out[:, :, 3] = alpha
    if keep_fx:
        out[:, :, 3] = keep_significant(out[:, :, 3])
    else:
        out[:, :, 3] = keep_largest(out[:, :, 3])
    return Image.fromarray(out, "RGBA")


def key_black(im):
    """Gift icon is on black — treat near-black as transparent."""
    arr = np.array(im.convert("RGB")).astype(np.int16)
    r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
    lum = (r.astype(np.int32) + g + b) / 3
    is_bg = lum < 18
    soft = (lum < 40) & ~is_bg
    alpha = np.full(g.shape, 255, dtype=np.uint8)
    alpha = np.where(soft, np.clip((lum - 18) * 12, 0, 255), alpha).astype(np.uint8)
    alpha = np.where(is_bg, 0, alpha)
    out = np.zeros(arr.shape[:2] + (4,), dtype=np.uint8)
    out[:, :, 0] = arr[:, :, 0].astype(np.uint8)
    out[:, :, 1] = arr[:, :, 1].astype(np.uint8)
    out[:, :, 2] = arr[:, :, 2].astype(np.uint8)
    out[:, :, 3] = keep_significant(alpha, min_cells=8)
    return Image.fromarray(out, "RGBA")


def _components(alpha, step=3):
    small = alpha[::step, ::step] > 24
    H, W = small.shape
    seen = np.zeros((H, W), dtype=bool)
    comps = []
    ys, xs = np.nonzero(small)
    for y, x in zip(ys.tolist(), xs.tolist()):
        if seen[y, x]:
            continue
        q = deque([(y, x)])
        seen[y, x] = True
        cells = []
        while q:
            cy, cx = q.popleft()
            cells.append((cy, cx))
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = cy + dy, cx + dx
                if 0 <= ny < H and 0 <= nx < W and small[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    q.append((ny, nx))
        comps.append(cells)
    return comps, step, small.shape


def keep_largest(alpha):
    comps, s, (H, W) = _components(alpha)
    if not comps:
        return alpha
    best = max(comps, key=len)
    keep = np.zeros((H, W), dtype=bool)
    for y, x in best:
        keep[y, x] = True
    dil = keep.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            dil |= np.roll(np.roll(keep, dy, 0), dx, 1)
    full = np.repeat(np.repeat(dil, s, 0), s, 1)[: alpha.shape[0], : alpha.shape[1]]
    return np.where(full, alpha, 0).astype(np.uint8)


def keep_significant(alpha, min_cells=10):
    comps, s, (H, W) = _components(alpha)
    keep = np.zeros((H, W), dtype=bool)
    for cells in comps:
        if len(cells) >= min_cells:
            for y, x in cells:
                keep[y, x] = True
    dil = keep.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            dil |= np.roll(np.roll(keep, dy, 0), dx, 1)
    full = np.repeat(np.repeat(dil, s, 0), s, 1)[: alpha.shape[0], : alpha.shape[1]]
    return np.where(full, alpha, 0).astype(np.uint8)


def bbox(im, thresh=16):
    a = np.array(im.split()[-1])
    ys, xs = np.nonzero(a > thresh)
    if len(xs) == 0:
        return (0, 0, im.width, im.height)
    return (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)


def crown_x(im):
    a = np.array(im.split()[-1])
    ys, xs = np.nonzero(a > 40)
    if len(xs) == 0:
        return im.width / 2
    y0 = ys.min()
    span = max(1, ys.max() - y0)
    band = xs[ys <= y0 + max(6, span * 0.08)]
    return float(band.mean()) if len(band) else float(xs.mean())


def fit(im, mode="stand", target_h=176):
    box = bbox(im)
    crop = im.crop(box)
    w, h = crop.size
    if mode == "contain":
        scale = min((CELL - 6) / w, (CELL - 6) / h)
    elif mode == "flat":
        scale = min((CELL - 8) / w, (CELL - 36) / h)
    else:
        scale = target_h / h
        if w * scale > CELL - 2:
            scale = (CELL - 2) / w
    nw = max(1, int(round(w * scale)))
    nh = max(1, int(round(h * scale)))
    crop = crop.resize((nw, nh), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    if mode == "flat":
        ox = (CELL - nw) // 2
        oy = CELL - nh - 6
    elif mode == "contain":
        ox = (CELL - nw) // 2
        oy = (CELL - nh) // 2
    else:
        ox = int(round(HEAD_X - crown_x(crop)))
        oy = FOOT - nh
        if ox < 0:
            ox = 0
        if ox + nw > CELL - 1:
            ox = CELL - 1 - nw
        if oy < 0:
            oy = 0
    canvas.paste(crop, (ox, oy), crop)
    return canvas


def bob(im, dy):
    out = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    out.paste(im, (0, dy), im)
    return out


def nudge(im, dx):
    out = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    out.paste(im, (dx, 0), im)
    return out


def load_poses():
    out = {}
    for name, fn in POSES.items():
        path = os.path.join(ASSETS, fn)
        print("key", name)
        if name == "gift":
            keyed = key_black(Image.open(path))
            out[name] = fit(keyed, mode="contain", target_h=140)
            continue
        keyed = key_green(Image.open(path), keep_fx=(name in FX_POSES))
        mode = "stand"
        if name in ("fly", "hammer_up", "hammer_slam", "ship"):
            mode = "contain"
        elif name == "down":
            mode = "flat"
        if name == "jump":
            th = 168
        elif name in ("low", "crouch_punch", "crouch_kick"):
            th = 128  # duck / crouch attacks stay low — bent knees
        elif name in ("hammer_up", "hammer_slam", "super", "cast"):
            th = 172
        else:
            th = 178
        out[name] = fit(keyed, mode=mode, target_h=th)
    return out


def cycle(frames):
    return [frames[i % len(frames)] for i in range(COLS)]


def build(poses):
    idle, walk = poses["idle"], poses["walk"]
    punch, kick = poses["punch"], poses["kick"]
    jump, low = poses["jump"], poses["low"]
    block, hurt = poses["block"], poses["hurt"]
    down, cast = poses["down"], poses["cast"]
    hu, hs = poses["hammer_up"], poses["hammer_slam"]
    super_p, fly = poses["super"], poses["fly"]
    cp, ck = poses["crouch_punch"], poses["crouch_kick"]

    sheet = Image.new("RGBA", (CELL * COLS, CELL * ROWS), (0, 0, 0, 0))
    rows = {
        0: [bob(idle, d) for d in (0, 1, 2, 1, 0, 1)],
        1: [idle, walk, nudge(idle, 3), nudge(walk, -4), idle, nudge(walk, 4), nudge(idle, -3), walk],
        2: [jump, jump, bob(jump, 4), bob(jump, 8), idle, idle],
        3: [block, block],
        4: [hurt, hurt, hurt, hurt, down, down, low, idle],
        5: [fly, nudge(fly, 2), nudge(fly, -2), fly],
        6: [low, low],  # duck — bent knees
        7: [idle, idle, punch, punch, punch, idle],
        8: [idle, idle, punch, punch, punch, idle],
        9: [idle, punch, punch, punch, punch, idle],
        10: [idle, idle, kick, kick, kick, idle],
        11: [idle, idle, kick, kick, kick, idle],
        12: [idle, kick, kick, kick, kick, idle],
        13: [idle, cast, cast, cast, idle, idle],  # gift fireball cast
        # Overhead sledgehammer: chamber above head, then slam downward
        14: [idle, hu, hu, hs, hs, idle],
        15: [idle, super_p, super_p, super_p, super_p, idle],
        16: [jump, jump, bob(jump, 2), bob(jump, 2), jump, idle],
        17: [jump, jump, bob(jump, 3), bob(jump, 3), jump, idle],
        18: [low, low],
        19: [low, low, cp, cp, cp, low],
        20: [low, low, cp, cp, cp, low],
        21: [low, cp, cp, cp, cp, low],
        22: [low, low, ck, ck, ck, low],
        23: [low, low, ck, ck, ck, low],
        24: [low, ck, ck, ck, ck, low],
        29: [idle, cast, punch, punch, idle, idle],
        30: [poses["ship"], nudge(poses["ship"], 3)],
        31: [poses["gift"]],
    }
    for r in range(25, 29):
        rows[r] = [jump, jump, bob(jump, 2), bob(jump, 2), jump, idle]

    for r, frames in rows.items():
        for c, fr in enumerate(cycle(frames)):
            sheet.paste(fr, (c * CELL, r * CELL), fr)
    return sheet


def write_background():
    src = os.path.join(ASSETS, "santa_bg.jpg")
    im = Image.open(src).convert("RGB")
    w, h = 1280, 580
    im = im.resize((w, int(im.height * w / im.width)), Image.Resampling.LANCZOS)
    if im.height >= h:
        top = (im.height - h) // 2
        im = im.crop((0, top, w, top + h))
    else:
        canvas = Image.new("RGB", (w, h), (20, 28, 48))
        canvas.paste(im, (0, (h - im.height) // 2))
        im = canvas
    out = os.path.join(ROOT, "sprites", "background_santa.png")
    im.save(out)
    print("wrote", out, im.size)


def main():
    poses = load_poses()
    sheet = build(poses)
    out = os.path.join(ROOT, "sprites", "santa.png")
    sheet.save(out)
    print("wrote", out, sheet.size)
    write_background()

    names = [
        "idle", "walk", "punch", "kick", "low", "crouch_punch", "crouch_kick",
        "jump", "block", "hurt", "down", "cast", "hammer_up", "hammer_slam",
        "super", "fly", "gift", "ship",
    ]
    preview = Image.new("RGBA", (CELL * len(names), CELL), (40, 44, 58, 255))
    for i, n in enumerate(names):
        preview.paste(poses[n], (i * CELL, 0), poses[n])
    preview.save("/tmp/santa_cells.png")
    print("wrote /tmp/santa_cells.png")


if __name__ == "__main__":
    main()
