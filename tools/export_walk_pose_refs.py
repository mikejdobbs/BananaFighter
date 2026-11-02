#!/usr/bin/env python3
"""Export 8 procedural walk silhouettes for painted walk frame generation."""
from __future__ import annotations

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

import make_sprites as ms  # noqa: E402

OUT = os.path.join(ROOT, "tools", "refs", "walk_pose_refs")


def main():
    os.makedirs(OUT, exist_ok=True)
    for i, pose in enumerate(ms.walk_frames(8)):
        ms.render("kammy", pose).save(os.path.join(OUT, f"walk_{i}.png"))
    print("wrote", OUT, flush=True)


if __name__ == "__main__":
    main()
