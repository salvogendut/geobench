"""Run against the assembled kernel: python3 tools/test_wm_damage.py kernel.raw kernel.sym

Requires GNU objcopy and SDCC's ucsim_z80. No ROMs or disk image needed.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def run_suite(raw, symbols):
    labels = dict((name, int(addr, 16)) for name, addr in re.findall(
        r"^(\w+) #([0-9A-Fa-f]+) ", Path(symbols).read_text(), re.M))
    # Kernel low-RAM ABI, byte-column x and scanline y.
    count, focus, table, zorder, clip = 0x1350, 0x1351, 0x1352, 0x141A, 0x1338
    rect = table + 25 + 1  # slot 1, not its z-index (2)
    flags = table + 25 + 13
    with tempfile.TemporaryDirectory() as tmp:
        image = Path(tmp) / "kernel.hex"
        subprocess.run(["objcopy", "-I", "binary", "-O", "ihex", "--change-addresses",
                        "0x8000", raw, image], check=True)

        def check(case, setup, start, stop, expected):
            commands = [f"set mem rom {addr:#x} " + " ".join(map(str, data))
                        for addr, data in setup]
            commands += [f"run {start:#x} {stop:#x}", "quit"]
            result = subprocess.run(["ucsim_z80", "-c", "-", image], check=True,
                                    input="\n".join(commands) + "\n", text=True,
                                    capture_output=True, timeout=5)
            actual = re.search(r"Flags=.*?\bA= 0x([0-9a-fA-F]+)", result.stdout)
            if not actual or int(actual[1], 16) != expected:
                raise AssertionError(f"{case}: expected A={expected}\n{result.stdout}")

        base = [(count, [3]), (focus, [2]), (zorder, [0, 2, 1]),
                (rect, [10, 20, 40, 80]), (flags, [3])]
        for name, damage, override, expected in [
            ("exact", [10, 20, 40, 80], [], 2),
            ("last pixel", [49, 99, 1, 1], [], 2),
            ("left", [9, 20, 1, 1], [], 0),
            ("top", [10, 19, 1, 1], [], 0),
            ("right", [10, 20, 41, 80], [], 0),
            ("bottom", [10, 20, 40, 81], [], 0),
            ("overflow", [48, 20, 240, 1], [], 0),
            ("desktop", [0, 0, 128, 212], [], 0),
            ("legacy", [10, 20, 40, 80], [(flags, [1])], 0),
            ("dead", [10, 20, 40, 80], [(flags, [2])], 0),
            ("empty", [10, 20, 40, 80], [(count, [0])], 0),
        ]:
            check(name, base + override + [(clip, damage)], labels["WM_REPAINT_ALL"],
                  labels["WRA_SEED"], expected)

        def check_call(routine, setup, args, outputs):
            # Call the service with the ABI registers set; OR any byte mismatches into A.
            dest = labels[routine]
            code = args + [0xcd, dest & 255, dest >> 8, 0x06, 0]
            for addr, data in outputs:
                for i, byte in enumerate(data):
                    at = addr + i
                    code += [0x3a, at & 255, at >> 8, 0xee, byte, 0xb0, 0x47]
            check(routine, setup + [(0x6000, code)], 0x6000, 0x6000 + len(code), 0)

        for routine, args, geometry, damage in [
            ("K_WM_SETPOS", (6, 31), [6, 31, 40, 80], [6, 20, 44, 91]),
            ("K_WM_SETSIZE", (50, 60), [10, 20, 50, 60], [10, 20, 50, 80]),
        ]:
            check_call(routine, base + [(focus, [1])], [0x3e, args[0], 0x2e, args[1]],
                       [(rect, geometry), (clip, damage)])

        geometry = [10, 20, 40, 80]
        descriptor = geometry + [4, 14, 0x21, 0x43, 0x65, 0x47, 0, 0]
        check_call("K_WM_MANAGED", [(count, [1]), (table + 13, [1]), (flags, [0]),
                                    (0x5100, descriptor)], [0x21, 0, 0x51],
                   [(rect, geometry + [0, 0x51, 0x21, 0x43, 0x21, 0x43]),
                    (count, [2, 1]), (zorder + 1, [1]), (flags, [3]),
                    (0x1448, geometry), (clip, geometry)])
    print("Window damage and geometry tests passed.")


if __name__ == "__main__":
    run_suite(*sys.argv[1:])
