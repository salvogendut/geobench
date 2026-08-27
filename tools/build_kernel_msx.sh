#!/usr/bin/env bash
# tools/build_kernel_msx.sh - build the MSX2 target (#287): Screen 6/7 kernels,
# the GBMSX.COM next-boot selector, and the M1 app/asset set. Stage everything
# into QA/MSX/CARD and pack it into the bootable Nextor image QA/MSX/GBMSX.IMG.
#
# Kept separate from tools/build_kernel.sh (that script is CPC-DSK-entangled
# and wipes its own QA outputs); the shared pieces (apps via build_capp.sh,
# GBCFG via build_cfgmod.sh, the Python asset tools) are reused. Portable GBPC
# v2 pictures, icon sets, and backdrop tiles remain canonical and are translated
# to native bytes by the kernel at display/load time.
#
#   bash tools/build_kernel_msx.sh       # uses QA/MSXDEPS/UNAPINET.COM
#   MSX_UNAPI_TSR=/path/to/UNAPINET.COM bash tools/build_kernel_msx.sh
#   MSX_UNAPI_TSR= bash tools/build_kernel_msx.sh  # explicitly omit the TSR
#   MSX_SHOTS="20 30 45" tools/run_msx.sh      # then verify in openMSX
set -euo pipefail
cd "$(dirname "$0")/.."

RASM="${RASM:-rasm}"
command -v "$RASM" >/dev/null || { echo "ERROR: rasm not on PATH" >&2; exit 1; }
command -v sdcc >/dev/null || { echo "ERROR: sdcc not on PATH" >&2; exit 1; }

PREEMPTIVE="${PREEMPTIVE:-1}"
PREEMPTIVE_DIAGNOSTIC="${PREEMPTIVE_DIAGNOSTIC:-0}"
NOTEPAD_APPDEFS="-DGBDOC_BOUNDED_IO"
NOTEPAD_DATA_LOC="0x6F48"
NOTEPAD_CFLAGS="--opt-code-size --max-allocs-per-node 100000"
NOTEPAD_SCROLL=1
if [ "$PREEMPTIVE" = "1" ]; then
    RASM="$RASM" bash tools/build_scheduler.sh msx
    EXTRA_RASM="${EXTRA_RASM:-} -DPREEMPTIVE=1 -DPREEMPTIVE_CONTEXT=1"
    export EXTRA_RASM
    export GLOBAL_APPDEFS="${GLOBAL_APPDEFS:-} -DGB_PREEMPTIVE"
    if [ "$PREEMPTIVE_DIAGNOSTIC" = "1" ]; then
        export GLOBAL_APPDEFS="$GLOBAL_APPDEFS -DGB_PREEMPTIVE_DIAGNOSTIC"
    elif [ "$PREEMPTIVE_DIAGNOSTIC" != "0" ]; then
        echo "PREEMPTIVE_DIAGNOSTIC must be 0 or 1" >&2
        exit 2
    fi
elif [ "$PREEMPTIVE" != "0" ]; then
    echo "PREEMPTIVE must be 0 or 1" >&2
    exit 2
elif [ "$PREEMPTIVE_DIAGNOSTIC" != "0" ]; then
    echo "PREEMPTIVE_DIAGNOSTIC requires PREEMPTIVE=1" >&2
    exit 2
fi

TITLEBAR_RASM="-DTITLEBAR_TILE=1"
RASM="$RASM" bash tools/build_titlebarmod.sh

# fetch_msx_deps.sh supplies the standard local guest driver. An explicitly
# set MSX_UNAPI_TSR overrides it; setting the variable to empty deliberately
# produces a network-free image. The binary stays in ignored paths.
UNAPI_LICENSE=docs/licenses/OPENMSXNET.md
if [ "${MSX_UNAPI_TSR+x}" = x ]; then
    UNAPI_TSR="$MSX_UNAPI_TSR"
elif [ -s QA/MSXDEPS/UNAPINET.COM ]; then
    UNAPI_TSR=QA/MSXDEPS/UNAPINET.COM
else
    UNAPI_TSR=
fi

mkdir -p build/msx

GB_PAINT_DIR="${GB_PAINT_DIR:-../GB-PAINT}"
PAINT_APP_DIR="$GB_PAINT_DIR/apps/paint"
if [ -f "$PAINT_APP_DIR/main.c" ] && [ -d "$GB_PAINT_DIR/assets/paint" ]; then
    PAINT_ASSET_DIR="$GB_PAINT_DIR/assets/paint"
else
    echo "ERROR: GB-PAINT checkout not found at $GB_PAINT_DIR" >&2
    echo "Set GB_PAINT_DIR=/path/to/GB-PAINT or clone it next to geobench." >&2
    exit 1
fi
GB_BASIC_DIR="${GB_BASIC_DIR:-../GB-BASIC}"
if [ ! -f "$GB_BASIC_DIR/Makefile" ] || [ ! -d "$GB_BASIC_DIR/apps/basic" ]; then
    echo "ERROR: GB-BASIC checkout not found at $GB_BASIC_DIR" >&2
    echo "Set GB_BASIC_DIR=/path/to/GB-BASIC or clone it next to geobench." >&2
    exit 1
fi
GEOBENCH_ROOT="$(pwd)"
PAINT_GBLIB="build/msx/GBLIBPAINT.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$PAINT_GBLIB" "$PAINT_APP_DIR/gblib.symbols"
TELNET_GBLIB="build/msx/GBLIBTELNET.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$TELNET_GBLIB" apps/telnet/gblib.symbols
VIEWER_GBLIB="build/msx/GBLIBVIEWER.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$VIEWER_GBLIB" apps/viewer/gblib.symbols
NOTEPAD_GBLIB="build/msx/GBLIBNOTEPAD.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$NOTEPAD_GBLIB" apps/notepad/gblib.symbols
ICONED_GBLIB="build/msx/GBLIBICONED.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$ICONED_GBLIB" apps/iconed/gblib.symbols

# --- the C apps, compiled with the MSX geometry ------------------------------
python3 tools/png2mahjong.py assets/katakana.png assets/hiragana.png apps/mahjong/kana.h
if [ "$PREEMPTIVE" = "1" ]; then
    TASK_ROOT=1 TASK_RUNTIME_RAW=build/msx/GBSCHED.RAW \
        TASK_STACK_RESERVE=256 APPDEFS="-DGB_MSX2" DATA_LOC=0x7300 DOC=1 TITLEBAR=1 \
        tools/build_capp.sh apps/desktop build/msx/DESKTOP.RAW
    if [ "$PREEMPTIVE_DIAGNOSTIC" = "1" ]; then
        TASK=1 TASK_STACK_RESERVE=256 APPDEFS="-DGB_MSX2" DATA_LOC=0x6200 \
            tools/build_capp.sh apps/taskdemo build/msx/TASKDEMO.RAW
    fi
else
    APPDEFS="-DGB_MSX2" DATA_LOC=0x7100 DOC=1 TITLEBAR=1 \
        tools/build_capp.sh apps/desktop build/msx/DESKTOP.RAW
fi
APPDEFS="-DGB_MSX2" APP_CFLAGS="--max-allocs-per-node 5000" DATA_LOC=0x7960 DOC=1 SCROLL=1 REPAINTTOP=1 tools/build_capp.sh apps/filemgr build/msx/FILEMGR.RAW
APP_ICON=apps/notepad/icon.asm GBLIB_SRC="$NOTEPAD_GBLIB" APPDEFS="-DGB_MSX2 $NOTEPAD_APPDEFS" APP_CFLAGS="$NOTEPAD_CFLAGS" DATA_LOC="$NOTEPAD_DATA_LOC" DOC=1 REPAINTTOP="$NOTEPAD_SCROLL" tools/build_capp.sh apps/notepad build/msx/NOTEPAD.RAW
APPDEFS="-DGB_MSX2" APP_CFLAGS="--opt-code-size --max-allocs-per-node 100000" DATA_LOC=0x7C40 DIALOGS=1 STEPPER=1 SELECTOR=1 ACTIONS=1 TITLEBAR=1 tools/build_capp.sh apps/settings build/msx/SETTINGS.RAW
APPDEFS="-DGB_MSX2" DIALOGS=1 BUTTON=1 tools/build_capp.sh apps/diskutil build/msx/DISKUTIL.RAW  # FAT12 quick-format (WRABS)
if [ "$PREEMPTIVE" = "1" ]; then
    TASK=1 TASK_STACK_RESERVE=256 APP_ICON=apps/xaos/icon.asm APPDEFS="-DGB_MSX2" \
        DATA_LOC=0x6600 DOC=1 BUTTON=1 tools/build_capp.sh apps/xaos build/msx/XAOS.RAW
else
    APP_ICON=apps/xaos/icon.asm APPDEFS="-DGB_MSX2" DATA_LOC=0x6400 DOC=1 BUTTON=1 \
        tools/build_capp.sh apps/xaos build/msx/XAOS.RAW
fi
APP_ICON=apps/iconed/icon.asm GBLIB_SRC="$ICONED_GBLIB" APPDEFS="-DGB_MSX2 -DGBUI_APPICON_PICKER -DGBDOC_BOUNDED_IO" APP_CFLAGS="--max-allocs-per-node 100000" DATA_LOC=0x7200 DOC=1 BUTTON=1 REPAINTTOP=1 tools/build_capp.sh apps/iconed build/msx/ICONED.RAW
APP_ICON=apps/viewer/icon.asm GBLIB_SRC="$VIEWER_GBLIB" APPDEFS="-DGB_MSX2" DATA_LOC=0x6A40 DOCRO=1 SCROLL16=1 REPAINTTOP=1 tools/build_capp.sh apps/viewer build/msx/VIEWER.RAW
APP_ICON="$PAINT_APP_DIR/icon.asm" APP_ICON16="$PAINT_APP_DIR/icon16.asm" GBLIB_SRC="$PAINT_GBLIB" APPDEFS="-DGB_MSX2" APP_CFLAGS="--opt-code-size --max-allocs-per-node 100000" HELPER_CFLAGS="--opt-code-size --max-allocs-per-node 100000" DATA_LOC=0x7DA0 PICKER=1 SIZEPROMPT=1 GBWIN=0 tools/build_capp.sh "$PAINT_APP_DIR" build/msx/PAINT.RAW
APPDEFS="-DGB_MSX2" DATA_LOC=0x6780 DOC=1 WIDGETS=1 STEPPER=1 FORM=1 TIMESET=1 tools/build_capp.sh apps/clock build/msx/CLOCK.RAW
APP_ICON=apps/shell/icon.asm APPDEFS="-DGB_MSX2" DATA_LOC=0x6D00 SCROLL=1 tools/build_capp.sh apps/shell build/msx/SHELL.RAW
APP_ICON=apps/mahjong/icon.asm APPDEFS="-DGB_MSX2" DATA_LOC=0x7100 DIALOGS=1 tools/build_capp.sh apps/mahjong build/msx/MAHJONG.RAW
APPDEFS="-DGB_MSX2" DATA_LOC=0x6800 BUTTON=1 tools/build_capp.sh apps/calculator build/msx/CALC.RAW
APP_ICON=apps/telnet/icon.asm GBLIB_SRC="$TELNET_GBLIB" APPDEFS="-DGB_MSX2" DATA_LOC=0x7300 NET=1 DOC=1 tools/build_capp.sh apps/telnet build/msx/TELNET.RAW
APP_ICON=apps/formref/icon.asm APP_ICON16=apps/formref/icon16.asm APPDEFS="-DGB_MSX2" DATA_LOC=0x6200 WIDGETS=1 STEPPER=1 SELECTOR=1 ACTIONS=1 FORM=1 FORM_SELECT=1 tools/build_capp.sh apps/formref build/msx/FORMREF.RAW
APPDEFS="-DGB_MSX2" DATA_LOC=0x6200 BUTTON=1 SOUND=1 tools/build_capp.sh apps/sndtest build/msx/SNDTEST.RAW
# DOX dispatch leaves Browser's loaded image just above the old 0x7E00 split;
# its 91-byte data/BSS footprint still fits comfortably below 0x8000 here.
APP_ICON=apps/browser/icon.asm APPDEFS="-DGB_MSX2" GBWIN=0 GBLIB_SRC=lib/gb/gblib_browser.s APP_CFLAGS="--max-allocs-per-node 100000" DATA_LOC=0x7E80 NET=1 tools/build_capp.sh apps/browser build/msx/BROWSER.RAW
APPDEFS="-DGB_MSX2" DATA_LOC=0x6200 tools/build_capp.sh apps/brsave build/msx/BRSAVE.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/saver build/msx/SQUARES.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/ant  build/msx/ANT.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/deco build/msx/DECO.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/xmatrix build/msx/XMATRIX.RAW
APPDEFS="-DGB_MSX2" tools/build_savercfg.sh apps/xmatrix build/msx/XMATRIXCFG.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/mountain build/msx/MOUNTAIN.RAW
APPDEFS="-DGB_MSX2" tools/build_savercfg.sh apps/mountain build/msx/MOUNTAINCFG.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/forest build/msx/FOREST.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/starfield build/msx/STARFLD.RAW
APPDEFS="-DGB_MSX2" tools/build_savercfg.sh apps/starfield build/msx/STARFLDCFG.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/fractalic build/msx/FRACTALI.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/munch build/msx/MUNCH.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/rorschach build/msx/RORSCH.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/truchet build/msx/TRUCHET.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/lightning build/msx/LIGHTN.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/pyro build/msx/PYRO.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/helix build/msx/HELIX.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/xroach build/msx/XROACH.RAW
APPDEFS="-DGB_MSX2" tools/build_capp.sh apps/catclock build/msx/CATCLK.RAW

# --- the shared config-parser module (platform-neutral C over low RAM) -----
APPDEFS="-DGB_MSX2" tools/build_cfgmod.sh build/msx/GBCFG.RAW
tools/build_uimod.sh                             # -> build/GBUI.RAW (dialogs/menus)
APPDEFS="-DGB_MSX2" tools/build_appickmod.sh build/msx/GBAPICK.RAW
tools/build_webmod.sh build/msx/GBWEB.RAW        # Browser cache/config helper
APPDEFS="-DGB_MSX2" tools/build_imgmod.sh build/msx/GBIMG.RAW # Browser inline-image helper
APPDEFS="-DGB_MSX2" tools/build_doxmod.sh build/msx/GBDOX.RAW # Browser DOX/PIC decoder

# --- assets ------------------------------------------------------------------
python3 tools/genfont.py build/msx/DEFAULT.FNT           # 1bpp glyphs: shared format
python3 tools/packicons.py build/msx/DEFAULT.IST \
    lib/icon_floppy.asm lib/icon_clock.asm lib/icon_trash.asm \
    lib/icon_geobench.asm lib/icon_basic.asm lib/icon_binary.asm \
    lib/icon_picture.asm lib/icon_text.asm lib/icon_folder.asm \
    lib/icon_app.asm lib/icon_font.asm \
    lib/icon_desktop.asm lib/icon_filemanager.asm \
    lib/icon_sd.asm \
    lib/icon_up.asm lib/icon_screensaver.asm \
    lib/icon_cf.asm lib/icon_ide.asm lib/icon_fractal.asm \
    lib/icon_settings.asm lib/icon_calculator.asm
python3 tools/packicons.py build/msx/PAINT.IST \
    "$PAINT_ASSET_DIR/pencil.asm" "$PAINT_ASSET_DIR/line.asm" \
    "$PAINT_ASSET_DIR/square.asm" "$PAINT_ASSET_DIR/boxfill.asm" \
    "$PAINT_ASSET_DIR/circle.asm" "$PAINT_ASSET_DIR/circlefill.asm" \
    "$PAINT_ASSET_DIR/bucket.asm" "$PAINT_ASSET_DIR/spray.asm" \
    "$PAINT_ASSET_DIR/select.asm" "$PAINT_ASSET_DIR/cut.asm" \
    "$PAINT_ASSET_DIR/copy.asm" "$PAINT_ASSET_DIR/paste.asm" \
    "$PAINT_ASSET_DIR/undo.asm"
"$RASM" kernel/modules/picedit_low.asm >/dev/null
# The MSX pointer is a hand-edited 66-byte V9938 hardware sprite. Keep the
# target-specific source under assets and stage it under the configured default
# cursor name; CPC and PCW continue to derive their software cursors separately.
[ -s assets/thinner.SPR ] || { echo "ERROR: missing MSX cursor assets/thinner.SPR" >&2; exit 1; }
cp assets/thinner.SPR build/msx/DEFAULT.SPR

# Bootsplash (#196/#287): the CPC lollipop, transcoded to the packed four-pen
# MSX UI format shared by both video backends. The MSX
# kernel does not incbin it (that is CPC-only) - boot_splash loads SPLASH.MOD
# from disk, so we just stage that compact bitmap. DEBUG=TRUE selects the
# SPLASHD.MOD variant with the build id.
BUILD_COMMIT="$(git rev-parse --short=12 HEAD 2>/dev/null || printf unknown)"
python3 tools/make_bootsplash.py assets/SPLASH.png build/msx/SPLASH_BUILD.png "$BUILD_COMMIT" GEOBENCH
python3 tools/png2cpc.py --platform msx2 build/msx/SPLASH_BUILD.png build/msx/SPLASH.BIN splash 96x184
python3 tools/make_bootsplash.py assets/SPLASH.png build/msx/SPLASHD_BUILD.png "$BUILD_COMMIT"
python3 tools/png2cpc.py --platform msx2 build/msx/SPLASHD_BUILD.png build/msx/SPLASHD.BIN splash 96x184

echo "Building GB-BASIC MSX payload from $GB_BASIC_DIR"
make -C "$GB_BASIC_DIR" raws-msx GEOBENCH="$GEOBENCH_ROOT"

# --- the two kernels, their stubs, and the small next-boot selector ----------
# RASM exits 0 even on assembly errors, so stale outputs would silently ship:
# remove them first and require fresh files after each pass.
rm -f build/msx/GBKERNM.RAW build/msx/GBKERN6.RAW build/msx/GBKERN7.RAW \
      build/msx/GBMSX.COM build/msx/GBMSX6.COM build/msx/GBMSX7.COM

# Compatibility backend: Screen 6, four native colours.
( cd build/msx && "$RASM" ../../kernel/gbkern.asm -DPLATFORM_MSX=1 -s -o gbkernm ${EXTRA_RASM:-} $TITLEBAR_RASM )
[ -s build/msx/GBKERNM.RAW ] || { echo "ERROR: GBKERNM.RAW not produced (rasm errors above)" >&2; exit 1; }
cp build/msx/GBKERNM.RAW build/msx/GBKERN6.RAW
( cd build/msx && "$RASM" ../../kernel/msx_stub.asm )
[ -s build/msx/GBMSX.COM ] || { echo "ERROR: Screen-6 loader stub not produced" >&2; exit 1; }
mv build/msx/GBMSX.COM build/msx/GBMSX6.COM

# Extended backend: Screen 7, with sixteen-colour Viewer support.
rm -f build/msx/GBKERNM.RAW
( cd build/msx && "$RASM" ../../kernel/gbkern.asm -DPLATFORM_MSX=1 -DMSX_SCREEN7=1 -s -o gbkernm7 ${EXTRA_RASM:-} $TITLEBAR_RASM )
[ -s build/msx/GBKERNM.RAW ] || { echo "ERROR: Screen-7 GBKERNM.RAW not produced" >&2; exit 1; }
cp build/msx/GBKERNM.RAW build/msx/GBKERN7.RAW
( cd build/msx && "$RASM" ../../kernel/msx_stub.asm -DMSX_SCREEN7=1 )
[ -s build/msx/GBMSX.COM ] || { echo "ERROR: Screen-7 loader stub not produced" >&2; exit 1; }
mv build/msx/GBMSX.COM build/msx/GBMSX7.COM

# GBMSX.COM reads MSXMODE= and chain-loads one of the mode-specific images.
( cd build/msx && "$RASM" ../../kernel/msx_launcher.asm )
[ -s build/msx/GBMSX.COM ] || { echo "ERROR: MSX video selector not produced" >&2; exit 1; }

# --- stage QA/MSX/CARD ---------------------------------------------------------
mkdir -p QA/MSX/CARD/GBENCH
rm -rf QA/MSX/CARD/PICS
rm -f QA/MSX/CARD/GBENCH/*.TBR QA/MSX/CARD/GBENCH/*.GDT
find QA/MSX/CARD -maxdepth 1 -type f -name '*.PIC' -delete
find QA/MSX/CARD/GBENCH -maxdepth 1 -type f -name '*.PIC' -delete
mkdir -p QA/MSX/CARD/PICS QA/MSX/CARD/DIAG
rm -f QA/MSX/CARD/GBSPIKE.COM                 # pre-DIAG staging location (#379)
cp build/msx/GBMSX.COM build/msx/GBMSX6.COM build/msx/GBMSX7.COM QA/MSX/CARD/
rm -f QA/MSX/CARD/DIAG/FORMREF.APP             # MSX app loading resolves through /GBENCH
rm -f QA/MSX/CARD/UNAPINET.COM QA/MSX/CARD/UNAPI.TXT
if [ -n "$UNAPI_TSR" ]; then
    [ -s "$UNAPI_TSR" ] || { echo "ERROR: MSX_UNAPI_TSR not found: $UNAPI_TSR" >&2; exit 1; }
    [ -s "$UNAPI_LICENSE" ] || { echo "ERROR: missing $UNAPI_LICENSE" >&2; exit 1; }
    cp "$UNAPI_TSR" QA/MSX/CARD/UNAPINET.COM
    cp "$UNAPI_LICENSE" QA/MSX/CARD/UNAPI.TXT
    printf 'UNAPINET\r\nGBMSX\r\n' > QA/MSX/CARD/AUTOEXEC.BAT
else
    printf 'GBMSX\r\n' > QA/MSX/CARD/AUTOEXEC.BAT
fi
printf 'FONT=DEFAULT\r\nICONS=REFINED\r\nCURSOR=DEFAULT\r\nTITLEBAR=ORIGINAL\r\nGADGETS=ORIGINAL\r\nVIEW=DEFAULT\r\nBACKDROP=SOLID\r\nWALLPAPER=LOGO\r\nSAVER=SQUARES\r\nSAVERTIME=2\r\nSTARFLD_SPEED=4\r\nSTARFLD_STARS=64\r\nXMATRIX_GLYPHS=0\r\nXMATRIX_SPEED=2\r\nXMATRIX_COLOR=4\r\nMOUNTAIN_SPEED=2\r\nMOUNTAIN_PEAKS=15\r\nMOUNTAIN_HOLD=120\r\nMSXMOUSE=TRUE\r\nMSXMODE=7\r\n' > QA/MSX/CARD/GEOBENCH.CFG
cp QA/MSX/CARD/GEOBENCH.CFG QA/MSX/CARD/GBENCH/DEFAULT.CFG
cp build/msx/DESKTOP.RAW  QA/MSX/CARD/GBENCH/DESKTOP.APP
cp build/msx/FILEMGR.RAW  QA/MSX/CARD/GBENCH/FILEMGR.APP
cp build/msx/NOTEPAD.RAW  QA/MSX/CARD/GBENCH/NOTEPAD.APP
cp build/msx/SETTINGS.RAW QA/MSX/CARD/GBENCH/SETTINGS.APP
cp build/msx/DISKUTIL.RAW QA/MSX/CARD/GBENCH/DISKUTIL.APP
cp build/msx/XAOS.RAW     QA/MSX/CARD/GBENCH/XAOS.APP
cp build/msx/ICONED.RAW   QA/MSX/CARD/GBENCH/ICONED.APP
cp build/msx/VIEWER.RAW   QA/MSX/CARD/GBENCH/VIEWER.APP
cp build/msx/PAINT.RAW    QA/MSX/CARD/GBENCH/PAINT.APP
cp build/msx/PAINT.IST    QA/MSX/CARD/GBENCH/PAINT.IST
cp build/msx/SHELL.RAW    QA/MSX/CARD/GBENCH/SHELL.APP
cp build/msx/MAHJONG.RAW  QA/MSX/CARD/GBENCH/MAHJONG.APP
cp build/msx/CALC.RAW     QA/MSX/CARD/GBENCH/CALC.APP
cp build/msx/TELNET.RAW   QA/MSX/CARD/GBENCH/TELNET.APP
cp build/msx/BROWSER.RAW  QA/MSX/CARD/GBENCH/BROWSER.APP
cp build/msx/BRSAVE.RAW   QA/MSX/CARD/GBENCH/BRSAVE.APP
cp build/msx/FORMREF.RAW  QA/MSX/CARD/GBENCH/FORMREF.APP
cp build/msx/SNDTEST.RAW  QA/MSX/CARD/GBENCH/SNDTEST.APP
if [ "$PREEMPTIVE_DIAGNOSTIC" = "1" ]; then
    cp build/msx/TASKDEMO.RAW QA/MSX/CARD/GBENCH/TASKDEMO.APP
else
    rm -f QA/MSX/CARD/GBENCH/TASKDEMO.APP
fi
for f in "$GB_BASIC_DIR/build/msx/BASIC.RAW" "$GB_BASIC_DIR/build/msx/BASRUN.RAW" "$GB_BASIC_DIR/build/msx/BASRUN2.BIN"; do
    [ -s "$f" ] || { echo "ERROR: missing GB-BASIC MSX payload $f (run make -C \"$GB_BASIC_DIR\" raws-msx)" >&2; exit 1; }
done
cp "$GB_BASIC_DIR/build/msx/BASIC.RAW"   QA/MSX/CARD/GBENCH/BASIC.APP
cp "$GB_BASIC_DIR/build/msx/BASRUN.RAW"  QA/MSX/CARD/GBENCH/BASRUN.APP
cp "$GB_BASIC_DIR/build/msx/BASRUN2.BIN" QA/MSX/CARD/GBENCH/BASRUN2.BIN
for bas in "$GB_BASIC_DIR"/examples/*.BAS; do
    [ -e "$bas" ] || continue
    sed 's/$/\r/' "$bas" > "QA/MSX/CARD/GBENCH/$(basename "$bas")"
done
cp build/msx/CLOCK.RAW    QA/MSX/CARD/GBENCH/CLOCK.APP
cp build/msx/SQUARES.RAW  QA/MSX/CARD/GBENCH/SQUARES.SAV
cp build/msx/ANT.RAW      QA/MSX/CARD/GBENCH/ANT.SAV
cp build/msx/DECO.RAW     QA/MSX/CARD/GBENCH/DECO.SAV
cp build/msx/XMATRIX.RAW  QA/MSX/CARD/GBENCH/XMATRIX.SAV
cp build/msx/XMATRIXCFG.RAW QA/MSX/CARD/GBENCH/XMATRIX.MOD
cp build/msx/MOUNTAIN.RAW QA/MSX/CARD/GBENCH/MOUNTAIN.SAV
cp build/msx/MOUNTAINCFG.RAW QA/MSX/CARD/GBENCH/MOUNTAIN.MOD
cp build/msx/FOREST.RAW   QA/MSX/CARD/GBENCH/FOREST.SAV
cp build/msx/STARFLD.RAW  QA/MSX/CARD/GBENCH/STARFLD.SAV
cp build/msx/STARFLDCFG.RAW QA/MSX/CARD/GBENCH/STARFLD.MOD
cp build/msx/FRACTALI.RAW QA/MSX/CARD/GBENCH/FRACTALI.SAV
cp build/msx/MUNCH.RAW QA/MSX/CARD/GBENCH/MUNCH.SAV
cp build/msx/RORSCH.RAW QA/MSX/CARD/GBENCH/RORSCH.SAV
cp build/msx/TRUCHET.RAW QA/MSX/CARD/GBENCH/TRUCHET.SAV
cp build/msx/LIGHTN.RAW QA/MSX/CARD/GBENCH/LIGHTN.SAV
cp build/msx/PYRO.RAW QA/MSX/CARD/GBENCH/PYRO.SAV
cp build/msx/HELIX.RAW QA/MSX/CARD/GBENCH/HELIX.SAV
cp build/msx/XROACH.RAW   QA/MSX/CARD/GBENCH/XROACH.SAV
cp build/msx/CATCLK.RAW   QA/MSX/CARD/GBENCH/CATCLK.SAV
cp assets/WELCOME.TXT     QA/MSX/CARD/WELCOME.TXT
cp build/msx/GBCFG.RAW  QA/MSX/CARD/GBENCH/GBCFG.MOD
cp build/GBUI.RAW       QA/MSX/CARD/GBENCH/GBUI.MOD
cp build/msx/GBAPICK.RAW QA/MSX/CARD/GBENCH/GBAPICK.MOD
cp build/msx/GBWEB.RAW  QA/MSX/CARD/GBENCH/GBWEB.MOD
cp build/msx/GBIMG.RAW  QA/MSX/CARD/GBENCH/GBIMG.MOD
cp build/msx/GBDOX.RAW  QA/MSX/CARD/GBENCH/GBDOX.MOD
cp build/msx/SPLASH.BIN  QA/MSX/CARD/GBENCH/SPLASH.MOD
cp build/msx/SPLASHD.BIN QA/MSX/CARD/GBENCH/SPLASHD.MOD
cp build/msx/GBTITLE.RAW QA/MSX/CARD/GBENCH/GBTITLE.MOD
cp build/msx/DEFAULT.FNT QA/MSX/CARD/GBENCH/
cp build/msx/DEFAULT.IST QA/MSX/CARD/GBENCH/
cp build/msx/DEFAULT.SPR QA/MSX/CARD/GBENCH/
for tbr in build/titlebars/*.TBR; do
    [ -e "$tbr" ] && cp "$tbr" QA/MSX/CARD/GBENCH/
done
for gdt in build/gadgets/*.GDT; do
    [ -e "$gdt" ] && cp "$gdt" QA/MSX/CARD/GBENCH/
done

# --- drop-in assets: canonical backdrops/iconsets/pictures are copied unchanged,
# mirroring the CPC build's globs (build_kernel.sh / stage_dist.sh) so a file
# dropped in ships on both distros. Names are uppercased 8.3. (#287/#388)
for bdp in assets/backdrops/*.BDP; do            # backdrop tiles (BACKDROP=<name>)
    [ -e "$bdp" ] || continue
    name=$(basename "$bdp" .BDP | tr a-z A-Z)
    cp "$bdp" "QA/MSX/CARD/GBENCH/$name.BDP"
done
for png in assets/backdrops/*.png; do            # ... or a source PNG with no .BDP
    [ -e "$png" ] || continue
    name=$(basename "$png" .png | tr a-z A-Z)
    [ -e "QA/MSX/CARD/GBENCH/$name.BDP" ] || \
        python3 tools/png2backdrop.py "$png" "QA/MSX/CARD/GBENCH/$name.BDP"
done
for ist in assets/iconsets/*.IST; do             # icon sets (ICONS=<name>)
    [ -e "$ist" ] || continue
    name=$(basename "$ist" .IST | tr a-z A-Z)
    cp "$ist" "QA/MSX/CARD/GBENCH/$name.IST"
done
# Mode 1 stays portable; mode 7 is copied only by this MSX build.
while IFS= read -r pic; do
    name=$(basename "$pic" .PIC | tr a-z A-Z)
    cp "$pic" "QA/MSX/CARD/PICS/$name.PIC"
done < <(python3 tools/picture_catalog.py msx)

# --- bootable Nextor image ------------------------------------------------------
bash tools/build_msx_img.sh QA/MSX/CARD QA/MSX/GBMSX.IMG
bash tools/build_msx_floppy.sh QA/MSX/CARD QA/MSX/Floppies

echo "MSX2 target built: QA/MSX/CARD (staged) + QA/MSX/GBMSX.IMG + QA/MSX/Floppies/*.DSK"
