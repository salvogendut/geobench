#!/usr/bin/env bash
# Build a *partitioned* FAT16 IDE/Symbiface disk image carrying GEOBENCH, for
# booting under UniDOS+FatFS (run"GBKERN). Layout:
#
#   LBA 0            MBR (one primary partition, type 0x06 = FAT16)
#   LBA PSTART..end  FAT16 volume (the partition)
#
# FatFS (ChaN, on the Symbiface FATFS ROMs) scans the MBR partition table and
# mounts the first FAT partition; a bare superfloppy image (FAT BPB at LBA 0)
# is reported as "Device ???" and rejected, so the MBR is required.
#
# Everything stays inside the first 32 MB so the kernel's 16-bit LBA reader can
# reach every sector (PSTART + volume sectors < 65536).
#
# GBKERN.BIN gets a 128-byte AMSDOS header so UniDOS can RUN it; every file
# GBKERN loads itself (fside_load_file) is copied RAW (the IDE backend does NOT
# strip a header, unlike the floppy backend).
#
# Usage: tools/build_ide_img.sh [out.img]
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-build/GEOBENCH.IMG}"
HDR="tools/amsdos_header.py"

TOTAL_SECTORS=65536          # 32 MB total (keeps every LBA < 65536, 16-bit)
PSTART=64                    # partition starts at LBA 64 (32 KB in)
PSECTORS=$(( TOTAL_SECTORS - PSTART ))
POFF=$(( PSTART * 512 ))     # partition byte offset for mtools @@

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

# 1) Blank image of the full size.
rm -f "$OUT"
truncate -s $(( TOTAL_SECTORS * 512 )) "$OUT"

# 2) Hand-built MBR: one FAT16 partition. Boot code left zero (first byte 0x00,
#    so the kernel can tell an MBR from a FAT boot sector, which starts 0xEB).
python3 - "$OUT" "$PSTART" "$PSECTORS" <<'PY'
import sys, struct
path, pstart, psecs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
mbr = bytearray(512)
ent = bytearray(16)
ent[0]  = 0x00                                   # not bootable
ent[1:4] = bytes((0xFE, 0xFF, 0xFF))             # start CHS (ignored, LBA used)
ent[4]  = 0x06                                   # type: FAT16
ent[5:8] = bytes((0xFE, 0xFF, 0xFF))             # end CHS (ignored)
ent[8:12]  = struct.pack("<I", pstart)           # start LBA
ent[12:16] = struct.pack("<I", psecs)            # sector count
mbr[446:462] = ent
mbr[510], mbr[511] = 0x55, 0xAA
with open(path, "r+b") as f:
    f.write(mbr)
PY

# 3) Format the partition region as FAT16 (mformat picks FAT16 for ~32 MB).
MIMG="${OUT}@@${POFF}"
mformat -i "$MIMG" -T "$PSECTORS" -h 0 -s 0 -v GEOBENCH ::

# 4) Stage files: GBKERN headered, everything else raw.
python3 "$HDR" build/GBKERN.RAW "$STAGE/GBKERN.BIN" GBKERN BIN 0x8000
cp build/DESKTOP.RAW "$STAGE/DESKTOP.BIN"
cp build/FILEMGR.RAW "$STAGE/FILEMGR.BIN"
cp build/VIEWER.RAW  "$STAGE/VIEWER.BIN"
cp build/NOTEPAD.RAW "$STAGE/NOTEPAD.BIN"
cp build/CHELLO.RAW  "$STAGE/CHELLO.BIN"
cp build/GBCFG.RAW   "$STAGE/GBCFG.BIN"
cp build/GBFAT.RAW   "$STAGE/GBFAT.BIN"
cp build/DEFAULT.FNT "$STAGE/DEFAULT.FNT"
cp build/DEFAULT.IST "$STAGE/DEFAULT.IST"
for ist in assets/iconsets/*.IST; do          # tracked custom icon sets (ICONS=<name>)
    [ -e "$ist" ] && cp "$ist" "$STAGE/"
done
cp assets/WELCOME.TXT "$STAGE/WELCOME.TXT"

for f in "$STAGE"/*; do
    mcopy -i "$MIMG" -o "$f" "::/$(basename "$f")"
done

echo "Built $OUT  (MBR + FAT16 partition @ LBA $PSTART)"
mdir -i "$MIMG" ::/
