"""Extract the two resident WM sections, checking the cfg and stack boundaries."""
from pathlib import Path
import re
import sys

build = Path(sys.argv[1])
layout = (build / "window.map").read_text()
flat = (build / "window.flat").read_bytes()
for area, base, end, name in (("CODE", 0x900, 0x1000, "window"),
                               ("GESTURE", 0xf240, 0xf700, "gesture")):
    match = re.search(rf"^_{area}\s+(\w+)\s+(\w+)", layout, re.M)
    if not match:
        raise SystemExit(f"Missing {area} in the Agon window link map")
    addr, size = (int(v, 16) for v in match.groups())
    if addr != base or not 0 < size <= end-base or addr+size > len(flat):
        raise SystemExit(f"{name} section at {addr:#06x}+{size:#06x} outside its reserved region")
    (build / f"{name}.bin").write_bytes(flat[addr:addr+size])
