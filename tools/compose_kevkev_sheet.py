"""Build sprites/kevkev.png from illustrated pose paintings.

Each pose is keyed off chroma green, the stray hair-tie speck is dropped,
and the figure is fitted into the 192px cells Mike's sheet uses: feet on the
bottom of the cell, crown lined up with Mike's so the poses don't slide.
"""

import os
from collections import deque

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "tools", "refs", "kevkev")
CELL = 192
COLS = 8
ROWS = 32
HEAD_X = 104     # bald crown, matching Mike's idle head
FOOT = 188       # sole line

POSES = {
    "idle": "kev_idle_green.jpg",
    "walk": "kev_walk.jpg",
    "punch": "kev_punch.jpg",
    "kick": "kev_kick.jpg",
    "jump": "kev_jump.jpg",
    "block": "kev_block.jpg",
    "hurt": "kev_hurt.jpg",
    "down": "kev_down.jpg",
    "whip": "kev_whip.jpg",
    "lion": "kev_lion.jpg",
    "throw": "kev_throw.jpg",
    "fly": "kev_fly.jpg",
    "low": "kev_low.jpg",
}


def key_green(im):
    arr = np.array(im.convert("RGB")).astype(np.int16)
    r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
    mx = np.maximum(r, b)
    spill = g - mx
    alpha = np.full(spill.shape, 255, dtype=np.int16)
    alpha = np.where(spill > 18, 255 - (spill - 18) * 7, alpha)
    alpha = np.clip(alpha, 0, 255).astype(np.uint8)
    # Drop the hard backdrop. Partial edge pixels stay for a soft fringe.
    alpha = np.where(spill > 55, 0, alpha)
    g2 = np.minimum(g, mx + 6).astype(np.uint8)
    out = np.zeros(arr.shape[:2] + (4,), dtype=np.uint8)
    out[:, :, 0] = arr[:, :, 0].astype(np.uint8)
    out[:, :, 1] = np.where(spill > 0, g2, arr[:, :, 1].astype(np.uint8))
    out[:, :, 2] = arr[:, :, 2].astype(np.uint8)
    out[:, :, 3] = alpha
    out[:, :, 3] = keep_largest(out[:, :, 3])
    return Image.fromarray(out, "RGBA")


def keep_largest(alpha):
    """Keep the body. A loose hair-tie ring and jpeg specks are separate blobs."""
    s = 3
    small = alpha[::s, ::s] > 24
    H, W = small.shape
    seen = np.zeros((H, W), dtype=bool)
    best = []
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
        if len(cells) > len(best):
            best = cells
    keep = np.zeros((H, W), dtype=bool)
    for y, x in best:
        keep[y, x] = True
    # Dilate so thin hair that fell between samples survives.
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
    """Centre of the bald crown. The top sliver is skull, not the ankle-length hair."""
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
    elif mode == "lion":
        # Whole lion visible, rider still large enough to read.
        scale = min((CELL - 4) / w, (CELL - 8) / h)
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
    elif mode == "lion":
        # The whole lion fits the cell. His crown stays near the other poses.
        ox = int(round(HEAD_X - crown_x(crop)))
        oy = CELL - nh - 2
        if ox < 1:
            ox = 1
        if ox + nw > CELL - 1:
            ox = CELL - 1 - nw
    else:
        # Anchor the crown, then give up a few pixels if a fist or foot would clip.
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


def hair_tie():
    im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    mask = Image.new("L", (CELL, CELL), 0)
    d = ImageDraw.Draw(mask)
    cx = cy = CELL // 2
    r, inner = 62, 36
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    d.ellipse((cx - inner, cy - inner, cx + inner, cy + inner), fill=0)
    brown = Image.new("RGBA", (CELL, CELL), (110, 68, 40, 255))
    hi = Image.new("RGBA", (CELL, CELL), (168, 112, 70, 0))
    hd = ImageDraw.Draw(hi)
    hd.arc((cx - r + 6, cy - r + 6, cx + r - 10, cy + r - 14), 200, 340, fill=(168, 112, 70, 180), width=8)
    im.paste(brown, (0, 0), mask)
    im = Image.alpha_composite(im, hi)
    clasp = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    cd = ImageDraw.Draw(clasp)
    cd.rounded_rectangle((cx - 16, cy - r - 6, cx + 16, cy - r + 22), 6, fill=(46, 108, 196, 255))
    cd.rounded_rectangle((cx - 10, cy - r - 1, cx + 10, cy - r + 14), 4, fill=(186, 214, 245, 255))
    return Image.alpha_composite(im, clasp)


def load_poses():
    out = {}
    for name, fn in POSES.items():
        path = os.path.join(ASSETS, fn)
        print("key", name)
        keyed = key_green(Image.open(path))
        mode = "stand"
        if name in ("fly",):
            mode = "contain"
        elif name in ("down",):
            mode = "flat"
        elif name in ("lion",):
            mode = "lion"
        elif name in ("whip", "kick", "jump"):
            mode = "stand"
        out[name] = fit(keyed, mode=mode, target_h=168 if name == "jump" else 178)
    out["tie"] = hair_tie()
    return out


def cycle(frames):
    """Eight columns from a short list."""
    return [frames[i % len(frames)] for i in range(COLS)]


def build(poses):
    idle, walk = poses["idle"], poses["walk"]
    punch, kick = poses["punch"], poses["kick"]
    jump, low = poses["jump"], poses["low"]
    block, hurt = poses["block"], poses["hurt"]
    down, whip = poses["down"], poses["whip"]
    lion, throw = poses["lion"], poses["throw"]
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
        13: [idle, throw, throw, throw, idle, idle],
        14: [idle, whip, whip, whip, whip, idle],
        15: [idle, idle, lion, lion, idle, idle],
        16: [jump, jump, bob(jump, 2), bob(jump, 2), jump, idle],
        17: [jump, jump, bob(jump, 3), bob(jump, 3), jump, idle],
        18: [low, low],
        29: [idle, throw, throw, throw, idle, idle],
        30: [lion, lion],
        31: [poses["tie"]],
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
    out = os.path.join(ROOT, "sprites", "kevkev.png")
    sheet.save(out)
    print("wrote", out, sheet.size)
    # Contact sheet of the poses a player actually sees.
    names = ["idle", "walk", "punch", "kick", "low", "jump", "block", "hurt", "down", "whip", "lion", "throw", "fly"]
    preview = Image.new("RGBA", (CELL * len(names), CELL), (40, 44, 58, 255))
    for i, n in enumerate(names):
        preview.paste(poses[n], (i * CELL, 0), poses[n])
    preview.resize((preview.width * 2, CELL * 2), Image.Resampling.NEAREST).save("/tmp/kevkev_cells.png")
    print("wrote /tmp/kevkev_cells.png")


if __name__ == "__main__":
    main()
