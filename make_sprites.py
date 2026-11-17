#!/usr/bin/env python3
"""Build every client sprite asset under ./sprites (needs Pillow).

    python3 make_sprites.py                 # all fighters + misc + bg
    python3 make_sprites.py ken slurpy      # selected fighters only
    python3 make_sprites.py misc bg         # fx and stage backgrounds

Each fighter has tools/build_<name>_sprites.py. Painted characters (jv, rump,
kammy, slurpy, flusk, stephen, lopicardo) pack frames from tools/refs/*_style_a_frames/; others are
procedural via tools/procedural_sprites.py. Projectile icons live on each fighter sheet.
"""
from __future__ import annotations

import importlib
import sys

from tools.sprite_common import CHAR_ORDER, PAINTED
from tools import procedural_sprites as proc

# Roster order: module name tools.build_<name>_sprites
BUILDERS = {name: f"tools.build_{name}_sprites" for name in CHAR_ORDER}


def _run_builder(name: str) -> None:
    mod = importlib.import_module(BUILDERS[name])
    mod.build()


def main(args: list[str]) -> None:
    targets = args or list(CHAR_ORDER) + ["misc", "bg"]
    unknown = [t for t in targets if t not in BUILDERS and t not in ("misc", "bg")]
    if unknown:
        raise SystemExit(f"unknown target(s): {', '.join(unknown)}")

    for t in targets:
        if t == "misc":
            proc.build_misc()
        elif t == "bg":
            proc.build_bg()
        else:
            _run_builder(t)
        print("done", t, flush=True)


if __name__ == "__main__":
    argv = sys.argv[1:]
    if not argv:
        painted = [n for n in CHAR_ORDER if n in PAINTED]
        procedural = [n for n in CHAR_ORDER if n not in PAINTED]
        argv = painted + procedural + ["misc", "bg"]
    main(argv)
