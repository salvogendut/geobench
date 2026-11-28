"""Emit RASM constants for the Agon loader's known SD resources."""

from binascii import crc_hqx
from pathlib import Path
import sys


if __name__ == "__main__":
    path, = sys.argv[1:]
    data = Path(path).read_bytes()
    print(f"IST_SIZE equ {len(data)}")
    print(f"IST_CRC equ {crc_hqx(data, 0xffff)}")
