"""Build sprites/pooh.png from illustrated pose paintings.

Chroma-keys pure neon green. Crouch uses a lower fit so bent knees read.
Also writes sprites/background_pooh.png.
"""

import os
from collections import deque

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "tools", "refs", "pooh")
CELL = 192
COLS = 8
ROWS = 32
HEAD_X = 104
FOOT = 188

POSES = {
    "idle": "pooh_idle_green.jpg",
    "walk": "pooh_walk.jpg",
    "punch": "pooh_punch.jpg",
    "kick": "pooh_kick.jpg",
    "jump": "pooh_jump.jpg",
    "block": "pooh_block.jpg",
    "hurt": "pooh_hurt.jpg",
    "down": "pooh_down.jpg",
    "cast": "pooh_cast.jpg",
    "blaze": "pooh_blaze.jpg",
    "super": "pooh_super.jpg",
    "fly": "pooh_fly.jpg",
    "low": "pooh_low.jpg",
    "crouch_punch": "pooh_crouch_punch.jpg",
    "crouch_kick": "pooh_crouch_kick.jpg",
}

FX_POSES = {"blaze", "cast", "super"}


def key_green(im, keep_fx=False):
    arr = np.array(im.convert("RGB")).astype(np.int16)
    r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
    chroma = g.astype(np.int16) - np.maximum(r, b)
    # Neon green key; yellow Pooh suit is high R+G so stay safe with red gate
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


def honey_jar():
    """Glass honey jar for ROW_SHOT."""
    im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = cy = CELL // 2
    d.ellipse((cx - 36, cy - 58, cx + 36, cy - 42), fill=(220, 200, 160, 255))
    d.rectangle((cx - 32, cy - 48, cx + 32, cy + 40), fill=(210, 150, 40, 255))
    d.rectangle((cx - 28, cy - 40, cx + 28, cy + 32), fill=(240, 185, 55, 255))
    d.rectangle((cx - 34, cy + 36, cx + 34, cy + 48), fill=(180, 120, 50, 255))
    d.ellipse((cx - 18, cy - 20, cx - 4, cy + 10), fill=(255, 230, 140, 180))
    return im


def honey_pot():
    """Honey pot for ROW_SHIP (super cue)."""
    im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = cy = CELL // 2
    d.ellipse((cx - 55, cy - 20, cx + 55, cy + 70), fill=(180, 120, 50, 255))
    d.ellipse((cx - 48, cy - 10, cx + 48, cy + 58), fill=(210, 150, 60, 255))
    d.ellipse((cx - 40, cy - 45, cx + 40, cy - 15), fill=(160, 100, 40, 255))
    d.ellipse((cx - 28, cy - 55, cx + 28, cy - 30), fill=(240, 190, 70, 255))
    # tiny bees
    for ox, oy in ((-50, -30), (48, -20), (10, -60)):
        d.ellipse((cx + ox - 6, cy + oy - 4, cx + ox + 6, cy + oy + 4), fill=(30, 28, 20, 255))
        d.ellipse((cx + ox - 2, cy + oy - 5, cx + ox + 2, cy + oy - 1), fill=(250, 210, 40, 255))
    return im


def load_poses():
    out = {}
    for name, fn in POSES.items():
        path = os.path.join(ASSETS, fn)
        print("key", name)
        keyed = key_green(Image.open(path), keep_fx=(name in FX_POSES))
        mode = "stand"
        if name in ("fly", "blaze"):
            mode = "contain"
        elif name == "down":
            mode = "flat"
        if name == "jump":
            th = 168
        elif name in ("low", "crouch_punch", "crouch_kick"):
            th = 132  # crouch attacks stay low so bent knees + strikes both read
        elif name in ("blaze", "super", "cast"):
            th = 172
        else:
            th = 178
        out[name] = fit(keyed, mode=mode, target_h=th)
    out["jar"] = honey_jar()
    out["pot"] = honey_pot()
    return out


def cycle(frames):
    return [frames[i % len(frames)] for i in range(COLS)]


def build(poses):
    idle, walk = poses["idle"], poses["walk"]
    punch, kick = poses["punch"], poses["kick"]
    jump, low = poses["jump"], poses["low"]
    block, hurt = poses["block"], poses["hurt"]
    down, cast = poses["down"], poses["cast"]
    blaze, super_p = poses["blaze"], poses["super"]
    fly = poses["fly"]
    cp, ck = poses["crouch_punch"], poses["crouch_kick"]
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
        14: [idle, blaze, blaze, blaze, blaze, idle],
        15: [idle, super_p, super_p, super_p, super_p, idle],
        16: [jump, jump, bob(jump, 2), bob(jump, 2), jump, idle],
        17: [jump, jump, bob(jump, 3), bob(jump, 3), jump, idle],
        18: [low, low],
        # Crouch LP/MP/HP (rows 19-21): client holds col 0→2→4 while ducking attacks.
        19: [low, low, cp, cp, cp, low],
        20: [low, low, cp, cp, cp, low],
        21: [low, cp, cp, cp, cp, low],
        # Crouch LK/MK/HK (rows 22-24)
        22: [low, low, ck, ck, ck, low],
        23: [low, low, ck, ck, ck, low],
        24: [low, ck, ck, ck, ck, low],
        29: [idle, cast, punch, punch, idle, idle],
        30: [poses["pot"], nudge(poses["pot"], 2)],
        31: [poses["jar"]],
    }
    for r in range(25, 29):
        rows[r] = [jump, jump, bob(jump, 2), bob(jump, 2), jump, idle]

    for r, frames in rows.items():
        cols = cycle(frames)
        for c, fr in enumerate(cols):
            sheet.paste(fr, (c * CELL, r * CELL), fr)
    return sheet


def write_background():
    src = os.path.join(ASSETS, "pooh_bg.jpg")
    im = Image.open(src).convert("RGB")
    w, h = 1280, 580
    im = im.resize((w, int(im.height * w / im.width)), Image.Resampling.LANCZOS)
    if im.height >= h:
        top = (im.height - h) // 2
        im = im.crop((0, top, w, top + h))
    else:
        canvas = Image.new("RGB", (w, h), (40, 36, 48))
        canvas.paste(im, (0, (h - im.height) // 2))
        im = canvas
    out = os.path.join(ROOT, "sprites", "background_pooh.png")
    im.save(out)
    print("wrote", out, im.size)


def main():
    poses = load_poses()
    sheet = build(poses)
    out = os.path.join(ROOT, "sprites", "pooh.png")
    sheet.save(out)
    print("wrote", out, sheet.size)
    write_background()
    names = [
        "idle", "walk", "punch", "kick", "low", "crouch_punch", "crouch_kick",
        "jump", "block", "hurt", "down", "cast", "blaze", "super", "fly", "pot", "jar",
    ]
    preview = Image.new("RGBA", (CELL * len(names), CELL), (40, 44, 58, 255))
    for i, n in enumerate(names):
        preview.paste(poses[n], (i * CELL, 0), poses[n])
    preview.resize((preview.width * 2, CELL * 2), Image.Resampling.NEAREST).save(
        "/tmp/pooh_cells.png"
    )
    print("wrote /tmp/pooh_cells.png")


if __name__ == "__main__":
    main()
