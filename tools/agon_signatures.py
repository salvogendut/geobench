"""Emit RASM constants for the Agon loader's known SD resources."""

from binascii import crc_hqx
from pathlib import Path
import sys


if __name__ == "__main__":
    for name, path in zip(("CALC", "FM", "IST", "PREVIEW"), sys.argv[1:], strict=True):
        data = Path(path).read_bytes()
        print(f"{name}_SIZE equ {len(data)}")
        print(f"{name}_CRC equ {crc_hqx(data, 0xffff)}")
