#!/usr/bin/env python3
"""make_sprites.py - draws every image the client uses into ./sprites (needs Pillow).

    python3 make_sprites.py            # everything
    python3 make_sprites.py ken alien  # just those characters
    python3 make_sprites.py misc bg    # shots/ship/fx, and the stage background

Everything is drawn at 3x size, given soft shading and a dark outline, then scaled down with a
high-quality filter. Sheets are 32-bit BMPs with a real alpha channel (smooth edges).

Fighter sheet: 8 columns of 192x192 cells, 29 rows
  0 idle (6)  1 walk (8)  2 jump: rise, apex, fall, land (4)  3 block (2)  4 hurt (3)
  5 fly (4, only characters with 'fly')  6 crouch (2)
  7..17 attacks, 6 frames each (windup 2, impact 2, follow-through 2), in this order:
        light punch, medium punch, heavy punch, light kick, medium kick, heavy kick,
        projectile/cast, special, super, jump punch (medium), jump kick (medium)
                                                         (move_row() in sim.h picks the row)
18 crouching block (2)
19..24 crouching attacks, 6 frames each: light/medium/heavy punch, light/medium/heavy kick
25..28 extra jump attacks, 6 frames each: light punch, heavy punch, light kick, heavy kick
       (jump punch / jump kick above are the medium ones)
'anims' below names the pose kind drawn for rows 7..17. 'canims' (rows 19..24) and 'xanims'
(rows 25..28) are optional per-character lists; CANIMS / XANIMS are the defaults.
shots.bmp: one 48x48 cell per character (config.h order).  ship.bmp: two 192x96 frames.
fx.bmp: four 96x96 cells (bomb, three explosion frames).  background.bmp: the 1280x580 stage.
"""

import math, os, random, struct, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops, ImageOps

CELL, COLS, ROWS = 192, 8, 29
SS = 3  # supersampling factor
K = CELL * SS / 64.0  # figures are designed in a 64-unit space
CX = 26  # figure centre inside a cell: leaves room for reach on the facing side
OUTLINE = (22, 20, 34)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sprites")

# One entry per character, in the same order as config.h; the file name is the lower-case name.
CHARS = {
    "ken": dict(
        skin=(235, 190, 150),
        top=(200, 40, 40),
        arm_col=(235, 190, 150),
        pants=(240, 240, 240),
        shoe=(60, 40, 30),
        torso_w=9,
        arm_w=5,
        leg_w=6,
        head_r=7,
        torso=17,
        arm=17,
        leg=22,
        anims=[
            "jab",
            "strike",
            "hook",
            "low_kick",
            "kick",
            "high_kick",
            "cast",
            "uppercut",
            "smash",
            "air_punch",
            "air_kick",
        ],
    ),
    "tank": dict(
        skin=(210, 170, 140),
        top=(90, 100, 120),
        arm_col=(90, 100, 120),
        pants=(60, 65, 80),
        shoe=(40, 40, 50),
        torso_w=20,
        arm_w=9,
        leg_w=10,
        head_r=8,
        torso=17,
        arm=17,
        leg=20,
        anims=[
            "jab",
            "strike",
            "pound",
            "low_kick",
            "kick",
            "high_kick",
            "cast",
            "strike",
            "smash",
            "air_punch",
            "air_kick",
        ],
        canims=["c_lp", "c_mp", "c_pound", "c_lk", "c_mk", "c_hk"],
    ),
    "ninja": dict(
        skin=(235, 190, 150),
        top=(30, 35, 60),
        arm_col=(30, 35, 60),
        pants=(30, 35, 60),
        shoe=(20, 20, 30),
        torso_w=6,
        arm_w=4,
        leg_w=5,
        head_r=6,
        torso=17,
        arm=17,
        leg=23,
        anims=[
            "jab",
            "strike",
            "slash",
            "low_kick",
            "kick",
            "high_kick",
            "star",
            "vanish",
            "smash",
            "air_punch",
            "air_kick",
        ],
    ),
    "monkey": dict(
        skin=(190, 140, 90),
        top=(110, 70, 40),
        arm_col=(110, 70, 40),
        pants=(110, 70, 40),
        shoe=(90, 55, 30),
        torso_w=16,
        arm_w=8,
        leg_w=8,
        head_r=7,
        torso=16,
        arm=24,
        leg=18,
        anims=[
            "jab",
            "strike",
            "pound",
            "low_kick",
            "kick",
            "high_kick",
            "cast",
            "grab",
            "smash",
            "air_punch",
            "air_kick",
        ],
        canims=["c_lp", "c_mp", "c_pound", "c_lk", "c_mk", "c_hk"],
    ),
    "alien": dict(
        skin=(120, 210, 120),
        top=(150, 155, 175),
        arm_col=(120, 210, 120),
        pants=(150, 155, 175),
        shoe=(90, 95, 110),
        torso_w=7,
        arm_w=4,
        leg_w=4,
        head_r=10,
        torso=13,
        arm=14,
        leg=18,
        anims=[
            "jab",
            "strike",
            "hook",
            "low_kick",
            "kick",
            "high_kick",
            "laser",
            "strike",
            "summon",
            "air_punch",
            "air_kick",
        ],
    ),
    "squirrel": dict(
        skin=(222, 140, 70),
        top=(205, 125, 62),
        arm_col=(210, 130, 66),
        pants=(190, 110, 52),
        shoe=(110, 65, 35),
        torso_w=11,
        arm_w=5,
        leg_w=6,
        head_r=8,
        torso=12,
        arm=13,
        leg=15,
        fly=True,
        anims=[
            "claw",
            "claw",
            "hook",
            "low_kick",
            "kick",
            "high_kick",
            "nut",
            "strike",
            "spray",
            "air_punch",
            "air_kick",
        ],
        canims=["c_claw", "c_claw", "c_hp", "c_lk", "c_mk", "c_hk"],
    ),
}
FUR, FUR_D, FUR_L = (222, 140, 70), (178, 98, 44), (246, 214, 168)

# ------------------------------------------------------------------ drawing canvas


class Cv:
    """Draws in design units; every coordinate and width is scaled by k."""

    def __init__(self, w, h, k):
        self.k = k
        self.img = Image.new("RGBA", (int(w * k), int(h * k)), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def _s(self, v):
        k = self.k
        if v and isinstance(v[0], (tuple, list)):
            return [(x * k, y * k) for x, y in v]
        return [x * k for x in v]

    def _w(self, w):
        return max(1, int(round(w * self.k)))

    def line(self, pts, fill, width=1, joint=None):
        self.d.line(self._s(pts), fill=fill, width=self._w(width), joint=joint)

    def ellipse(self, box, fill=None, outline=None, width=1):
        self.d.ellipse(self._s(box), fill=fill, outline=outline, width=self._w(width))

    def rectangle(self, box, fill):
        self.d.rectangle(self._s(box), fill=fill)

    def polygon(self, pts, fill):
        self.d.polygon(self._s(pts), fill=fill)

    def pieslice(self, box, a, b, fill):
        self.d.pieslice(self._s(box), a, b, fill=fill)

    def arc(self, box, a, b, fill, width=1):
        self.d.arc(self._s(box), a, b, fill=fill, width=self._w(width))


_shade = {}


def shade_layer(size):
    if size not in _shade:
        g = Image.linear_gradient("L").resize(size)
        g = ImageOps.invert(g).point(
            lambda v: 205 + v * 50 // 255
        )  # bright top, darker bottom
        _shade[size] = Image.merge("RGB", (g, g, g))
    return _shade[size]


def finish(img, outline=2.0, shade=True):
    """Soft top-to-bottom shading plus a dark sticker outline around the silhouette."""
    size = img.size
    a = img.getchannel("A")
    rgb = img.convert("RGB")
    if shade:
        rgb = ImageChops.multiply(rgb, shade_layer(size))
    body = rgb.copy()
    body.putalpha(a)
    r = outline * SS
    dil = a.filter(ImageFilter.GaussianBlur(r / 1.8)).point(
        lambda v: 255 if v > 5 else 0
    )
    base = Image.new("RGBA", size, OUTLINE + (0,))
    base.putalpha(dil)
    base.alpha_composite(body)
    return base


def save_argb(img, path):
    """32-bit BMP with a V4 header and an alpha mask, which SDL_LoadBMP reads as ARGB."""
    img = img.convert("RGBA")
    w, h = img.size
    pix = img.tobytes("raw", "BGRA")
    v4 = struct.pack("<IiiHHIIiiII", 108, w, -h, 1, 32, 3, len(pix), 2835, 2835, 0, 0)
    v4 += struct.pack("<IIII", 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    v4 += struct.pack("<I", 0x73524742) + b"\x00" * 36 + b"\x00" * 12
    assert len(v4) == 108
    head = b"BM" + struct.pack("<IHHI", 14 + 108 + len(pix), 0, 0, 14 + 108)
    with open(path, "wb") as f:
        f.write(head + v4 + pix)


# ------------------------------------------------------------------ poses and tweening

# lean (deg, + = forward), crouch (px), dx, tail (idle tail sway), rot (flying tail angle),
# sq (squat: hip drop as a fraction of leg length; knees bend so the feet stay on the ground),
# kick (with sq: keep the explicit front leg, only the back leg squats),
# af/ab = front/back arm (upper, lower angle), lf/lb = front/back leg. Angles are degrees from
# straight down, + = toward the facing side.
DEF = dict(
    lean=0,
    crouch=0,
    dx=0,
    tail=0,
    rot=0,
    fly=False,
    fx=None,
    sq=0,
    kick=False,
    af=(15, 30),
    ab=(-12, -25),
    lf=(4, 0),
    lb=(-4, 0),
)


def P(**kw):
    d = dict(DEF)
    d.update(kw)
    return d


NUM = ("lean", "crouch", "dx", "tail", "sq")
TUP = ("af", "ab", "lf", "lb")


def tween(a, b, t):
    r = dict(a)
    for k in NUM:
        r[k] = a[k] + (b[k] - a[k]) * t
    for k in TUP:
        r[k] = tuple(a[k][i] + (b[k][i] - a[k][i]) * t for i in (0, 1))
    r["fx"] = b["fx"] if t >= 0.999 else a["fx"]
    return r


def idle_frames(n=6):  # breathing
    out = []
    for i in range(n):
        s = math.sin(2 * math.pi * i / n)
        out.append(
            P(
                crouch=0.7 + 0.7 * s,
                af=(15 + 4 * s, 30 + 6 * s),
                ab=(-12 - 3 * s, -25 - 5 * s),
                tail=12 * s,
            )
        )
    return out


def walk_frames(n=8):  # full stride with knee bend and arm swing
    out = []
    for i in range(n):
        phi = 2 * math.pi * i / n
        s, c = math.sin(phi), math.cos(phi)

        def leg(sg, cg):
            th = 30 * sg
            return (th, th - 32 * max(0.0, cg))

        def arm(sg):
            a = -28 * sg
            return (a, a + 14 + 10 * abs(sg))

        out.append(
            P(
                lf=leg(s, c),
                lb=leg(-s, -c),
                af=arm(s),
                ab=arm(-s),
                crouch=1.5 * abs(c),
                lean=3,
                tail=10 * s,
            )
        )
    return out


JUMP = [
    P(lf=(40, -10), lb=(-10, -50), af=(150, 160), ab=(140, 150), tail=-8),  # rise
    P(
        lf=(30, -30), lb=(-25, -45), af=(110, 130), ab=(100, 120), lean=4, tail=0
    ),  # apex
    P(
        dx=5, lf=(20, 20), lb=(-20, -20), af=(120, 110), ab=(-120, -110), tail=10
    ),  # fall
    P(crouch=5, lf=(45, -20), lb=(-45, 20), af=(70, 40), ab=(60, 30), lean=8),
]  # land
BLK = P(crouch=3, lean=-4, af=(130, 80), ab=(120, 70), lf=(25, 10), lb=(-20, -10))
BLOCK = [tween(P(), BLK, 0.6), BLK]
HRT = P(lean=-18, dx=6, af=(-70, -100), ab=(-50, -80), lf=(15, 5), lb=(-20, -10))
HURT = [
    HRT,
    P(lean=-12, dx=5, af=(-50, -80), ab=(-35, -65), lf=(12, 4), lb=(-16, -8)),
    tween(HRT, P(), 0.6),
]
CROUCH = dict(crouch=4, lf=(45, -20), lb=(-45, 20), af=(110, 50), ab=(100, 40))
CRH = P(
    crouch=11, lean=3, af=(95, 55), ab=(90, 50), lf=(38, -18), lb=(-38, 18)
)  # deep low guard: about half height, dodges punches and shots
CROUCH_ROW = [tween(P(), CRH, 0.6), CRH]
CBLK = P(sq=0.5, lean=-2, af=(130, 80), ab=(120, 70))  # down + away: low guard
CROUCH_BLOCK_ROW = [tween(P(), CBLK, 0.6), CBLK]


def fly_frames():  # tail spinning like a rotor
    out = []
    for rot in (0, 90, 180, 270):
        sw = math.sin(math.radians(rot))
        out.append(
            P(
                fly=True,
                rot=rot,
                crouch=-1,
                lf=(12 + 5 * sw, -20),
                lb=(-12 - 4 * sw, -30),
                af=(55, 70),
                ab=(40, 60),
            )
        )
    return out


# windup, impact, follow-through for every kind of attack
MOVES = {
    "jab": [
        P(lean=-3, af=(40, 60), ab=(10, 20)),
        P(lean=7, af=(88, 88), ab=(-20, -35), fx="spark"),
        P(lean=2, af=(55, 65), ab=(0, -15)),
    ],
    "strike": [
        P(lean=-6, af=(-60, -90), ab=(20, 40)),
        P(lean=10, af=(90, 90), ab=(-30, -50), lf=(20, 10), lb=(-25, -20), fx="spark"),
        P(lean=4, af=(60, 70), ab=(-10, -30)),
    ],
    "hook": [
        P(lean=-10, af=(-80, -110), ab=(20, 40)),
        P(lean=16, af=(100, 95), ab=(-35, -55), lf=(22, 12), lb=(-28, -22), fx="spark"),
        P(lean=6, af=(65, 75), ab=(-12, -30)),
    ],
    "low_kick": [
        P(lean=-4, lf=(25, -20), lb=(-5, 0), af=(35, 55), ab=(-30, -40)),
        P(lean=-8, dx=4, lf=(75, 80), lb=(-4, 0), af=(50, 80), ab=(-35, -50)),
        P(lean=-2, lf=(30, 5), lb=(-4, 0)),
    ],
    "kick": [
        P(lean=-8, lf=(40, -30), lb=(-5, 0), af=(40, 60), ab=(-40, -50)),
        P(lean=-14, dx=6, lf=(90, 90), lb=(-4, 0), af=(60, 100), ab=(-45, -62)),
        P(lean=-4, lf=(40, 10), lb=(-4, 0)),
    ],
    "high_kick": [
        P(lean=-10, dx=5, lf=(55, -35), lb=(-6, 0), af=(30, 50), ab=(-35, -45)),
        P(lean=-24, dx=8, lf=(125, 115), lb=(-4, 0), af=(50, 110), ab=(-48, -64)),
        P(lean=-8, lf=(60, 20), lb=(-4, 0)),
    ],
    "uppercut": [
        P(crouch=5, lean=4, lf=(40, -20), lb=(-35, 15), af=(20, 10), ab=(0, -10)),
        P(
            lean=-4,
            crouch=-3,
            af=(172, 176),
            ab=(-30, -50),
            lf=(15, 5),
            lb=(-20, -10),
            fx="spark",
        ),
        P(lean=-6, crouch=-1, af=(150, 160), ab=(-25, -40)),
    ],
    "air_punch": [
        P(lean=6, lf=(30, -20), lb=(-15, -45), af=(100, 110), ab=(60, 80)),
        P(lean=10, lf=(25, -25), lb=(-20, -50), af=(105, 100), ab=(40, 70), fx="spark"),
        P(lean=4, lf=(30, -15), lb=(-15, -40), af=(70, 80), ab=(50, 60)),
    ],
    "air_kick": [
        P(lean=-4, lf=(70, -40), lb=(-25, -60), af=(80, 110), ab=(30, 60)),
        P(lean=-8, dx=4, lf=(58, 48), lb=(-35, -70), af=(100, 130), ab=(-10, 40)),
        P(lean=-2, lf=(50, 10), lb=(-20, -50), af=(70, 100), ab=(40, 60)),
    ],
    "cast": [
        P(lean=-6, af=(-60, -80), ab=(-40, -70)),
        P(lean=8, af=(90, 90), ab=(85, 85), fx="glow"),
        P(lean=2, af=(60, 60), ab=(50, 50)),
    ],
    "grab": [
        P(lean=2, crouch=2, af=(50, 80), ab=(40, 70)),
        P(lean=14, af=(110, 100), ab=(100, 95)),
        P(lean=6, af=(80, 60), ab=(70, 50)),
    ],
    "smash": [
        P(lean=-10, crouch=3, af=(170, 175), ab=(165, 172)),
        P(
            lean=22,
            crouch=4,
            af=(40, 20),
            ab=(35, 15),
            lf=(25, 10),
            lb=(-25, -10),
            fx="impact",
        ),
        P(lean=10, af=(30, 15), ab=(25, 10)),
    ],
    "pound": [
        P(lean=-6, af=(120, 150), ab=(110, 140)),
        P(lean=18, af=(50, 25), ab=(40, 20), fx="impact"),
        P(lean=8, af=(30, 15), ab=(20, 10)),
    ],
    "slash": [
        P(lean=-8, dx=5, af=(-110, -125), ab=(10, 20), fx="blade"),
        P(
            lean=12,
            af=(95, 100),
            ab=(-30, -40),
            lf=(25, 15),
            lb=(-25, -15),
            fx="blade+arc",
        ),
        P(lean=4, af=(60, 70), ab=(-10, -20), fx="blade"),
    ],
    "star": [
        P(lean=-6, af=(-110, -130), ab=(30, 40), fx="star"),
        P(lean=8, af=(80, 85), ab=(-20, -30)),
        P(lean=2, af=(50, 60), ab=(-10, -20)),
    ],
    "vanish": [P(**CROUCH), P(fx="smoke", **CROUCH), P(**CROUCH)],
    "laser": [
        P(lean=-2, af=(60, 70), ab=(-10, -20), fx="gun"),
        P(lean=4, af=(90, 90), ab=(-15, -25), fx="gun+flash"),
        P(lean=0, af=(70, 80), ab=(-10, -20), fx="gun"),
    ],
    "summon": [
        P(af=(160, 170), ab=(-12, -25), fx="beacon"),
        P(af=(170, 175), ab=(165, 172), fx="beacon+beam"),
        P(af=(140, 150), ab=(-12, -25), fx="beacon"),
    ],
    "claw": [
        P(lean=-6, af=(-60, -90), ab=(20, 40), tail=-14),
        P(lean=12, af=(95, 90), ab=(-30, -50), fx="claws", tail=14),
        P(lean=4, af=(60, 70), ab=(-10, -30)),
    ],
    "nut": [
        P(lean=-6, af=(-110, -130), ab=(20, 30), fx="nut", tail=-12),
        P(lean=10, af=(85, 80), ab=(-20, -30), fx="nut_fly", tail=12),
        P(lean=2, af=(50, 60), ab=(-10, -20)),
    ],
    "spray": [
        P(lean=-8, crouch=2, af=(-80, -110), ab=(-60, -100), fx="nut", tail=-16),
        P(lean=12, af=(100, 95), ab=(95, 90), fx="nuts", tail=16),
        P(lean=4, af=(60, 70), ab=(55, 60)),
    ],
    # ---- crouching attacks (sq = squat: hips drop, knees bend, feet stay planted) ----
    "c_lp": [
        P(sq=0.5, lean=-2, af=(40, 60), ab=(10, 20)),
        P(sq=0.5, lean=6, af=(88, 88), ab=(-20, -35), fx="spark"),
        P(sq=0.5, lean=2, af=(55, 65), ab=(0, -15)),
    ],
    "c_mp": [
        P(sq=0.5, lean=-6, af=(-50, -80), ab=(20, 40)),
        P(sq=0.5, lean=10, af=(85, 85), ab=(-30, -50), fx="spark"),
        P(sq=0.5, lean=4, af=(60, 70), ab=(-10, -30)),
    ],
    "c_hp": [
        P(sq=0.6, lean=-8, af=(15, 5), ab=(0, -10)),  # rising uppercut
        P(sq=0.45, lean=-2, af=(150, 155), ab=(-30, -50), fx="spark"),
        P(sq=0.5, lean=-4, af=(120, 130), ab=(-25, -40)),
    ],
    "c_claw": [
        P(sq=0.5, lean=-4, af=(-40, -70), ab=(20, 40), tail=-14),
        P(sq=0.5, lean=10, af=(90, 88), ab=(-30, -50), fx="claws", tail=14),
        P(sq=0.5, lean=4, af=(55, 65), ab=(-10, -30)),
    ],
    "c_pound": [
        P(sq=0.5, lean=-6, af=(120, 150), ab=(110, 140)),  # overhead hammer, low stance
        P(sq=0.5, lean=16, af=(55, 30), ab=(45, 25), fx="impact"),
        P(sq=0.5, lean=8, af=(30, 15), ab=(20, 10)),
    ],
    "c_lk": [
        P(sq=0.5, kick=True, lean=-2, lf=(30, -20), af=(35, 55), ab=(-30, -40)),
        P(sq=0.5, kick=True, lean=-6, lf=(78, 80), af=(50, 80), ab=(-35, -50)),
        P(sq=0.5, kick=True, lean=-2, lf=(35, 0), af=(35, 55), ab=(-30, -40)),
    ],
    "c_mk": [
        P(sq=0.5, kick=True, lean=-4, lf=(40, -25), af=(40, 60), ab=(-40, -50)),
        P(sq=0.5, kick=True, lean=-10, dx=3, lf=(88, 90), af=(55, 90), ab=(-40, -55)),
        P(sq=0.5, kick=True, lean=-4, lf=(40, 5), af=(40, 60), ab=(-40, -50)),
    ],
    "c_hk": [
        P(
            sq=0.55, kick=True, lean=-6, lf=(50, -30), af=(30, 50), ab=(-35, -45)
        ),  # sweep
        P(sq=0.6, kick=True, lean=-20, dx=6, lf=(100, 100), af=(45, 95), ab=(-40, -70)),
        P(sq=0.5, kick=True, lean=-8, lf=(55, 10), af=(40, 60), ab=(-35, -45)),
    ],
    # ---- extra jump attacks (the medium ones are 'air_punch' / 'air_kick') ----
    "air_lp": [
        P(lean=4, lf=(25, -15), lb=(-12, -40), af=(70, 80), ab=(40, 60)),
        P(lean=7, lf=(22, -18), lb=(-15, -45), af=(88, 88), ab=(30, 55), fx="spark"),
        P(lean=3, lf=(25, -12), lb=(-12, -38), af=(60, 70), ab=(40, 55)),
    ],
    "air_hp": [
        P(
            lean=-4, lf=(30, -20), lb=(-15, -45), af=(160, 170), ab=(150, 160)
        ),  # overhead smash
        P(lean=16, lf=(25, -25), lb=(-20, -50), af=(55, 30), ab=(45, 25), fx="spark"),
        P(lean=8, lf=(30, -15), lb=(-15, -40), af=(40, 25), ab=(30, 20)),
    ],
    "air_lk": [
        P(
            lean=-2, lf=(65, -50), lb=(-20, -55), af=(60, 80), ab=(30, 50)
        ),  # knee, then snap
        P(lean=-5, dx=2, lf=(70, 45), lb=(-30, -65), af=(80, 105), ab=(10, 40)),
        P(lean=-2, lf=(45, 5), lb=(-20, -50), af=(60, 80), ab=(40, 55)),
    ],
    "air_hk": [
        P(
            lean=-6, lf=(75, -40), lb=(-30, -65), af=(70, 100), ab=(20, 50)
        ),  # big diving kick
        P(lean=-22, dx=6, lf=(78, 74), lb=(-40, -75), af=(110, 140), ab=(-20, 30)),
        P(lean=-8, lf=(55, 15), lb=(-25, -55), af=(80, 100), ab=(40, 60)),
    ],
}

# default animation kinds for the new rows (per-character lists 'canims' / 'xanims' override these)
CANIMS = ["c_lp", "c_mp", "c_hp", "c_lk", "c_mk", "c_hk"]  # rows 19..24
XANIMS = ["air_lp", "air_hp", "air_lk", "air_hk"]  # rows 25..28


def move_frames(kind):
    w, h, r = MOVES[kind]
    n = P()
    return [
        tween(n, w, 0.5),
        w,
        tween(w, h, 0.7),
        h,
        tween(h, r, 0.5),
        tween(r, n, 0.5),
    ]


# ------------------------------------------------------------------ the figures


def shade_c(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c)


def seg(p, ang, ln):
    a = math.radians(ang)
    return (p[0] + ln * math.sin(a), p[1] + ln * math.cos(a))


def ring(q, r):
    return [q[0] - r, q[1] - r, q[0] + r, q[1] + r]


def limb(cv, p, angs, ln, w, col):
    p1 = seg(p, angs[0], ln / 2)
    p2 = seg(p1, angs[1], ln / 2)
    cv.line([p, p1, p2], fill=col, width=w, joint="curve")
    for q in (p, p1, p2):
        cv.ellipse(ring(q, w / 2), fill=col)
    return p2


def foot(cv, p, col):
    cv.rectangle([p[0] - 2, p[1] - 2, p[0] + 5, p[1] + 1], fill=col)


def star(cv, p, r):
    cv.polygon(
        [seg(p, 45 * i, r if i % 2 == 0 else r * 0.4) for i in range(8)],
        fill=(195, 200, 210),
    )
    cv.ellipse(ring(p, 1), fill=(40, 40, 50))


def acorn(cv, c, s=1.0):
    x, y = c
    cv.ellipse(
        [x - 3.5 * s, y - 2.5 * s, x + 3.5 * s, y + 4.5 * s], fill=(176, 116, 58)
    )
    cv.pieslice(
        [x - 4 * s, y - 5 * s, x + 4 * s, y + 2 * s], 180, 360, fill=(112, 72, 40)
    )
    cv.line(
        [(x, y - 5 * s), (x + 0.6 * s, y - 7 * s)], fill=(90, 58, 32), width=1.2 * s
    )


def squirrel_tail(cv, c, hip, hd, p):
    base = (hip[0] - 3, hip[1] - 3)
    if p["fly"]:  # the tail becomes a rotor above the head
        hub = (hd[0], hd[1] - c["head_r"] - 7)
        cv.line(
            [base, (base[0] - 5, (base[1] + hub[1]) / 2), hub],
            fill=FUR_D,
            width=4,
            joint="curve",
        )
        rot = math.radians(p["rot"])
        R, fl = 19, 0.2
        cv.ellipse(
            [hub[0] - R, hub[1] - R * fl, hub[0] + R, hub[1] + R * fl],
            outline=FUR_L,
            width=1.1,
        )
        tip = (hub[0] + R * math.cos(rot), hub[1] + R * fl * math.sin(rot))
        for f, r, col in ((0.30, 4.2, FUR), (0.62, 4.6, FUR_D), (1.00, 4.2, FUR_L)):
            cv.ellipse(
                ring(
                    (hub[0] + (tip[0] - hub[0]) * f, hub[1] + (tip[1] - hub[1]) * f), r
                ),
                fill=col,
            )
        cv.ellipse(ring(hub, 3), fill=FUR_D)
        return
    t = p["tail"]
    pts = [
        (base[0], base[1]),
        (base[0] - 5 + t * 0.06, base[1] - 7),
        (base[0] - 9 + t * 0.15, base[1] - 15),
        (base[0] - 8 + t * 0.28, base[1] - 23),
        (base[0] - 3 + t * 0.40, base[1] - 30),
    ]
    for q, r, col in zip(pts, (5, 6.5, 7.5, 7.5, 6), (FUR_D, FUR, FUR_D, FUR, FUR_L)):
        cv.ellipse(ring(q, r), fill=col)


def body_deco(cv, name, hip, sh, tw):
    mid = ((hip[0] + sh[0]) / 2, (hip[1] + sh[1]) / 2)
    if name == "ken":
        cv.line(
            [(hip[0] - tw / 2, hip[1] - 1), (hip[0] + tw / 2, hip[1] - 1)],
            fill=(20, 20, 20),
            width=3,
        )
        cv.line([sh, (sh[0] + 2, sh[1] + 6)], fill=(245, 245, 245), width=2)
    elif name == "tank":
        cv.line([sh, hip], fill=(150, 160, 180), width=max(2, tw - 8))
        cv.ellipse(ring(sh, 6), fill=(150, 160, 180))
        for q in ((mid[0] - 3, mid[1]), (mid[0] + 3, mid[1])):
            cv.ellipse(ring(q, 1), fill=(230, 200, 60))
    elif name == "ninja":
        cv.line(
            [(hip[0] - tw / 2, hip[1] - 1), (hip[0] + tw / 2, hip[1] - 1)],
            fill=(200, 30, 40),
            width=2,
        )
    elif name == "monkey":
        cv.ellipse(
            [mid[0] - tw / 3, mid[1] - 6, mid[0] + tw / 3 + 2, mid[1] + 6],
            fill=(205, 160, 110),
        )
    elif name == "alien":
        cv.line(
            [(hip[0] - tw / 2, hip[1] - 1), (hip[0] + tw / 2, hip[1] - 1)],
            fill=(60, 65, 85),
            width=2,
        )
        cv.ellipse(ring(mid, 2), fill=(120, 240, 255))
    elif name == "squirrel":
        cv.ellipse(
            [mid[0] - 1, mid[1] - 5, mid[0] + tw / 2 + 1, mid[1] + 5],
            fill=(250, 230, 200),
        )


def head(cv, name, h, r, skin):
    hx, hy = h
    if name == "monkey":
        cv.ellipse(ring(h, r), fill=(110, 70, 40))
        cv.ellipse(ring((hx - r + 1, hy - r + 2), 3), fill=(110, 70, 40))
        cv.ellipse(ring((hx - r + 1, hy - r + 2), 1), fill=(205, 160, 110))
        cv.ellipse([hx - 1, hy - 2, hx + r, hy + r - 1], fill=(230, 190, 140))
        cv.rectangle([hx + 2, hy - 2, hx + 4, hy - 1], fill=(60, 255, 100))
        cv.line([(hx + 2, hy + 3), (hx + r - 2, hy + 3)], fill=(60, 30, 20), width=1)
        cv.line([(hx + 3, hy + 4), (hx + 3, hy + 5)], fill=(255, 255, 255), width=1)
        return
    if name == "alien":  # big head, huge slanted black eyes, one antenna
        cv.ellipse([hx - r, hy - r, hx + r, hy + r + 2], fill=skin)
        cv.polygon(
            [
                (hx + 1, hy - 1),
                (hx + r, hy - 3),
                (hx + r - 1, hy + 3),
                (hx + 3, hy + 5),
            ],
            fill=(15, 15, 25),
        )
        cv.polygon(
            [
                (hx - r + 2, hy - 2),
                (hx - 3, hy - 1),
                (hx - 3, hy + 4),
                (hx - r + 3, hy + 3),
            ],
            fill=(15, 15, 25),
        )
        cv.rectangle([hx + 4, hy - 1, hx + 5, hy], fill=(255, 255, 255))
        cv.line([(hx - 2, hy - r), (hx - 5, hy - r - 6)], fill=skin, width=1)
        cv.ellipse(ring((hx - 5, hy - r - 6), 2), fill=(255, 80, 80))
        return
    if name == "squirrel":
        for ex in (hx - r * 0.55, hx + r * 0.1):
            cv.polygon(
                [(ex - 3, hy - r + 2), (ex + 1, hy - r - 6), (ex + 4, hy - r + 1)],
                fill=FUR_D,
            )
        cv.ellipse(ring(h, r), fill=FUR)
        cv.ellipse([hx + 1, hy - 1, hx + r + 1, hy + r - 1], fill=(250, 230, 200))
        cv.ellipse(ring((hx + 3.5, hy - 2.5), 2.6), fill=(255, 255, 255))
        cv.ellipse(ring((hx + 4.2, hy - 2.4), 1.4), fill=(20, 15, 15))
        cv.ellipse(ring((hx + r + 0.5, hy + 1), 1.3), fill=(60, 35, 30))
        cv.rectangle([hx + r - 3, hy + 3, hx + r - 1, hy + 6], fill=(255, 255, 255))
        return
    cv.ellipse(ring(h, r), fill=skin)
    if name == "ken":
        cv.pieslice(ring(h, r), 180, 360, fill=(30, 25, 25))
        cv.line([(hx - r, hy - 1), (hx + r, hy - 1)], fill=(245, 245, 245), width=2)
        cv.line([(hx - r, hy - 1), (hx - r - 6, hy + 3)], fill=(245, 245, 245), width=2)
        cv.rectangle([hx + 2, hy + 1, hx + 3, hy + 2], fill=(20, 20, 20))
    elif name == "tank":
        cv.pieslice(ring(h, r), 170, 370, fill=(150, 155, 165))
        cv.rectangle([hx - r, hy - 1, hx + r, hy + 1], fill=(30, 30, 40))
        cv.rectangle([hx + 2, hy - 1, hx + 4, hy], fill=(255, 220, 60))
        cv.arc(ring(h, r), 20, 160, fill=(150, 155, 165), width=2)
    elif name == "ninja":
        cv.ellipse(ring(h, r), fill=(30, 35, 60))
        cv.rectangle([hx - 1, hy - 2, hx + r - 1, hy + 1], fill=skin)
        cv.rectangle([hx + 2, hy - 1, hx + 3, hy], fill=(20, 20, 30))
        cv.line([(hx - r, hy - 3), (hx + r, hy - 3)], fill=(200, 30, 40), width=2)
        cv.line([(hx - r, hy - 3), (hx - r - 8, hy - 6)], fill=(200, 30, 40), width=2)
        cv.line([(hx - r, hy - 3), (hx - r - 7, hy + 1)], fill=(200, 30, 40), width=2)


def fx(cv, kind, fh, af, hip, cx):
    for k in (kind or "").split("+"):
        if k == "blade":
            cv.line([fh, seg(fh, af[1], 12)], fill=(215, 220, 230), width=2)
            cv.line(
                [seg(fh, af[1] + 180, 2), seg(fh, af[1], 2)], fill=(80, 60, 40), width=3
            )
        elif k == "arc":
            cv.arc(
                [cx - 6, hip[1] - 34, cx + 34, hip[1] + 10],
                -70,
                30,
                fill=(255, 255, 255),
                width=2,
            )
        elif k == "glow":
            cv.ellipse(ring(fh, 6), fill=(255, 160, 40))
            cv.ellipse(ring(fh, 3), fill=(255, 240, 120))
        elif k == "spark":
            for a in range(0, 360, 45):
                cv.line([fh, seg(fh, a, 5)], fill=(255, 255, 255), width=1)
        elif k == "star":
            star(cv, fh, 4)
        elif k == "impact":
            g = (fh[0] + 4, 61)
            for a in (120, 150, 180, 210, 240):
                cv.line([g, seg(g, a, 9)], fill=(255, 220, 90), width=2)
        elif k == "smoke":
            for x, y, rr in [
                (cx - 12, 52, 6),
                (cx + 10, 46, 7),
                (cx - 2, 30, 7),
                (cx + 12, 58, 5),
                (cx - 14, 36, 5),
                (cx + 2, 48, 8),
            ]:
                cv.ellipse(ring((x, y), rr), fill=(175, 175, 190))
        elif k == "gun":  # ray gun held in the front hand
            gx, gy = fh
            cv.rectangle([gx, gy - 2, gx + 9, gy + 2], fill=(70, 75, 90))
            cv.rectangle([gx + 9, gy - 1, gx + 11, gy + 1], fill=(255, 60, 60))
            cv.rectangle([gx + 1, gy + 2, gx + 4, gy + 5], fill=(50, 55, 70))
        elif k == "flash":  # muzzle flash
            mx, my = fh[0] + 13, fh[1]
            for a in range(0, 360, 45):
                cv.line([(mx, my), seg((mx, my), a, 5)], fill=(255, 230, 120), width=1)
            cv.ellipse(ring((mx, my), 2), fill=(255, 255, 255))
        elif k == "beacon":  # signal light above the raised hand
            bx, by = fh[0], fh[1] - 7
            cv.ellipse(ring((bx, by), 4), outline=(120, 255, 255), width=1)
            cv.ellipse(ring((bx, by), 2), fill=(60, 200, 230))
        elif k == "beam":
            cv.line([(fh[0], fh[1] - 12), (fh[0], 1)], fill=(140, 255, 255), width=2)
        elif k == "claws":
            for i in (-1, 0, 1):
                cv.line(
                    [(fh[0] + 3, fh[1] - 6 + i * 4), (fh[0] + 11, fh[1] + 2 + i * 4)],
                    fill=(255, 255, 255),
                    width=1.2,
                )
        elif k == "nut":
            acorn(cv, (fh[0] + 3, fh[1] - 1))
        elif k == "nut_fly":
            acorn(cv, (fh[0] + 11, fh[1] - 1))
            for dy in (-3, 0, 3):
                cv.line(
                    [(fh[0] + 1, fh[1] + dy), (fh[0] + 6, fh[1] + dy)],
                    fill=(255, 255, 255),
                    width=0.8,
                )
        elif k == "nuts":
            for dx_, dy_ in ((10, -8), (13, 0), (10, 8)):
                acorn(cv, (fh[0] + dx_, fh[1] + dy_), 0.9)


def draw_fig(cv, name, p):
    c = CHARS[name]
    cx = CX + p["dx"]
    lean = p["lean"] + (12 if name == "monkey" else 0)
    sq = p["sq"]
    hip = (cx, 62 - c["leg"] + p["crouch"] + sq * c["leg"])
    sh = seg(hip, 180 - lean, c["torso"])
    hd = seg(sh, 180 - lean * 1.4, c["head_r"] + 2)
    af, ab, lf, lb = p["af"], p["ab"], p["lf"], p["lb"]
    if sq > 0.001:  # squat: bend the knees so the feet stay on the ground

        def squat(
            b,
        ):  # thigh forward by th+b, shin back by th-b: foot height = leg*(1-sq)
            th = math.degrees(math.acos(min(1.0, (1 - sq) / math.cos(math.radians(b)))))
            return (th + b, -th + b)

        lb = squat(-14)  # back foot planted behind
        if not p["kick"]:
            lf = squat(12)  # front foot planted ahead (a kick keeps its own front leg)
    aw, lw, tw = c["arm_w"], c["leg_w"], c["torso_w"]
    skin = c["skin"]

    if name == "squirrel":
        squirrel_tail(cv, c, hip, hd, p)
    hb = limb(cv, (sh[0] - 2, sh[1] + 1), ab, c["arm"], aw, shade_c(c["arm_col"], 0.75))
    cv.ellipse(ring(hb, aw / 2 + 1), fill=shade_c(skin, 0.75))
    fb = limb(cv, (hip[0] - 1, hip[1]), lb, c["leg"], lw, shade_c(c["pants"], 0.75))
    foot(cv, fb, shade_c(c["shoe"], 0.75))

    cv.line([hip, sh], fill=c["top"], width=tw)
    for q in (hip, sh):
        cv.ellipse(ring(q, tw / 2), fill=c["top"])
    body_deco(cv, name, hip, sh, tw)

    ff = limb(cv, (hip[0] + 1, hip[1]), lf, c["leg"], lw, c["pants"])
    foot(cv, ff, c["shoe"])
    head(cv, name, hd, c["head_r"], skin)
    fh = limb(cv, (sh[0] + 1, sh[1] + 1), af, c["arm"], aw, c["arm_col"])
    cv.ellipse(ring(fh, aw / 2 + 1), fill=skin)
    fx(cv, p["fx"], fh, af, hip, cx)


def render(name, pose):
    cv = Cv(64, 64, K)
    draw_fig(cv, name, pose)
    return finish(cv.img).resize((CELL, CELL), Image.LANCZOS)


def sheet(name):
    c = CHARS[name]
    img = Image.new("RGBA", (CELL * COLS, CELL * ROWS), (0, 0, 0, 0))
    rows = [
        idle_frames(),
        walk_frames(),
        JUMP,
        BLOCK,
        HURT,
        fly_frames() if c.get("fly") else [],
        CROUCH_ROW,
    ]
    rows += [move_frames(k) for k in c["anims"]]  # rows 7..17
    rows += [CROUCH_BLOCK_ROW]  # row 18
    rows += [move_frames(k) for k in c.get("canims", CANIMS)]  # rows 19..24
    rows += [move_frames(k) for k in c.get("xanims", XANIMS)]  # rows 25..28
    for r, frames in enumerate(rows):
        for col, pose in enumerate(frames):
            img.paste(render(name, pose), (col * CELL, r * CELL))
    return img


# ------------------------------------------------------------------ shots, ship, fx


def shots():
    img = Image.new("RGBA", (48 * len(CHARS), 48), (0, 0, 0, 0))
    for i, name in enumerate(CHARS):
        cv = Cv(16, 16, K)
        c = (8, 8)
        if name == "ken":
            cv.polygon([(1, 8), (8, 3), (8, 13)], fill=(230, 90, 30))
            cv.ellipse(ring((9, 8), 6), fill=(255, 150, 40))
            cv.ellipse(ring((9, 8), 3), fill=(255, 245, 140))
        elif name == "tank":
            cv.polygon(
                [(2, 6), (6, 2), (12, 3), (14, 9), (10, 14), (4, 13)],
                fill=(105, 105, 115),
            )
            cv.polygon([(5, 5), (8, 4), (9, 7), (6, 8)], fill=(160, 160, 170))
        elif name == "ninja":
            cv.polygon(
                [seg(c, 45 * k, 7 if k % 2 == 0 else 2.5) for k in range(8)],
                fill=(195, 200, 210),
            )
            cv.ellipse(ring(c, 1), fill=(40, 40, 50))
        elif name == "alien":  # red laser bolt, bright at the tip
            cv.line([(0.5, 8), (15, 8)], fill=(255, 40, 40), width=4)
            cv.line([(1, 8), (14, 8)], fill=(255, 200, 200), width=2)
            cv.rectangle([13, 6, 15, 10], fill=(255, 255, 255))
        elif name == "squirrel":
            acorn(cv, (8, 8), 1.6)
        else:
            cv.ellipse(ring(c, 6), fill=(70, 30, 110))
            cv.ellipse(ring(c, 3), fill=(60, 255, 100))
        img.paste(
            finish(cv.img, outline=1.4).resize((48, 48), Image.LANCZOS), (i * 48, 0)
        )
    return img


def ship_sheet():  # two frames, lights alternate
    img = Image.new("RGBA", (384, 96), (0, 0, 0, 0))
    for f in range(2):
        cv = Cv(64, 32, K)
        cv.pieslice([20, 2, 44, 26], 180, 360, fill=(140, 230, 255))
        cv.arc([22, 4, 42, 24], 200, 250, fill=(235, 252, 255), width=1.2)
        cv.ellipse(ring((32, 9), 4), fill=(120, 210, 120))
        cv.polygon([(33, 7), (37, 8), (37, 10), (33, 10)], fill=(15, 15, 25))
        cv.ellipse([3, 12, 61, 28], fill=(150, 155, 172))
        cv.ellipse([10, 21, 54, 31], fill=(95, 100, 118))
        for i in range(6):
            x = 10 + i * 9
            cv.rectangle(
                [x, 18, x + 3, 20],
                fill=(255, 230, 60) if (i + f) % 2 else (255, 80, 60),
            )
        for x in (14, 24, 40, 50):
            cv.ellipse(ring((x, 15), 0.8), fill=(210, 214, 226))
        cv.rectangle([28, 28, 36, 31], fill=(60, 65, 80))
        img.paste(
            finish(cv.img, outline=1.4).resize((192, 96), Image.LANCZOS), (f * 192, 0)
        )
    return img


def fx_sheet():  # bomb + 3 explosion frames
    img = Image.new("RGBA", (384, 96), (0, 0, 0, 0))
    rnd = random.Random(3)
    cells = []
    cv = Cv(32, 32, K)
    cv.polygon([(8, 4), (13, 8), (8, 10)], fill=(200, 60, 60))
    cv.polygon([(24, 4), (19, 8), (24, 10)], fill=(200, 60, 60))
    cv.ellipse([10, 6, 22, 28], fill=(45, 48, 60))
    cv.line([(13, 9), (13, 20)], fill=(110, 115, 130), width=1)
    cv.ellipse(ring((16, 4), 1.5), fill=(255, 220, 80))
    cells.append(cv)
    cv = Cv(32, 32, K)
    cv.ellipse([8, 10, 24, 26], fill=(255, 140, 30))
    cv.ellipse([12, 14, 20, 22], fill=(255, 240, 120))
    cells.append(cv)
    cv = Cv(32, 32, K)
    cv.ellipse([1, 1, 31, 31], fill=(240, 90, 30))
    cv.ellipse([5, 5, 27, 27], fill=(255, 160, 40))
    cv.ellipse([11, 11, 21, 21], fill=(255, 245, 140))
    for a in range(0, 360, 30):  # flame spikes
        cv.polygon(
            [seg((16, 16), a - 6, 12), seg((16, 16), a, 16), seg((16, 16), a + 6, 12)],
            fill=(240, 90, 30),
        )
    cells.append(cv)
    cv = Cv(32, 32, K)
    cv.ellipse([2, 4, 30, 30], fill=(110, 110, 120))
    cv.ellipse([8, 10, 24, 26], fill=(200, 100, 50))
    for _ in range(10):
        x, y = rnd.randint(5, 26), rnd.randint(7, 27)
        cv.rectangle([x, y, x + 1, y + 1], fill=(255, 180, 60))
    cells.append(cv)
    for i, cv in enumerate(cells):
        img.paste(
            finish(cv.img, outline=1.0, shade=(i == 0)).resize((96, 96), Image.LANCZOS),
            (i * 96, 0),
        )
    return img


# ------------------------------------------------------------------ the stage


def background(W=1280, H=580, ss=2):
    """Stadium at dusk: a huge waving US flag hung behind a cheering crowd."""
    w, h = W * ss, H * ss
    u, v = w / 1120.0, h / 500.0
    rnd = random.Random(7)
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):  # dusk sky
        t = y / h
        d.line(
            [(0, y), (w, y)],
            fill=(int(20 + 80 * t), int(24 + 50 * t), int(70 + 40 * t)),
        )
    for _ in range(90):  # a few stars high up
        sx, sy = rnd.randint(0, w), rnd.randint(0, int(h * 0.25))
        d.ellipse([sx, sy, sx + 2 * ss, sy + 2 * ss], fill=(220, 225, 255))
    glow = Image.new("RGB", (w, h), (0, 0, 0))  # warm stadium glow behind the flag
    ImageDraw.Draw(glow).ellipse(
        [w * 0.16, -h * 0.05, w * 0.84, h * 0.85], fill=(80, 58, 96)
    )
    img = ImageChops.add(img, glow.filter(ImageFilter.GaussianBlur(70 * u)))
    d = ImageDraw.Draw(img)
    for lx in (0.08, 0.92):  # floodlight towers
        x0 = int(w * lx)
        d.rectangle([x0 - 5 * u, 80 * v, x0 + 5 * u, 330 * v], fill=(40, 42, 56))
        d.rectangle([x0 - 34 * u, 56 * v, x0 + 34 * u, 82 * v], fill=(60, 62, 78))
        for i in range(5):
            d.ellipse(
                ring((x0 - 28 * u + i * 14 * u, 69 * v), 5 * u), fill=(255, 246, 200)
            )

    red, white, blue = (178, 34, 52), (245, 245, 245), (60, 59, 110)
    fx0, fy0, fw = int(w // 2 - 340 * u), int(34 * v), int(680 * u)
    fh = fw / 1.9  # flag 1.9 : 1, as in the real thing
    sh = fh / 13.0
    cw, ch = int(fw * 0.4), int(sh * 7)  # blue canton: 40% wide, 7 stripes tall

    def wave(x):
        return int((7 * math.sin(x / (55 * u)) + 3 * math.sin(x / (17 * u))) * u)

    d.rectangle(
        [fx0 - 12 * u, fy0 - 16 * u, fx0 + fw + 12 * u, fy0 - 8 * u], fill=(70, 70, 82)
    )
    for x in range(fw):  # column by column, so it ripples and folds
        wv = wave(x)
        f = 0.86 + 0.14 * math.sin(x / (55 * u) + 1.2)
        for s in range(13):
            d.line(
                [
                    (fx0 + x, fy0 + int(s * sh) + wv),
                    (fx0 + x, fy0 + int((s + 1) * sh) + wv),
                ],
                fill=shade_c(red if s % 2 == 0 else white, f),
            )
        if x < cw:
            d.line(
                [(fx0 + x, fy0 + wv), (fx0 + x, fy0 + ch + wv)], fill=shade_c(blue, f)
            )
    for r in range(9):  # 50 stars: rows of 6 and 5
        n, off = (6, 1) if r % 2 == 0 else (5, 2)
        for c in range(n):
            sx = int(cw * (off + 2 * c) / 12.0)
            sy = int(ch * (1 + 2 * r) / 18.0)
            pts = [
                seg(
                    (fx0 + sx, fy0 + sy + wave(sx)),
                    180 + 36 * i,
                    (6 if i % 2 == 0 else 2.4) * u,
                )
                for i in range(10)
            ]
            d.polygon(pts, fill=white)

    skins = [
        (240, 200, 160),
        (210, 160, 120),
        (150, 105, 75),
        (100, 70, 50),
        (250, 215, 185),
    ]
    shirts = [
        (200, 50, 50),
        (60, 90, 190),
        (240, 240, 240),
        (230, 190, 50),
        (60, 150, 90),
        (150, 70, 160),
        (240, 130, 40),
        (50, 50, 60),
    ]
    hairs = [(40, 30, 25), (90, 60, 35), (200, 170, 90), (30, 30, 30), (150, 60, 40)]
    for r in range(4):  # crowd rows, back to front, darker at the back
        base = (292 + r * 52) * v
        dim = 0.55 + 0.15 * r
        for x in range(-20, int(w / u) + 40, 36):
            px = (x + (18 if r % 2 else 0) + rnd.randint(-4, 4)) * u
            py = base + rnd.randint(-5, 5) * v
            skin = shade_c(rnd.choice(skins), dim)
            shirt = shade_c(rnd.choice(shirts), dim)
            if rnd.random() < 0.3:  # arm up, sometimes waving a small flag
                s_ = rnd.choice((-1, 1))
                ax, hx_ = px + 12 * u * s_, px + 15 * u * s_
                d.line(
                    [(ax, py + 20 * v), (hx_, py - 14 * v)], fill=skin, width=int(4 * u)
                )
                d.ellipse(ring((hx_, py - 16 * v), 3 * u), fill=skin)
                if rnd.random() < 0.35:
                    d.line(
                        [(hx_, py - 16 * v), (hx_, py - 34 * v)],
                        fill=shade_c((220, 220, 220), dim),
                        width=max(1, int(u)),
                    )
                    d.rectangle(
                        [hx_, py - 34 * v, hx_ + 11 * u, py - 27 * v],
                        fill=shade_c(rnd.choice((red, blue, white)), dim),
                    )
            d.rounded_rectangle(
                [px - 14 * u, py + 10 * v, px + 14 * u, py + 74 * v],
                radius=int(9 * u),
                fill=shirt,
            )
            d.ellipse(ring((px, py), 9 * u), fill=skin)
            d.pieslice(
                ring((px, py), 9 * u), 180, 360, fill=shade_c(rnd.choice(hairs), dim)
            )
            if r >= 2:  # faces on the front rows
                for ex in (-3, 3):
                    d.ellipse(
                        ring((px + ex * u, py + 1 * v), 1.1 * u),
                        fill=shade_c((25, 20, 20), dim),
                    )
                if rnd.random() < 0.5:
                    d.ellipse(
                        [px - 3 * u, py + 3.5 * v, px + 3 * u, py + 7 * v],
                        fill=shade_c((120, 40, 40), dim),
                    )
    vig = ImageOps.invert(Image.radial_gradient("L").resize((w, h))).point(
        lambda p_: 150 + p_ * 105 // 255
    )
    img = ImageChops.multiply(img, Image.merge("RGB", (vig, vig, vig)))
    return img.resize((W, H), Image.LANCZOS)


# ------------------------------------------------------------------ main


def main(args):
    os.makedirs(OUT, exist_ok=True)
    for t in args or list(CHARS) + ["misc", "bg"]:
        if t in CHARS:
            save_argb(sheet(t), os.path.join(OUT, t + ".bmp"))
        elif t == "misc":
            save_argb(shots(), os.path.join(OUT, "shots.bmp"))
            save_argb(ship_sheet(), os.path.join(OUT, "ship.bmp"))
            save_argb(fx_sheet(), os.path.join(OUT, "fx.bmp"))
        elif t == "bg":
            background().save(os.path.join(OUT, "background.bmp"))
        print("done", t, flush=True)


if __name__ == "__main__":
    main(sys.argv[1:])
