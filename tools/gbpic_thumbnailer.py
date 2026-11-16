#!/usr/bin/env python3
"""Render a GEOBENCH GBPC picture as a PNG thumbnail."""

import os
import sys
from pathlib import Path
from urllib.parse import unquote, urlparse

from PIL import Image


MID = 146
CPC_RGB = (
    (0, 0, 0), (0, 0, MID), (0, 0, 255),
    (MID, 0, 0), (MID, 0, MID), (MID, 0, 255),
    (255, 0, 0), (255, 0, MID), (255, 0, 255),
    (0, MID, 0), (0, MID, MID), (0, MID, 255),
    (MID, MID, 0), (MID, MID, MID), (MID, MID, 255),
    (255, MID, 0), (255, MID, MID), (255, MID, 255),
    (0, 255, 0), (0, 255, MID), (0, 255, 255),
    (MID, 255, 0), (MID, 255, MID), (MID, 255, 255),
    (255, 255, 0), (255, 255, MID), (255, 255, 255),
)
SCREEN7_EXTRA_INKS = (18, 2, 24, 8, 20, 15, 16, 11, 21, 5, 13, 22)
MODE1_LOW_BITS = (7, 6, 5, 4)
MODE1_HIGH_BITS = (3, 2, 1, 0)


def local_path(value):
    """Accept the local paths and file URIs used by thumbnailer implementations."""
    value = os.fspath(value)
    parsed = urlparse(value)
    if parsed.scheme == "file":
        return unquote(parsed.path)
    return value


def load_pic(path):
    raw = Path(local_path(path)).read_bytes()
    if len(raw) < 14 or raw[:4] != b"GBPC" or raw[4] != 2:
        raise ValueError("not a GBPC v2 picture")

    mode = raw[5]
    width = int.from_bytes(raw[6:8], "little")
    height = int.from_bytes(raw[8:10], "little")
    if not width or not height or width > 4096 or height > 4096:
        raise ValueError("invalid picture dimensions")
    if width * height > 16 * 1024 * 1024:
        raise ValueError("picture is too large")

    inks = tuple(raw[10:14])
    if any(ink >= len(CPC_RGB) for ink in inks):
        raise ValueError("invalid CPC ink in picture palette")
    palette = tuple(CPC_RGB[ink] for ink in inks)
    payload = raw[14:]
    pixels = []

    if mode in (1, 6):
        row_bytes = (width + 3) // 4
        expected = row_bytes * height
        if len(payload) < expected:
            raise ValueError("truncated picture payload")
        for y in range(height):
            row = payload[y * row_bytes:(y + 1) * row_bytes]
            for packed in row:
                if mode == 1:
                    pens = tuple(
                        ((packed >> MODE1_LOW_BITS[i]) & 1)
                        | (((packed >> MODE1_HIGH_BITS[i]) & 1) << 1)
                        for i in range(4)
                    )
                else:
                    pens = tuple((packed >> (6 - 2 * i)) & 3 for i in range(4))
                pixels.extend(palette[pen] for pen in pens)
            del pixels[width * (y + 1):]
    elif mode == 7:
        palette += tuple(CPC_RGB[ink] for ink in SCREEN7_EXTRA_INKS)
        row_bytes = (width + 1) // 2
        expected = row_bytes * height
        if len(payload) < expected:
            raise ValueError("truncated picture payload")
        for y in range(height):
            row = payload[y * row_bytes:(y + 1) * row_bytes]
            for packed in row:
                pixels.append(palette[packed >> 4])
                pixels.append(palette[packed & 15])
            del pixels[width * (y + 1):]
    else:
        raise ValueError("unsupported GBPC packing mode")

    image = Image.new("RGB", (width, height))
    image.putdata(pixels)
    return image


def render(source, destination, requested_size):
    image = load_pic(source)
    size = max(1, min(int(requested_size), 4096))
    scale = min(size / image.width, size / image.height)
    target = (max(1, round(image.width * scale)),
              max(1, round(image.height * scale)))
    if target != image.size:
        resampling = getattr(Image, "Resampling", Image)
        method = resampling.NEAREST if scale > 1 else resampling.LANCZOS
        image = image.resize(target, method)
    image.save(local_path(destination), "PNG")


def main():
    if len(sys.argv) not in (3, 4):
        print("usage: gbpic_thumbnailer.py INPUT OUTPUT [SIZE]", file=sys.stderr)
        return 2
    try:
        render(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) == 4 else 256)
    except (OSError, ValueError) as error:
        print(f"gbpic-thumbnailer: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
