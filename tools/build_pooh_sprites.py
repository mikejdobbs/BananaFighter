"""Pooh's sheet is composed from the painted poses in tools/refs/pooh.

Run tools/compose_pooh_sheet.py (this file just forwards).
"""

import os
import runpy

runpy.run_path(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "compose_pooh_sheet.py"),
    run_name="__main__",
)
