# Rebuild the machine chord table from the human CSV before compile.
Import("env")

import sys
from pathlib import Path

tools_dir = Path(__file__).resolve().parent
if str(tools_dir) not in sys.path:
    sys.path.insert(0, str(tools_dir))

import build_chord_map_machine

status = build_chord_map_machine.main()
if status:
    env.Exit(status)
