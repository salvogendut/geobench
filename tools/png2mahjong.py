#!/usr/bin/env python3
"""Generate the compact Kana Mahjong artwork and board tables.

The sources are a 6x7 Katakana atlas and a 6x6 Hiragana atlas. This tool
extracts each glyph, reduces it to a monochrome mask, and renders a canonical
GEOBENCH Mode-1 tile (16x18 pixels, four pixels per byte). It also emits the
traditional 144-position Turtle layout and a legal removal order used to make
every shuffled game solvable.

Usage:
    tools/png2mahjong.py [--check] assets/katakana.png assets/hiragana.png \
        apps/mahjong/kana.h
"""

import argparse
from pathlib import Path

from PIL import Image


ATLAS_SPECS = (("Katakana", 6, 7, 12), ("Hiragana", 6, 6, 6))
# Both source atlases use this row-major gojuon subset. Keep pronunciation
# metadata beside the atlas description so generated artwork and labels cannot
# silently drift apart. Labels are ASCII and limited to three characters for
# the compact in-app preview panel.
ROMAJI = (
    "A", "I", "U", "E", "O", "KA",
    "KI", "KU", "KE", "KO", "SA", "SHI",
    "SU", "SE", "SO", "TA", "CHI", "TSU",
    "TE", "TO", "NA", "NI", "NU", "NE",
    "NO", "HA", "HI", "FU", "HE", "HO",
    "MA", "MI", "MU", "ME", "MO", "YA",
    "YU", "YO", "RA", "RI", "RU", "WA",
)
TILE_W = 16
TILE_H = 18
TILE_BYTES = TILE_W // 4 * TILE_H
GLYPH_W = 12
GLYPH_H = 13


def turtle_layout():
    layout = []

    def add(row, col, level):
        layout.append((row, col, level))

    for col in range(2, 25, 2):
        add(0, col, 0)
    for col in range(6, 21, 2):
        add(2, col, 0)
    for col in range(4, 23, 2):
        add(4, col, 0)
    for col in range(2, 25, 2):
        add(6, col, 0)
    add(7, 0, 0)
    add(7, 26, 0)
    add(7, 28, 0)
    for col in range(2, 25, 2):
        add(8, col, 0)
    for col in range(4, 23, 2):
        add(10, col, 0)
    for col in range(6, 21, 2):
        add(12, col, 0)
    for col in range(2, 25, 2):
        add(14, col, 0)

    for row in range(2, 13, 2):
        for col in range(8, 19, 2):
            add(row, col, 1)
    for row in range(4, 11, 2):
        for col in range(10, 17, 2):
            add(row, col, 2)
    for row in range(6, 9, 2):
        for col in range(12, 15, 2):
            add(row, col, 3)
    add(7, 13, 4)

    if len(layout) != 144:
        raise ValueError(f"Turtle layout has {len(layout)} positions, expected 144")
    return layout


def intervals_overlap(a, b):
    return a < b + 2 and b < a + 2


def tile_is_open(index, layout, active):
    if not active[index]:
        return False
    row, col, level = layout[index]
    left = right = covered = False
    for other, (orow, ocol, olevel) in enumerate(layout):
        if other == index or not active[other]:
            continue
        row_overlap = intervals_overlap(row, orow)
        if olevel > level and row_overlap and intervals_overlap(col, ocol):
            covered = True
        if olevel == level and row_overlap:
            if ocol + 2 == col:
                left = True
            if col + 2 == ocol:
                right = True
    return not covered and (not left or not right)


def solvable_order(layout):
    """Produce a deterministic legal pair-removal sequence for the layout."""
    active = [True] * len(layout)
    result = []
    while len(result) < len(layout):
        available = [
            i for i in range(len(layout))
            if active[i] and tile_is_open(i, layout, active)
        ]
        if len(available) < 2:
            raise ValueError(f"Turtle removal order stalled after {len(result)} tiles")
        available.sort(key=lambda i: (-layout[i][2], layout[i][0], layout[i][1]))
        first = available[0]
        fr, fc, _ = layout[first]
        second = max(
            available[1:],
            key=lambda i: (
                layout[i][2] * 100
                + abs(layout[i][1] - fc) * 3
                + abs(layout[i][0] - fr)
            ),
        )
        active[first] = active[second] = False
        result.extend((first, second))

    if len(set(result)) != len(layout):
        raise ValueError("generated removal order contains duplicate positions")

    # Re-run the final sequence as an independent guard against generator changes.
    active = [True] * len(layout)
    for pair in range(0, len(result), 2):
        a, b = result[pair : pair + 2]
        if not tile_is_open(a, layout, active) or not tile_is_open(b, layout, active):
            raise ValueError(f"illegal generated removal pair {a}, {b}")
        active[a] = active[b] = False
    return result


def mode1_byte(pens):
    value = 0
    for pixel, pen in enumerate(pens):
        if pen & 1:
            value |= 1 << (7 - pixel)
        if pen & 2:
            value |= 1 << (3 - pixel)
    return value


def glyph_mask(cell, atlas_name, margin):
    # The source has antialiasing and black grid lines.  Remove the grid margin,
    # locate only the dark glyph, then reduce with a high-quality filter before
    # applying the final one-bit threshold.
    if cell.width <= margin * 2 or cell.height <= margin * 2:
        raise ValueError(f"{atlas_name} atlas cells are too small for margin {margin}")
    cell = cell.crop((margin, margin, cell.width - margin, cell.height - margin))
    dark = cell.point(lambda p: 255 if p < 160 else 0)
    bbox = dark.getbbox()
    if bbox is None:
        raise ValueError(f"empty cell in {atlas_name} atlas")
    glyph = cell.crop(bbox)
    scale = min(GLYPH_W / glyph.width, GLYPH_H / glyph.height)
    width = max(1, round(glyph.width * scale))
    height = max(1, round(glyph.height * scale))
    glyph = glyph.resize((width, height), Image.Resampling.LANCZOS)
    pixels = glyph.load()
    mask = [[False] * TILE_W for _ in range(TILE_H)]
    x0 = (15 - width) // 2
    y0 = (17 - height) // 2
    for y in range(height):
        for x in range(width):
            if pixels[x, y] < 180:
                mask[y0 + y][x0 + x] = True
    return mask


def tile_art(mask):
    packed = []
    for y in range(TILE_H):
        row = []
        for x in range(TILE_W):
            # Pen 1 is the normal solid face. Pen 2 draws the outline, glyph,
            # and one-pixel right/bottom depth edge. The app recolours pen 1 to
            # pen 3 when selected without duplicating the artwork.
            if x == 15 or y == 17 or x == 0 or x == 14 or y == 0 or y == 16:
                pen = 2
            else:
                pen = 1
            if mask[y][x]:
                pen = 2
            row.append(pen)
        for x in range(0, TILE_W, 4):
            packed.append(mode1_byte(row[x : x + 4]))
    if len(packed) != TILE_BYTES:
        raise ValueError("internal tile packing error")
    return packed


def comma_lines(values, width=16):
    lines = []
    for start in range(0, len(values), width):
        lines.append("    " + ", ".join(str(v) for v in values[start : start + width]) + ",")
    return "\n".join(lines)


def string_lines(values, width=8):
    lines = []
    for start in range(0, len(values), width):
        lines.append(
            "    " + ", ".join(f'"{value}"' for value in values[start : start + width]) + ","
        )
    return "\n".join(lines)


def atlas_art(source, atlas_name, cols, rows, margin):
    image = Image.open(source).convert("L")
    if image.width % cols or image.height % rows:
        raise ValueError(
            f"atlas {image.width}x{image.height} is not divisible by "
            f"{cols}x{rows}"
        )
    cell_w = image.width // cols
    cell_h = image.height // rows
    art = []
    for row in range(rows):
        for col in range(cols):
            cell = image.crop(
                (col * cell_w, row * cell_h, (col + 1) * cell_w, (row + 1) * cell_h)
            )
            art.extend(tile_art(glyph_mask(cell, atlas_name, margin)))
    return art


def generate(sources):
    art = []
    face_counts = []
    art_offsets = []
    for source, (atlas_name, cols, rows, margin) in zip(sources, ATLAS_SPECS):
        art_offsets.append(len(art))
        face_counts.append(cols * rows)
        art.extend(atlas_art(source, atlas_name, cols, rows, margin))

    if max(face_counts) != len(ROMAJI):
        raise ValueError(
            f"pronunciation table has {len(ROMAJI)} labels, "
            f"but the largest atlas has {max(face_counts)} faces"
        )
    if any(len(label) > 3 for label in ROMAJI):
        raise ValueError("pronunciation labels must fit in three characters")

    layout = turtle_layout()
    order = solvable_order(layout)
    rows = [p[0] for p in layout]
    cols = [p[1] for p in layout]
    levels = [p[2] for p in layout]
    return f"""/* Generated by tools/png2mahjong.py from the Kana atlases. Do not edit. */
#ifndef MAHJONG_KANA_H
#define MAHJONG_KANA_H

#define MJ_TILE_COUNT 144
#define MJ_TILESET_COUNT {len(ATLAS_SPECS)}
#define MJ_FACE_COUNT_MAX {max(face_counts)}
#define MJ_TILE_WB {TILE_W // 4}
#define MJ_TILE_H {TILE_H}
#define MJ_TILE_BYTES {TILE_BYTES}
#define MJ_ART_BYTES {len(art)}

static const unsigned char mj_face_count[MJ_TILESET_COUNT] = {{
{comma_lines(face_counts)}
}};
static const unsigned int mj_art_offset[MJ_TILESET_COUNT] = {{
{comma_lines(art_offsets)}
}};
static const char mj_pronunciation[MJ_FACE_COUNT_MAX][4] = {{
{string_lines(ROMAJI)}
}};

static const unsigned char mj_row[MJ_TILE_COUNT] = {{
{comma_lines(rows)}
}};
static const unsigned char mj_col[MJ_TILE_COUNT] = {{
{comma_lines(cols)}
}};
static const unsigned char mj_level[MJ_TILE_COUNT] = {{
{comma_lines(levels)}
}};
static const unsigned char mj_solution[MJ_TILE_COUNT] = {{
{comma_lines(order)}
}};
static const unsigned char mj_tile_art[MJ_ART_BYTES] = {{
{comma_lines(art)}
}};

#endif
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the output is stale")
    parser.add_argument("katakana", type=Path)
    parser.add_argument("hiragana", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    generated = generate((args.katakana, args.hiragana))
    current = args.output.read_text() if args.output.exists() else None
    if args.check:
        if current != generated:
            raise SystemExit(f"{args.output} is stale; run tools/png2mahjong.py")
        print(
            f"{args.output}: current "
            "(42 Katakana, 36 Hiragana, 144-position solvable Turtle layout)"
        )
        return
    if current != generated:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        print(
            f"generated {args.output} "
            "(42 Katakana, 36 Hiragana, "
            f"{len(turtle_layout())} tiles)"
        )
    else:
        print(f"up to date {args.output}")


if __name__ == "__main__":
    main()
