"""KevKev's sheet is composed from the painted poses in tools/refs/kevkev.

The old procedural body is gone. Run tools/compose_kevkev_sheet.py.
"""

import runpy
import os

runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), "compose_kevkev_sheet.py"), run_name="__main__")
