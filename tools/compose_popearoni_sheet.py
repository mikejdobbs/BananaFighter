"""Build sprites/popearoni.png from illustrated pose paintings.

Chroma-keys pure neon green. Fits 192px cells. Crouch uses a lower fit so
bent knees read clearly. Also writes sprites/background_popearoni.png.
"""

import os
from collections import deque

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "tools", "refs", "popearoni")
CELL = 192
COLS = 8
ROWS = 32
HEAD_X = 104
FOOT = 188

POSES = {
    "idle": "pope_idle_green.jpg",
    "walk": "pope_walk.jpg",
    "punch": "pope_punch.jpg",
    "kick": "pope_kick.jpg",
    "jump": "pope_jump.jpg",
    "block": "pope_block.jpg",
    "hurt": "pope_hurt.jpg",
    "down": "pope_down.jpg",
    "cast": "pope_cast.jpg",
    "demon": "pope_demon.jpg",
    "super": "pope_super.jpg",
    "fly": "pope_fly.jpg",
    "low": "pope_low.jpg",
}

FX_POSES = {"demon", "cast"}


def key_green(im, keep_fx=False):
    """Key pure #00FF00-style backdrop; keep white robe / skin."""
    arr = np.array(im.convert("RGB")).astype(np.int16)
    r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
    chroma = g.astype(np.int16) - np.maximum(r, b)
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


def pizza_icon():
    """Pepperoni pizza disc for ROW_SHOT."""
    im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = cy = CELL // 2
    d.ellipse((cx - 70, cy - 70, cx + 70, cy + 70), fill=(210, 150, 60, 255))
    d.ellipse((cx - 58, cy - 58, cx + 58, cy + 58), fill=(230, 70, 40, 255))
    d.ellipse((cx - 50, cy - 50, cx + 50, cy + 50), fill=(245, 210, 90, 255))
    for ox, oy, r in (
        (-22, -18, 12),
        (18, -10, 11),
        (-8, 20, 13),
        (24, 18, 10),
        (0, -28, 9),
        (-28, 8, 10),
        (10, 4, 11),
    ):
        d.ellipse((cx + ox - r, cy + oy - r, cx + ox + r, cy + oy + r), fill=(170, 40, 40, 255))
        d.ellipse(
            (cx + ox - r + 3, cy + oy - r + 2, cx + ox + r - 4, cy + oy + r - 5),
            fill=(200, 70, 55, 255),
        )
    return im


def mitre_icon():
    """Tall papal mitre for ROW_SHIP (flying hat)."""
    im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx = CELL // 2
    # pointed mitre body
    pts = [
        (cx - 48, 150),
        (cx - 42, 70),
        (cx, 28),
        (cx + 42, 70),
        (cx + 48, 150),
    ]
    d.polygon(pts, fill=(245, 245, 240, 255))
    d.line([(cx, 32), (cx, 150)], fill=(210, 175, 60, 255), width=5)
    d.ellipse((cx - 10, 55, cx + 10, 75), outline=(210, 175, 60, 255), width=3)
    # gold base band
    d.rectangle((cx - 50, 145, cx + 50, 162), fill=(210, 175, 60, 255))
    d.rectangle((cx - 48, 147, cx + 48, 160), fill=(235, 205, 90, 255))
    # outline
    d.line(pts + [pts[0]], fill=(40, 40, 45, 255), width=3)
    return im


def load_poses():
    out = {}
    for name, fn in POSES.items():
        path = os.path.join(ASSETS, fn)
        print("key", name)
        keyed = key_green(Image.open(path), keep_fx=(name in FX_POSES))
        mode = "stand"
        if name in ("fly", "demon"):
            mode = "contain"
        elif name == "down":
            mode = "flat"
        if name == "jump":
            th = 168
        elif name == "low":
            th = 132  # crouch sits lower — bent knees must read as a duck
        elif name in ("demon", "super", "cast"):
            th = 172
        else:
            th = 178
        out[name] = fit(keyed, mode=mode, target_h=th)
    out["pizza"] = pizza_icon()
    out["hat"] = mitre_icon()
    return out


def cycle(frames):
    return [frames[i % len(frames)] for i in range(COLS)]


def build(poses):
    idle, walk = poses["idle"], poses["walk"]
    punch, kick = poses["punch"], poses["kick"]
    jump, low = poses["jump"], poses["low"]
    block, hurt = poses["block"], poses["hurt"]
    down, cast = poses["down"], poses["cast"]
    demon, super_p = poses["demon"], poses["super"]
    fly = poses["fly"]
    hat = poses["hat"]
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
        13: [idle, cast, cast, cast, idle, idle],  # A_CAST pizza
        14: [idle, demon, demon, demon, demon, idle],  # A_SPECIAL demon rising
        15: [idle, super_p, super_p, super_p, super_p, idle],  # A_SUPER holy hat
        16: [jump, jump, bob(jump, 2), bob(jump, 2), jump, idle],
        17: [jump, jump, bob(jump, 3), bob(jump, 3), jump, idle],
        18: [low, low],
        29: [idle, cast, punch, punch, idle, idle],
        30: [hat, nudge(hat, 2)],
        31: [poses["pizza"]],
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


def write_background():
    src = os.path.join(ASSETS, "pope_bg.jpg")
    im = Image.open(src).convert("RGB")
    # Match other home stages: 1280x580
    w, h = 1280, 580
    im = im.resize((w, int(im.height * w / im.width)), Image.Resampling.LANCZOS)
    if im.height >= h:
        top = (im.height - h) // 2
        im = im.crop((0, top, w, top + h))
    else:
        canvas = Image.new("RGB", (w, h), (40, 36, 48))
        canvas.paste(im, (0, (h - im.height) // 2))
        im = canvas
    out = os.path.join(ROOT, "sprites", "background_popearoni.png")
    im.save(out)
    print("wrote", out, im.size)


def main():
    poses = load_poses()
    sheet = build(poses)
    out = os.path.join(ROOT, "sprites", "popearoni.png")
    sheet.save(out)
    print("wrote", out, sheet.size)
    write_background()
    names = [
        "idle", "walk", "punch", "kick", "low", "jump", "block", "hurt",
        "down", "cast", "demon", "super", "fly", "hat", "pizza",
    ]
    preview = Image.new("RGBA", (CELL * len(names), CELL), (40, 44, 58, 255))
    for i, n in enumerate(names):
        preview.paste(poses[n], (i * CELL, 0), poses[n])
    preview.resize((preview.width * 2, CELL * 2), Image.Resampling.NEAREST).save(
        "/tmp/popearoni_cells.png"
    )
    print("wrote /tmp/popearoni_cells.png")


if __name__ == "__main__":
    main()
