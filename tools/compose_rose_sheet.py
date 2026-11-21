"""Build sprites/rose.png from illustrated pose paintings.

Chroma-key green, fit into 192px cells with head anchored like Mike.
Ice FX poses (blast/dance/cast) keep secondary cyan shards.
"""

import os
from collections import deque

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "tools", "refs", "rose")
CELL = 192
COLS = 8
ROWS = 32
HEAD_X = 104
FOOT = 188

POSES = {
    "idle": "rose_idle_green.jpg",
    "walk": "rose_walk.jpg",
    "punch": "rose_punch.jpg",
    "kick": "rose_kick.jpg",
    "jump": "rose_jump.jpg",
    "block": "rose_block.jpg",
    "hurt": "rose_hurt.jpg",
    "down": "rose_down.jpg",
    "blast": "rose_blast.jpg",
    "dance": "rose_dance.jpg",
    "cast": "rose_cast.jpg",
    "fly": "rose_fly.jpg",
    "low": "rose_low.jpg",
}

FX_POSES = {"blast", "dance", "cast"}


def key_green(im, keep_fx=False):
    arr = np.array(im.convert("RGB")).astype(np.int16)
    r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
    mx = np.maximum(r, b)
    spill = g - mx
    alpha = np.full(spill.shape, 255, dtype=np.int16)
    alpha = np.where(spill > 18, 255 - (spill - 18) * 7, alpha)
    alpha = np.clip(alpha, 0, 255).astype(np.uint8)
    alpha = np.where(spill > 55, 0, alpha)
    g2 = np.minimum(g, mx + 6).astype(np.uint8)
    out = np.zeros(arr.shape[:2] + (4,), dtype=np.uint8)
    out[:, :, 0] = arr[:, :, 0].astype(np.uint8)
    out[:, :, 1] = np.where(spill > 0, g2, arr[:, :, 1].astype(np.uint8))
    out[:, :, 2] = arr[:, :, 2].astype(np.uint8)
    out[:, :, 3] = alpha
    if keep_fx:
        out[:, :, 3] = keep_significant(out[:, :, 3])
    else:
        out[:, :, 3] = keep_largest(out[:, :, 3])
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
    full = np.repeat(np.repeat(dil, s, 0), s, 1)
    full = full[: alpha.shape[0], : alpha.shape[1]]
    return np.where(full, alpha, 0).astype(np.uint8)


def keep_significant(alpha, min_cells=12):
    """Keep the body plus ice shard blobs."""
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
    full = np.repeat(np.repeat(dil, s, 0), s, 1)
    full = full[: alpha.shape[0], : alpha.shape[1]]
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
    band = xs[ys <= y0 + max(6, span * 0.07)]
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


def ice_bolt():
    """Cyan ice crystal projectile for ROW_SHOT."""
    im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = cy = CELL // 2
    # elongated crystal pointing right
    pts = [
        (cx - 70, cy),
        (cx - 20, cy - 28),
        (cx + 78, cy),
        (cx - 20, cy + 28),
    ]
    d.polygon(pts, fill=(140, 220, 255, 255))
    d.polygon([(cx - 20, cy - 28), (cx + 78, cy), (cx - 8, cy)], fill=(220, 245, 255, 255))
    d.polygon([(cx - 20, cy + 28), (cx + 78, cy), (cx - 8, cy)], fill=(70, 160, 220, 255))
    # inner shard
    d.polygon([(cx - 40, cy), (cx - 10, cy - 12), (cx + 40, cy), (cx - 10, cy + 12)],
              fill=(255, 255, 255, 220))
    return im


def load_poses():
    out = {}
    for name, fn in POSES.items():
        path = os.path.join(ASSETS, fn)
        print("key", name)
        keyed = key_green(Image.open(path), keep_fx=(name in FX_POSES))
        mode = "stand"
        if name == "fly":
            mode = "contain"
        elif name == "down":
            mode = "flat"
        th = 168 if name == "jump" else (170 if name in ("blast", "dance") else 178)
        out[name] = fit(keyed, mode=mode, target_h=th)
    out["bolt"] = ice_bolt()
    return out


def cycle(frames):
    return [frames[i % len(frames)] for i in range(COLS)]


def build(poses):
    idle, walk = poses["idle"], poses["walk"]
    punch, kick = poses["punch"], poses["kick"]
    jump, low = poses["jump"], poses["low"]
    block, hurt = poses["block"], poses["hurt"]
    down, blast = poses["down"], poses["blast"]
    dance, cast = poses["dance"], poses["cast"]
    fly = poses["fly"]
    sheet = Image.new("RGBA", (CELL * COLS, CELL * ROWS), (0, 0, 0, 0))

    rows = {
        0: [bob(idle, d) for d in (0, 1, 2, 1, 0, 1)],
        1: [idle, walk, nudge(idle, 3), nudge(walk, -4), idle, nudge(walk, 4), nudge(idle, -3), walk],
        2: [jump, jump, bob(jump, 4), bob(jump, 8), idle, idle],
        3: [block, block],
        4: [hurt, hurt, hurt, hurt, down, down, low, idle],
        5: [fly, nudge(fly, 2), nudge(fly, -2), fly],
        6: [low, low],
        7: [idle, idle, punch, punch, punch, idle],
        8: [idle, idle, punch, punch, punch, idle],
        9: [idle, punch, punch, punch, punch, idle],
        10: [idle, idle, kick, kick, kick, idle],
        11: [idle, idle, kick, kick, kick, idle],
        12: [idle, kick, kick, kick, kick, idle],
        13: [idle, cast, cast, cast, idle, idle],
        14: [idle, blast, blast, blast, blast, idle],
        # Super ice dance: cols 2-5 cycle while she rides across the stage.
        15: [idle, dance, dance, nudge(dance, 4), bob(nudge(dance, -3), 2),
            nudge(dance, -5), dance, idle],
        16: [jump, jump, bob(jump, 2), bob(jump, 2), jump, idle],
        17: [jump, jump, bob(jump, 3), bob(jump, 3), jump, idle],
        18: [low, low],
        29: [idle, cast, punch, punch, idle, idle],
        30: [dance, dance],
        31: [poses["bolt"]],
    }
    for r in range(19, 25):
        rows[r] = [low, low, nudge(low, 6), nudge(low, 8), low, low]
    for r in range(25, 29):
        rows[r] = [jump, jump, bob(jump, 2), bob(jump, 2), jump, idle]

    for r, frames in rows.items():
        cols = cycle(frames)
        for c, fr in enumerate(cols):
            sheet.paste(fr, (c * CELL, r * CELL), fr)
    return sheet


def main():
    poses = load_poses()
    sheet = build(poses)
    out = os.path.join(ROOT, "sprites", "rose.png")
    sheet.save(out)
    print("wrote", out, sheet.size)
    names = ["idle", "walk", "punch", "kick", "low", "jump", "block", "hurt",
             "down", "blast", "dance", "cast", "fly"]
    preview = Image.new("RGBA", (CELL * len(names), CELL), (40, 44, 58, 255))
    for i, n in enumerate(names):
        preview.paste(poses[n], (i * CELL, 0), poses[n])
    preview.resize((preview.width * 2, CELL * 2), Image.Resampling.NEAREST).save("/tmp/rose_cells.png")
    print("wrote /tmp/rose_cells.png")


if __name__ == "__main__":
    main()
