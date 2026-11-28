"""Describe compatible raw Z80 guests without modifying their GBAP artwork."""
from binascii import crc_hqx
from pathlib import Path
from struct import pack
import sys


def catalog(guests):
    records = bytearray()
    seen = set()
    for kind, data in guests:
        if not 0 <= kind <= 5 or not 0 < len(data) <= 0x3f00:
            raise ValueError("invalid Agon window class or guest size")
        key = (len(data), crc_hqx(data, 0xffff))
        if key in seen:
            raise ValueError("ambiguous guest fingerprint")
        seen.add(key)
        records.extend(pack("<BHH", kind, *key))
    if not 1 <= len(seen) <= 32:
        raise ValueError("catalogue must describe 1..32 guests")
    return b"AGAC\x01" + bytes([len(seen)]) + records


if __name__ == "__main__":
    out, *args = sys.argv[1:]
    Path(out).write_bytes(catalog((int(kind), Path(path).read_bytes())
                                  for kind, path in (arg.split(":", 1) for arg in args)))
