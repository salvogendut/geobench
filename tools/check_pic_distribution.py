#!/usr/bin/env python3
"""Verify that every shipped platform carries canonical .PIC and .BDP bytes."""

import sys
from collections import defaultdict
from pathlib import Path

from picture_catalog import picture_mode


ROOT = Path(__file__).resolve().parents[1]


def edsk_tracks(path: Path):
    image = path.read_bytes()
    if not image.startswith(b"EXTENDED CPC DSK File\r\nDisk-Info\r\n"):
        raise ValueError(f"{path}: not an extended DSK image")
    tracks = image[0x30]
    sides = image[0x31]
    pos = 256
    out = []
    for index in range(tracks * sides):
        size = image[0x34 + index] * 256
        if not size:                         # valid unformatted slot in an extended DSK
            continue
        track = image[pos:pos + size]
        if len(track) != size or size < 256:
            raise ValueError(f"{path}: malformed track {index}")
        sectors = {}
        data_pos = 256
        for sector_index in range(track[0x15]):
            desc = 0x18 + sector_index * 8
            sector_id = track[desc + 2]
            sector_size = track[desc + 6] | (track[desc + 7] << 8)
            if not sector_size:
                sector_size = 128 << track[desc + 3]
            sectors[sector_id] = track[data_pos:data_pos + sector_size]
            data_pos += sector_size
        out.append((track[0x10], track[0x11], sectors))
        pos += size
    return out


def logical_sectors(path: Path, first_sector: int):
    sectors = []
    for _track, _side, by_id in edsk_tracks(path):
        for sector_id in range(first_sector, first_sector + 9):
            if sector_id in by_id:
                sectors.append(by_id[sector_id])
    return sectors


def cpm_files(sectors, offset_tracks: int, block_size: int, directory_blocks: int):
    sectors_per_block = block_size // 512
    data = sectors[offset_tracks * 9:]
    directory_size = directory_blocks * block_size
    directory = b"".join(data[:directory_size // 512])
    entries = defaultdict(list)
    for pos in range(0, directory_size, 32):
        entry = directory[pos:pos + 32]
        if len(entry) < 32 or entry[0] in (0xE5,):
            continue
        base = bytes(value & 0x7F for value in entry[1:9]).decode("ascii").rstrip()
        ext = bytes(value & 0x7F for value in entry[9:12]).decode("ascii").rstrip()
        name = base + (("." + ext) if ext else "")
        extent = entry[12] + (entry[14] << 5)
        entries[name].append((extent, entry))

    files = {}
    for name, extents in entries.items():
        payload = bytearray()
        for _extent, entry in sorted(extents):
            records = entry[15]
            block_count = (records * 128 + block_size - 1) // block_size
            blocks = []
            for index in range(block_count):
                if block_size == 2048:
                    block = entry[16 + index * 2] | (entry[17 + index * 2] << 8)
                else:
                    block = entry[16 + index]
                first = block * sectors_per_block
                blocks.append(b"".join(data[first:first + sectors_per_block]))
            payload.extend(b"".join(blocks)[:records * 128])
        files[name] = bytes(payload)
    return files


def strip_amsdos(data: bytes) -> bytes:
    if len(data) < 128:
        return data
    checksum = data[67] | (data[68] << 8)
    if (sum(data[:67]) & 0xFFFF) != checksum:
        return data
    length = data[24] | (data[25] << 8)
    return data[128:128 + length]


def cpc_disk(path: Path):
    return cpm_files(logical_sectors(path, 0xC1), 0, 1024, 2)


def pcw_disk(path: Path):
    sectors = logical_sectors(path, 1)
    spec = sectors[0]
    block_size = 128 << spec[6]
    return cpm_files(sectors, spec[5], block_size, spec[7])


def compare_payload(label: str, actual: bytes, expected: bytes, padded: bool = False):
    if padded:
        if actual[:len(expected)] != expected or any(byte != 0x1A for byte in actual[len(expected):]):
            raise ValueError(f"{label}: payload differs from canonical asset")
    elif actual != expected:
        raise ValueError(f"{label}: payload differs from canonical asset")


def main() -> None:
    pictures = sorted((ROOT / "assets/pictures").glob("*.PIC"))
    if not pictures:
        sys.exit("no pictures found")
    try:
        modes = {path.name: picture_mode(path) for path in pictures}
    except ValueError as error:
        sys.exit(str(error))
    assets = {
        path.name: path.read_bytes() for path in pictures if modes[path.name] == 1
    }
    msx_assets = {path.name: path.read_bytes() for path in pictures}
    if not assets:
        sys.exit("no portable mode-1 pictures found")

    backdrops = {path.name.upper(): path.read_bytes() for path in sorted((ROOT / "assets/backdrops").glob("*.BDP"))}
    if not backdrops:
        sys.exit("no canonical backdrops found")
    for name, data in backdrops.items():
        if len(data) != 64:
            sys.exit(f"assets/backdrops/{name}: expected a 64-byte canonical Mode-1 tile")

    titlebars = {
        path.name.upper(): path.read_bytes()
        for path in sorted((ROOT / "assets/titlebars").glob("*.TBR"))
    }
    if not titlebars:
        sys.exit("no canonical title-bar motifs found")
    for name, data in titlebars.items():
        if len(data) not in (56, 106):
            sys.exit(
                f"assets/titlebars/{name}: expected a 56-byte tile or 106-byte legacy theme"
            )
    gadgets = {
        path.name.upper(): path.read_bytes()
        for path in sorted((ROOT / "assets/gadgets").glob("*.GDT"))
    }
    if not gadgets:
        sys.exit("no canonical title-bar gadget themes found")
    for name, data in gadgets.items():
        if len(data) != 50:
            sys.exit(f"assets/gadgets/{name}: expected a 50-byte gadget pair")

    for distro, expected_assets in (
        (ROOT / "QA/CPC/CARD/PICS", assets),
        (ROOT / "QA/MSX/PICS", msx_assets),
    ):
        names = {path.name for path in distro.glob("*.PIC")}
        if names != set(expected_assets):
            sys.exit(f"{distro.relative_to(ROOT)}: picture set differs from assets/pictures")
        for name, expected in expected_assets.items():
            compare_payload(str((distro / name).relative_to(ROOT)), (distro / name).read_bytes(), expected)

    cpc_extras = cpc_disk(ROOT / "QA/CPC/Floppies/EXTRAS.DSK")
    pcw_extras = pcw_disk(ROOT / "QA/PCW/EXTRAS.DSK")
    cpc_required = {
        "DISKUTIL.APP", "XROACH.SAV", "CATCLK.SAV", "HELIX.SAV",
        "MOUNTAIN.SAV", "MOUNTAIN.MOD", "WELCOME.TXT"
    }
    missing = cpc_required - set(cpc_extras)
    if missing:
        sys.exit(f"QA/CPC/Floppies/EXTRAS.DSK: missing extras: {', '.join(sorted(missing))}")
    if {name for name in cpc_extras if name.endswith(".PIC")} != set(assets):
        sys.exit("QA/CPC/Floppies/EXTRAS.DSK: picture catalogue differs from assets/pictures")
    if {name for name in pcw_extras if name.endswith(".PIC")} != set(assets):
        sys.exit("QA/PCW/EXTRAS.DSK: picture catalogue differs from assets/pictures")
    pcw_required = {
        "PAINT.APP", "PAINT.IST", "BASIC.APP", "BASRUN.APP", "BASRUN2.BIN",
        "ANT.SAV", "DECO.SAV", "XMATRIX.SAV", "XMATRIX.MOD",
        "MOUNTAIN.SAV", "MOUNTAIN.MOD",
        "ART.BAS", "CHASE.BAS", "GUESS.BAS", "HELLO.BAS", "PRIMES.BAS",
    }
    missing = pcw_required - set(pcw_extras)
    if missing:
        sys.exit(f"QA/PCW/EXTRAS.DSK: missing extras: {', '.join(sorted(missing))}")
    for name, expected in assets.items():
        compare_payload(f"QA/CPC/Floppies/EXTRAS.DSK:{name}", strip_amsdos(cpc_extras[name]), expected)
        compare_payload(f"QA/PCW/EXTRAS.DSK:{name}", pcw_extras[name], expected, padded=True)

    cpc_main = cpc_disk(ROOT / "QA/CPC/Floppies/GEOBENCH.DSK")
    pcw_main = pcw_disk(ROOT / "QA/PCW/GEOBENCH.DSK")
    for distro in (ROOT / "QA/CPC/CARD/GBENCH", ROOT / "QA/MSX/GBENCH"):
        names = {path.name for path in distro.glob("*.TBR")}
        if names != set(titlebars):
            sys.exit(f"{distro.relative_to(ROOT)}: title-bar set differs from assets/titlebars")
        for name, expected in titlebars.items():
            compare_payload(
                str((distro / name).relative_to(ROOT)),
                (distro / name).read_bytes(), expected,
            )
        names = {path.name for path in distro.glob("*.GDT")}
        if names != set(gadgets):
            sys.exit(f"{distro.relative_to(ROOT)}: gadget set differs from assets/gadgets")
        for name, expected in gadgets.items():
            compare_payload(
                str((distro / name).relative_to(ROOT)),
                (distro / name).read_bytes(), expected,
            )
    floppy_titlebars = {"IMPROVED.TBR", "ORIGINAL.TBR"}
    extras_titlebars = set(titlebars) - floppy_titlebars
    floppy_gadgets = {"ORIGINAL.GDT"}
    extras_gadgets = set(gadgets) - floppy_gadgets
    for label, files, padded in (
        ("QA/CPC/Floppies/GEOBENCH.DSK", cpc_main, False),
        ("QA/PCW/GEOBENCH.DSK", pcw_main, True),
    ):
        names = {name for name in files if name.endswith(".TBR")}
        if names != floppy_titlebars:
            sys.exit(f"{label}: expected only IMPROVED.TBR and ORIGINAL.TBR")
        for name in floppy_titlebars:
            payload = files[name] if padded else strip_amsdos(files[name])
            compare_payload(f"{label}:{name}", payload, titlebars[name], padded=padded)
        names = {name for name in files if name.endswith(".GDT")}
        if names != floppy_gadgets:
            sys.exit(f"{label}: expected only ORIGINAL.GDT")
        payload = files["ORIGINAL.GDT"] if padded else strip_amsdos(files["ORIGINAL.GDT"])
        compare_payload(
            f"{label}:ORIGINAL.GDT", payload, gadgets["ORIGINAL.GDT"], padded=padded
        )
    for label, files, padded in (
        ("QA/CPC/Floppies/EXTRAS.DSK", cpc_extras, False),
        ("QA/PCW/EXTRAS.DSK", pcw_extras, True),
    ):
        names = {name for name in files if name.endswith(".TBR")}
        if names != extras_titlebars:
            sys.exit(f"{label}: secondary title-bar catalogue is incomplete")
        for name in extras_titlebars:
            payload = files[name] if padded else strip_amsdos(files[name])
            compare_payload(f"{label}:{name}", payload, titlebars[name], padded=padded)
        names = {name for name in files if name.endswith(".GDT")}
        if names != extras_gadgets:
            sys.exit(f"{label}: secondary gadget catalogue is incomplete")
        for name in extras_gadgets:
            payload = files[name] if padded else strip_amsdos(files[name])
            compare_payload(f"{label}:{name}", payload, gadgets[name], padded=padded)
    compare_payload(
        "QA/CPC/Floppies/GEOBENCH.DSK:GBTITLE.MOD",
        strip_amsdos(cpc_main["GBTITLE.MOD"]),
        (ROOT / "build/GBTITLE.RAW").read_bytes(),
    )
    compare_payload(
        "QA/CPC/CARD/GBENCH/GBTITLE.MOD",
        (ROOT / "QA/CPC/CARD/GBENCH/GBTITLE.MOD").read_bytes(),
        (ROOT / "build/GBTITLE.RAW").read_bytes(),
    )
    compare_payload(
        "QA/MSX/GBENCH/GBTITLE.MOD",
        (ROOT / "QA/MSX/GBENCH/GBTITLE.MOD").read_bytes(),
        (ROOT / "build/msx/GBTITLE.RAW").read_bytes(),
    )
    compare_payload(
        "QA/PCW/GEOBENCH.DSK:GBTITLE.MOD",
        pcw_main["GBTITLE.MOD"], (ROOT / "build/pcw/GBTITLE.RAW").read_bytes(),
        padded=True,
    )
    compare_payload(
        "QA/CPC/Floppies/GEOBENCH.DSK:DEFAULT.SPR",
        cpc_main["DEFAULT.SPR"],
        (ROOT / "QA/CPC/CARD/GBENCH/DEFAULT.SPR").read_bytes(),
    )
    compare_payload("QA/CPC/Floppies/GEOBENCH.DSK:LOGO.PIC", strip_amsdos(cpc_main["LOGO.PIC"]), assets["LOGO.PIC"])
    compare_payload("QA/PCW/GEOBENCH.DSK:LOGO.PIC", pcw_main["LOGO.PIC"], assets["LOGO.PIC"], padded=True)
    compare_payload(
        "QA/CPC/Floppies/GEOBENCH.DSK:DEFAULT.CFG",
        strip_amsdos(cpc_main["DEFAULT.CFG"]),
        strip_amsdos(cpc_main["GEOBENCH.CFG"]),
    )
    compare_payload(
        "QA/PCW/GEOBENCH.DSK:DEFAULT.CFG",
        pcw_main["DEFAULT.CFG"],
        pcw_main["GEOBENCH.CFG"],
    )
    if b"TIMESYNC=false\r\n" not in pcw_main["GEOBENCH.CFG"]:
        sys.exit("QA/PCW/GEOBENCH.DSK: time sync must be disabled by default")
    for mutable, pristine in (
        (ROOT / "QA/CPC/CARD/GEOBENCH.CFG", ROOT / "QA/CPC/CARD/GBENCH/DEFAULT.CFG"),
        (ROOT / "QA/MSX/GEOBENCH.CFG", ROOT / "QA/MSX/GBENCH/DEFAULT.CFG"),
    ):
        compare_payload(
            str(pristine.relative_to(ROOT)),
            pristine.read_bytes(),
            mutable.read_bytes(),
        )
        if b"TITLEBAR=ORIGINAL\r\n" not in mutable.read_bytes():
            sys.exit(f"{mutable.relative_to(ROOT)}: TITLEBAR must default to ORIGINAL")
        if b"GADGETS=ORIGINAL\r\n" not in mutable.read_bytes():
            sys.exit(f"{mutable.relative_to(ROOT)}: GADGETS must default to ORIGINAL")

    cpc_companion = cpc_disk(ROOT / "QA/CPC/Floppies/COMPANION.DSK")
    pcw_companion = pcw_disk(ROOT / "QA/PCW/COMPANION.DSK")
    configurable_saver_modules = {
        "XMATRIX.MOD", "MOUNTAIN.MOD", "STARFLD.MOD"
    }
    if not {"XMATRIX.MOD", "STARFLD.MOD"} <= set(cpc_companion):
        sys.exit("QA/CPC/Floppies/COMPANION.DSK: missing saver Configure module")
    for path, main_files, companion_files in (
        ("QA/CPC", cpc_main, cpc_companion),
        ("QA/PCW", pcw_main, pcw_companion),
    ):
        if "GBIMG.MOD" in main_files or "GBIMG.MOD" not in companion_files:
            sys.exit(f"{path}: GBIMG.MOD must live beside Browser on Companion")
    for path, files in (
        ("QA/CPC/Floppies/COMPANION.DSK", cpc_companion),
        ("QA/PCW/COMPANION.DSK", pcw_companion),
    ):
        if any(name.endswith(".PIC") for name in files):
            sys.exit(f"{path}: companion disk must not contain pictures")

    for directory in (ROOT / "QA/CPC/CARD/GBENCH", ROOT / "QA/MSX/GBENCH"):
        names = {path.name for path in directory.iterdir()}
        if not configurable_saver_modules <= names:
            sys.exit(f"{directory.relative_to(ROOT)}: missing saver Configure module")
        if any(directory.glob("*.PIC")):
            sys.exit(f"{directory.relative_to(ROOT)}: pictures must live under PICS")

    for directory in (ROOT / "QA/CPC/CARD/GBENCH", ROOT / "QA/MSX/GBENCH"):
        names = {path.name for path in directory.glob("*.BDP")}
        if names != set(backdrops):
            sys.exit(f"{directory.relative_to(ROOT)}: backdrop set differs from assets/backdrops")
        for name, expected in backdrops.items():
            compare_payload(str((directory / name).relative_to(ROOT)), (directory / name).read_bytes(), expected)

    cpc_bdp = {name for name in cpc_main if name.endswith(".BDP")}
    pcw_bdp = {name for name in pcw_companion if name.endswith(".BDP")}
    if cpc_bdp != set(backdrops):
        sys.exit("QA/CPC/Floppies/GEOBENCH.DSK: backdrop catalogue differs from assets/backdrops")
    if pcw_bdp != set(backdrops):
        sys.exit("QA/PCW/COMPANION.DSK: backdrop catalogue differs from assets/backdrops")
    for name, expected in backdrops.items():
        compare_payload(f"QA/CPC/Floppies/GEOBENCH.DSK:{name}", strip_amsdos(cpc_main[name]), expected)
        compare_payload(f"QA/PCW/COMPANION.DSK:{name}", pcw_companion[name], expected, padded=True)

    mode7_count = len(msx_assets) - len(assets)
    print(f"portable PIC distribution: {len(assets)} byte-identical pictures across CPC, MSX and PCW")
    print(f"MSX Screen 7 distribution: {mode7_count} additional pictures in QA/MSX/PICS")
    print(f"portable BDP distribution: {len(backdrops)} byte-identical backdrops across CPC, MSX and PCW")
    print(
        f"title bars: {len(titlebars)} motifs on card/MSX; "
        "IMPROVED and ORIGINAL on CPC/PCW boot floppies; remaining motifs on "
        "EXTRAS.DSK; ORIGINAL default"
    )
    print(
        f"title gadgets: {len(gadgets)} themes on card/MSX; ORIGINAL on CPC/PCW "
        "boot floppies; remaining themes on EXTRAS.DSK"
    )
    print("CPC floppy cursor: headerless DEFAULT.SPR matches the card distribution")
    print("target defaults: pristine DEFAULT.CFG matches GEOBENCH.CFG on CPC, MSX and PCW; PCW time sync disabled")


if __name__ == "__main__":
    main()
