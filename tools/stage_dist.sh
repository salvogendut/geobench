#!/usr/bin/env bash
# stage_dist.sh <outdir>: stage the UNIFIED GEOBENCH card distribution (#134 + #136).
# One image works on any machine. On-card layout:
#   GB.BAS       - BASIC loader: probes the bus and RUN"s the right per-card kernel
#   GBIDE.BIN    - IDE (SYMBiFACE/Cyboard) kernel   } real 128-byte AMSDOS header,
#   GBALB.BIN    - Albireo (CH376) kernel           } exec 0x8000
#   GEOBENCH.CFG - config (root; read before the kernel enters /GEOBENCH)
#   GEOBENCH/    - everything the kernel loads at boot (apps/modules/fonts/icons/cursor)
# Boot: RUN"GB -> GB.BAS detects IDE/Albireo -> RUN"GBIDE/GBALB -> the kernel reads
# /GEOBENCH (IDE walks the FAT subdir; Albireo prefixes alb_path). A machine-code
# loader is impossible under UniDOS (see memory geobench-loader-136), hence BASIC.
# Needs build/GBIDE.RAW + build/GBALB.RAW (captured by build_kernel.sh per variant).
#   * <app>.APP / GBCFG/GBFAT/FLOPPYSV.BIN - HEADERLESS raw images (kernel loads them)
#   * GB.BAS / GEOBENCH.CFG - written with CR+LF line endings (the CPC requires them)
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:?usage: tools/stage_dist.sh <outdir>}"
SYS="$OUT/GEOBENCH"                  # everything the kernel loads lives here
mkdir -p "$SYS"

# --- root: the loader, both per-card kernels, the config ----------------------
printf '10 OUT &FD0B,&55:IF INP(&FD0B)<>&55 THEN 40\r\n20 OUT &FD0B,&AA:IF INP(&FD0B)<>&AA THEN 40\r\n30 RUN"GBIDE\r\n40 OUT &FE81,6:OUT &FE80,&57:IF INP(&FE80)=&A8 THEN RUN"GBALB\r\n50 RUN"GBIDE\r\n' \
    > "$OUT/GB.BAS"
python3 tools/amsdos_header.py build/GBIDE.RAW "$OUT/GBIDE.BIN" GBIDE BIN 0x8000
python3 tools/amsdos_header.py build/GBALB.RAW "$OUT/GBALB.BIN" GBALB BIN 0x8000
printf 'FONT=DEFAULT\r\nICONS=REFINED\r\nCURSOR=DEFAULT\r\nVIEW=DEFAULT\r\n' \
    > "$OUT/GEOBENCH.CFG"

# --- /GEOBENCH: apps, modules, assets -----------------------------------------
for a in DESKTOP FILEMGR VIEWER NOTEPAD ICONED CLOCK PAINT XAOS; do
    cp "build/$a.RAW" "$SYS/$a.APP"
done
cp build/GBCFG.RAW "$SYS/GBCFG.BIN"
cp build/GBFAT.RAW "$SYS/GBFAT.BIN"
cp build/FLOPPYSV.RAW "$SYS/FLOPPYSV.BIN"   # #135: paged AMSDOS/floppy write module
cp build/GBUI.RAW "$SYS/GBUI.BIN"           # #142: paged dialog (popup/prompt/file-picker) module
cp build/DEFAULT.FNT build/CLASSIC.FNT build/DEFAULT.IST build/PAINT.IST \
   build/DEFAULT.SPR build/HAND.SPR "$SYS/"
for ist in assets/iconsets/*.IST; do          # tracked custom icon sets (edit with
    [ -e "$ist" ] && cp "$ist" "$SYS/"         # tools/iconedit.py); select via ICONS=<name>
done
cp assets/WELCOME.TXT "$SYS/"
cp assets/penguin.PIC "$SYS/PENGUIN.PIC"   # sample 200x200 picture (view in Viewer)
