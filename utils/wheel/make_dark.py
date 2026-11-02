#!/usr/bin/env python3
"""Generate the dark variant of the Wheel theme from the light one."""
import re, sys, os
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "wps")
MAP = {
    "f7f7f9": "1c1c1f",  # top bar
    "c8c8ce": "38383c",  # hairlines
    "141416": "f2f2f5",  # main text
    "141415": "f2f2f5",  # top bar text
    "3a3a3e": "e5e5ea",  # top bar icons
    "fffffe": "8e8e93",  # note glyph on the placeholder tile
    "ffffff": "0f0f11",  # list background
    "f2f2f5": "18181b",  # side pane
    "d1d1d6": "2c2c2f",
    "6e6e73": "a1a1a8",  # secondary text
    "3a3a3f": "e5e5ea",  # icons
    "cfcfd6": "2c2c31",  # art placeholder
    "d6d6dc": "3a3a3f",  # bar tracks
    "dcdce1": "3a3a3f",
    "e6e6ea": "050506",  # art shadow
    "d4d4da": "1f1f22",
}
def convert(src, dst):
    s = open(os.path.join(ROOT, src), encoding="utf-8").read()
    s = s.replace("# Wheel -", "# Wheel Dark -", 1)
    s = re.sub(r"\b(" + "|".join(MAP) + r")\b", lambda m: MAP[m.group(1)], s)
    open(os.path.join(ROOT, dst), "w", encoding="utf-8").write(s)
convert("Wheel.sbs", "Wheel-Dark.sbs")
convert("Wheel.wps", "Wheel-Dark.wps")
print("ok")
