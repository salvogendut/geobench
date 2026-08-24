#!/usr/bin/env bash
# tools/build_kernel_pcw.sh - build the Amstrad PCW target (#331): the
# GBKERNP.RAW kernel on its own boot sector, the core app/asset set, staged
# into QA/PCW and packed into GEOBENCH.DSK (bootable CF2), COMPANION.DSK,
# and EXTRAS.DSK (CF2DD picture gallery plus standalone applications).
#
# The PCW boots standalone (no CP/M): kernel/pcwboot.asm loads the kernel
# from the disc's reserved tracks; system files live in the disc's CP/M 2.2
# filesystem (read by lib/pcw/fs.asm, written at build time by mkpcwdsk.py).
#
# GBPC v2 pictures, icon sets, and backdrop tiles stay canonical; the kernel
# translates them at runtime. Save-block-format blobs such as the splash still
# need the final CGA2 hardware-pen permutation at build time.
#
#   bash tools/build_kernel_pcw.sh
#   SDL_VIDEODRIVER=dummy ~/Dev/1985/1985 --config debug/1985-pcw.conf \
#       --disk-a QA/PCW/GEOBENCH.DSK --screenshot-at 600:/tmp/pcw.ppm
set -euo pipefail
cd "$(dirname "$0")/.."

RASM="${RASM:-rasm}"
GB_PAINT_DIR="${GB_PAINT_DIR:-../GB-PAINT}"
GB_BASIC_DIR="${GB_BASIC_DIR:-../GB-BASIC}"
command -v "$RASM" >/dev/null || { echo "ERROR: rasm not on PATH" >&2; exit 1; }
command -v sdcc >/dev/null || { echo "ERROR: sdcc not on PATH" >&2; exit 1; }

TITLEBAR_RASM="-DTITLEBAR_TILE=1"
RASM="$RASM" bash tools/build_titlebarmod.sh
[ -f "$GB_PAINT_DIR/Makefile" ] || {
    echo "ERROR: GB-PAINT checkout not found at $GB_PAINT_DIR" >&2
    echo "Set GB_PAINT_DIR=/path/to/GB-PAINT or clone it next to geobench." >&2
    exit 1
}
[ -f "$GB_BASIC_DIR/Makefile" ] || {
    echo "ERROR: GB-BASIC checkout not found at $GB_BASIC_DIR" >&2
    echo "Set GB_BASIC_DIR=/path/to/GB-BASIC or clone it next to geobench." >&2
    exit 1
}

mkdir -p build/pcw QA/PCW
TELNET_GBLIB="build/pcw/GBLIBTELNET.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$TELNET_GBLIB" apps/telnet/gblib.symbols
VIEWER_GBLIB="build/pcw/GBLIBVIEWER.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$VIEWER_GBLIB" apps/viewer/gblib.symbols

# --- the C apps, compiled with the PCW geometry (same DATA_LOCs as CPC/MSX) --
python3 tools/png2mahjong.py assets/katakana.png assets/hiragana.png apps/mahjong/kana.h
APPDEFS="-DGB_PCW" DATA_LOC=0x7100 DOC=1 TITLEBAR=1 tools/build_capp.sh apps/desktop build/pcw/DESKTOP.RAW
APPDEFS="-DGB_PCW" APP_CFLAGS="--max-allocs-per-node 5000" DATA_LOC=0x7960 DOC=1 SCROLL=1 tools/build_capp.sh apps/filemgr build/pcw/FILEMGR.RAW
APP_ICON=apps/notepad/icon.asm APPDEFS="-DGB_PCW" DATA_LOC=0x6BF0 DOC=1 tools/build_capp.sh apps/notepad build/pcw/NOTEPAD.RAW
APPDEFS="-DGB_PCW" APP_CFLAGS="--opt-code-size --max-allocs-per-node 20000" DATA_LOC=0x7C40 DIALOGS=1 STEPPER=1 SELECTOR=1 ACTIONS=1 TITLEBAR=1 tools/build_capp.sh apps/settings build/pcw/SETTINGS.RAW
APP_ICON=apps/viewer/icon.asm GBLIB_SRC="$VIEWER_GBLIB" APPDEFS="-DGB_PCW" DATA_LOC=0x6A30 DOCRO=1 SCROLL16=1 tools/build_capp.sh apps/viewer build/pcw/VIEWER.RAW
APPDEFS="-DGB_PCW" DATA_LOC=0x6780 DOC=1 WIDGETS=1 STEPPER=1 FORM=1 TIMESET=1 tools/build_capp.sh apps/clock build/pcw/CLOCK.RAW
APP_ICON=apps/xaos/icon.asm APPDEFS="-DGB_PCW" DATA_LOC=0x6400 DOC=1 BUTTON=1 tools/build_capp.sh apps/xaos build/pcw/XAOS.RAW
APP_ICON=apps/iconed/icon.asm APPDEFS="-DGB_PCW -DGBUI_APPICON_PICKER" APP_CFLAGS="--max-allocs-per-node 100000" DATA_LOC=0x7000 DOC=1 BUTTON=1 tools/build_capp.sh apps/iconed build/pcw/ICONED.RAW
APP_ICON=apps/telnet/icon.asm GBLIB_SRC="$TELNET_GBLIB" APPDEFS="-DGB_PCW" DATA_LOC=0x7380 DOC=1 tools/build_capp.sh apps/telnet build/pcw/TELNET.RAW
APPDEFS="-DGB_PCW" DATA_LOC=0x7400 tools/build_capp.sh apps/nettest build/pcw/NETTEST.RAW
APP_ICON=apps/formref/icon.asm APP_ICON16=apps/formref/icon16.asm APPDEFS="-DGB_PCW" DATA_LOC=0x6200 WIDGETS=1 STEPPER=1 SELECTOR=1 ACTIONS=1 FORM=1 FORM_SELECT=1 tools/build_capp.sh apps/formref build/pcw/FORMREF.RAW
APPDEFS="-DGB_PCW" DATA_LOC=0x6200 BUTTON=1 SOUND=1 tools/build_capp.sh apps/sndtest build/pcw/SNDTEST.RAW
APPDEFS="-DGB_PCW" DATA_LOC=0x7940 DIALOGS=1 WIDGETS=1 tools/build_capp.sh apps/wget build/pcw/WGET.RAW
APP_ICON=apps/browser/icon.asm GBWIN=0 GBLIB_SRC=lib/gb/gblib_browser.s APP_CFLAGS="--max-allocs-per-node 100000" LOAD_LIMIT=0x7F80 APPDEFS="-DGB_PCW" DATA_LOC=0x7FA4 tools/build_capp.sh apps/browser build/pcw/BROWSER.RAW
APPDEFS="-DGB_PCW" DATA_LOC=0x6200 tools/build_capp.sh apps/brsave build/pcw/BRSAVE.RAW
APPDEFS="-DGB_PCW" DATA_LOC=0x7400 tools/build_capp.sh apps/timesync build/pcw/TIMESYNC.RAW
APP_ICON=apps/shell/icon.asm APPDEFS="-DGB_PCW" DATA_LOC=0x6D00 SCROLL=1 tools/build_capp.sh apps/shell build/pcw/SHELL.RAW
APP_ICON=apps/mahjong/icon.asm APPDEFS="-DGB_PCW" DATA_LOC=0x7100 DIALOGS=1 tools/build_capp.sh apps/mahjong build/pcw/MAHJONG.RAW
APPDEFS="-DGB_PCW" DATA_LOC=0x6800 BUTTON=1 tools/build_capp.sh apps/calculator build/pcw/CALC.RAW
# savers: the PORTABLE (pure gb_* API) subset - the direct-#C000 ones need a
# PCW plot path first (follow-up)
APPDEFS="-DGB_PCW" tools/build_capp.sh apps/saver build/pcw/SQUARES.RAW
APPDEFS="-DGB_PCW" tools/build_capp.sh apps/ant  build/pcw/ANT.RAW
APPDEFS="-DGB_PCW" tools/build_capp.sh apps/deco build/pcw/DECO.RAW
APPDEFS="-DGB_PCW" tools/build_capp.sh apps/xmatrix build/pcw/XMATRIX.RAW
APPDEFS="-DGB_PCW" tools/build_savercfg.sh apps/xmatrix build/pcw/XMATRIXCFG.RAW
APPDEFS="-DGB_PCW" tools/build_capp.sh apps/mountain build/pcw/MOUNTAIN.RAW
APPDEFS="-DGB_PCW" tools/build_savercfg.sh apps/mountain build/pcw/MOUNTAINCFG.RAW

# --- shared paged C modules (platform-neutral, low-RAM marshalled) -----------
tools/build_cfgmod.sh                            # -> build/GBCFG.RAW
tools/build_uimod.sh                             # -> build/GBUI.RAW
APPDEFS="-DGB_PCW" tools/build_appickmod.sh build/pcw/GBAPICK.RAW
tools/build_webmod.sh                            # -> build/GBWEB.RAW
tools/build_imgmod.sh                            # -> build/GBIMG.RAW

# --- portable icons/pictures; target-native fonts, pointer and splash ---------
python3 tools/genfont.py build/pcw/DEFAULT.FNT
python3 tools/packfont.py build/pcw/CLASSIC.FNT lib/font.asm   # 8x8 (FONT=CLASSIC)
python3 tools/packicons.py build/pcw/DEFAULT.IST \
    lib/icon_floppy.asm lib/icon_clock.asm lib/icon_trash.asm \
    lib/icon_geobench.asm lib/icon_basic.asm lib/icon_binary.asm \
    lib/icon_picture.asm lib/icon_text.asm lib/icon_folder.asm \
    lib/icon_app.asm lib/icon_font.asm \
    lib/icon_desktop.asm lib/icon_filemanager.asm \
    lib/icon_sd.asm \
    lib/icon_up.asm lib/icon_screensaver.asm \
    lib/icon_cf.asm lib/icon_ide.asm lib/icon_fractal.asm \
    lib/icon_settings.asm lib/icon_calculator.asm
cp assets/iconsets/REFINED.IST build/pcw/REFINED.IST

# the pointer: interleaved software-cursor .SPR in CGA2 hardware space
"$RASM" kernel/modules/picedit_low.asm -DPLATFORM_PCW=1 >/dev/null
python3 tools/png2spr.py --platform pcw assets/pointer.png build/pcw/DEFAULT.SPR cursor 12x16
cat build/pcw/PICEDITL.RAW >> build/pcw/DEFAULT.SPR

# bootsplash: Screen-6 transcode, then the CGA2 hardware-pen permute
# (boot_splash blits it with restore_block, which writes raw bytes)
BUILD_COMMIT="$(git rev-parse --short=12 HEAD 2>/dev/null || printf unknown)"
python3 tools/make_bootsplash.py assets/SPLASH.png build/pcw/SPLASH_BUILD.png "$BUILD_COMMIT" GEOBENCH
python3 tools/png2cpc.py --platform msx2 build/pcw/SPLASH_BUILD.png build/pcw/SPLASH.BIN splash 96x184
python3 - build/pcw/SPLASH.BIN build/pcw/SPLASH.MOD <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
open(sys.argv[2], 'wb').write(bytes(((b & 0x55) << 1) | (((b ^ 0xFF) & 0xAA) >> 1) for b in d))
PY
cp assets/pictures/LOGO.PIC build/pcw/LOGO.PIC

# --- the kernel + the boot sector ---------------------------------------------
rm -f build/pcw/GBKERNP.RAW build/pcwboot.bin
( cd build/pcw && "$RASM" ../../kernel/gbkern.asm -DPLATFORM_PCW=1 -s -o gbkernp ${EXTRA_RASM:-} $TITLEBAR_RASM )
[ -s build/pcw/GBKERNP.RAW ] || { echo "ERROR: GBKERNP.RAW not produced (rasm errors above)" >&2; exit 1; }
"$RASM" kernel/pcwboot.asm
[ -s build/pcwboot.bin ] || { echo "ERROR: pcwboot.bin not produced" >&2; exit 1; }

# --- the bootable disc -----------------------------------------------------------
printf 'FONT=DEFAULT\r\nICONS=REFINED\r\nCURSOR=DEFAULT\r\nTITLEBAR=ORIGINAL\r\nGADGETS=ORIGINAL\r\nVIEW=DEFAULT\r\nBACKDROP=SOLID\r\nWALLPAPER=LOGO\r\nSAVER=SQUARES\r\nSAVERTIME=2\r\nSTARFLD_SPEED=4\r\nSTARFLD_STARS=64\r\nXMATRIX_GLYPHS=0\r\nXMATRIX_SPEED=2\r\nMOUNTAIN_SPEED=2\r\nMOUNTAIN_PEAKS=15\r\nMOUNTAIN_HOLD=120\r\nTIMESYNC=false\r\nTIMEZONE=+2\r\nPROXY=\r\n' > build/pcw/GEOBENCH.CFG
cp build/pcw/GEOBENCH.CFG build/pcw/DEFAULT.CFG
python3 tools/mkpcwdsk.py QA/PCW/GEOBENCH.DSK \
    --boot build/pcwboot.bin --sys build/pcw/GBKERNP.RAW --load 0x8000 \
    --add build/pcw/GEOBENCH.CFG=GEOBENCH.CFG \
    --add build/pcw/DEFAULT.CFG=DEFAULT.CFG \
    --add build/GBCFG.RAW=GBCFG.MOD \
    --add build/GBUI.RAW=GBUI.MOD \
    --add build/pcw/GBAPICK.RAW=GBAPICK.MOD \
    --add build/GBWEB.RAW=GBWEB.MOD \
    --add build/pcw/SPLASH.MOD=SPLASH.MOD \
    --add build/pcw/GBTITLE.RAW=GBTITLE.MOD \
    --add build/pcw/DEFAULT.FNT=DEFAULT.FNT \
    --add build/pcw/DEFAULT.IST=DEFAULT.IST \
    --add build/pcw/REFINED.IST=REFINED.IST \
    --add build/pcw/DEFAULT.SPR=DEFAULT.SPR \
    --add build/pcw/DESKTOP.RAW=DESKTOP.APP \
    --add build/pcw/FILEMGR.RAW=FILEMGR.APP \
    --add build/pcw/NOTEPAD.RAW=NOTEPAD.APP \
    --add build/pcw/SETTINGS.RAW=SETTINGS.APP \
    --add build/pcw/VIEWER.RAW=VIEWER.APP \
    --add build/pcw/CLOCK.RAW=CLOCK.APP \
    --add build/pcw/TIMESYNC.RAW=TIMESYNC.APP \
    --add build/pcw/ICONED.RAW=ICONED.APP \
    --add build/pcw/SHELL.RAW=SHELL.APP \
    --add build/pcw/BRSAVE.RAW=BRSAVE.APP \
    --add build/pcw/SQUARES.RAW=SQUARES.SAV \
    --add build/pcw/LOGO.PIC=LOGO.PIC \
    --add build/pcw/CLASSIC.FNT=CLASSIC.FNT \
    --add build/titlebars/ORIGINAL.TBR=ORIGINAL.TBR \
    --add build/titlebars/IMPROVED.TBR=IMPROVED.TBR \
    --add build/gadgets/ORIGINAL.GDT=ORIGINAL.GDT

# --- COMPANION.DSK: TELNET, backdrops and spare assets (plain CF2 data disc)
tools/package_pcw_companion.sh QA/PCW/COMPANION.DSK

# --- EXTRAS.DSK: portable gallery + standalone apps on a 720K CF2DD disc
echo "Building GB-PAINT PCW payload from $GB_PAINT_DIR"
make -C "$GB_PAINT_DIR" GEOBENCH="$PWD" app-pcw assets-pcw
echo "Building GB-BASIC PCW payload from $GB_BASIC_DIR"
make -C "$GB_BASIC_DIR" GEOBENCH="$PWD" raws-pcw

for f in \
    "$GB_PAINT_DIR/build/pcw/PAINT.APP" \
    "$GB_PAINT_DIR/build/PAINT.IST" \
    "$GB_BASIC_DIR/build/pcw/BASIC.RAW" \
    "$GB_BASIC_DIR/build/pcw/BASRUN.RAW" \
    "$GB_BASIC_DIR/build/pcw/BASRUN2.BIN"; do
    [ -s "$f" ] || { echo "ERROR: missing PCW extras payload $f" >&2; exit 1; }
done

rm -rf build/pcw/basic-examples
mkdir -p build/pcw/basic-examples
for bas in "$GB_BASIC_DIR"/examples/*.BAS; do
    sed 's/$/\r/' "$bas" > "build/pcw/basic-examples/$(basename "$bas")"
done

EXTRAS_ADDS=(
    --add "$GB_PAINT_DIR/build/pcw/PAINT.APP=PAINT.APP"
    --add "$GB_PAINT_DIR/build/PAINT.IST=PAINT.IST"
    --add "$GB_BASIC_DIR/build/pcw/BASIC.RAW=BASIC.APP"
    --add "$GB_BASIC_DIR/build/pcw/BASRUN.RAW=BASRUN.APP"
    --add "$GB_BASIC_DIR/build/pcw/BASRUN2.BIN=BASRUN2.BIN"
    --add "build/pcw/ANT.RAW=ANT.SAV"
    --add "build/pcw/DECO.RAW=DECO.SAV"
    --add "build/pcw/XMATRIX.RAW=XMATRIX.SAV"
    --add "build/pcw/XMATRIXCFG.RAW=XMATRIX.MOD"
    --add "build/pcw/MOUNTAIN.RAW=MOUNTAIN.SAV"
    --add "build/pcw/MOUNTAINCFG.RAW=MOUNTAIN.MOD"
)
for tbr in build/titlebars/*.TBR; do
    name=$(basename "$tbr")
    case "$name" in
        IMPROVED.TBR|ORIGINAL.TBR) continue ;;
    esac
    EXTRAS_ADDS+=(--add "$tbr=$name")
done
for gdt in build/gadgets/*.GDT; do
    name=$(basename "$gdt")
    case "$name" in
        ORIGINAL.GDT) continue ;;
    esac
    EXTRAS_ADDS+=(--add "$gdt=$name")
done
while IFS= read -r pic; do
    name=$(basename "$pic" .PIC | tr a-z A-Z)
    EXTRAS_ADDS+=(--add "$pic=$name.PIC")
done < <(python3 tools/picture_catalog.py portable)
for bas in build/pcw/basic-examples/*.BAS; do
    EXTRAS_ADDS+=(--add "$bas=$(basename "$bas")")
done
rm -f QA/PCW/MEDIA.DSK
python3 tools/mkpcwdsk.py QA/PCW/EXTRAS.DSK --type cf2dd "${EXTRAS_ADDS[@]}"

echo "PCW target built: QA/PCW/GEOBENCH.DSK + COMPANION.DSK + EXTRAS.DSK"
