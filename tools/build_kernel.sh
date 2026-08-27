#!/usr/bin/env bash
# Build the GEOBENCH banked-kernel skeleton + the HELLO app (Phase 1 proof).
#
# Apps are separate binaries; the app is built FIRST because the kernel incbins
# build/HELLO.RAW. Output: build/gbkern.dsk (GBKERN.BIN, with HELLO embedded).
#   tools/build_kernel.sh
#   1984 --memory=128 --disk-a=build/gbkern.dsk --autostart=GBKERN
set -euo pipefail

cd "$(dirname "$0")/.."          # repo root
RASM="${RASM:-rasm}"

TITLEBAR_RASM="-DTITLEBAR_TILE=1"
RASM="$RASM" bash tools/build_titlebarmod.sh

PREEMPTIVE="${PREEMPTIVE:-1}"
NOTEPAD_APPDEFS="-DGBDOC_BOUNDED_IO"
NOTEPAD_DATA_LOC="0x6F48"
NOTEPAD_CFLAGS="--opt-code-size --max-allocs-per-node 100000"
NOTEPAD_SCROLL=1
if [ "$PREEMPTIVE" = "1" ]; then
    RASM="$RASM" bash tools/build_scheduler.sh cpc
    EXTRA_RASM="${EXTRA_RASM:-} -DPREEMPTIVE=1 -DPREEMPTIVE_CONTEXT=1"
    export EXTRA_RASM
    export GLOBAL_APPDEFS="${GLOBAL_APPDEFS:-} -DGB_PREEMPTIVE"
elif [ "$PREEMPTIVE" != "0" ]; then
    echo "PREEMPTIVE must be 0 or 1" >&2
    exit 2
fi

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
CPC_QA="QA/CPC"
CARD_QA="$CPC_QA/CARD"
FLOPPY_QA="$CPC_QA/Floppies"
CARD_IMG="$CPC_QA/GEOBENCH.IMG"

# The card ships both Albireo and M4 kernels in QA/CPC/CARD and
# QA/CPC/GEOBENCH.IMG.
# STORAGE only picks the backend left in build/ for the dev harness (--disk-a):
# "albireo" (default), "m4", or "ide" (the dormant legacy backend, still buildable
# for recovery/tests). Backends are mutually exclusive per build (#104).
case "${STORAGE:-albireo}" in
    albireo) STORAGE_FLAG="-DSTORAGE_ALBIREO=1" ;;
    m4)      STORAGE_FLAG="-DSTORAGE_M4=1" ;;
    ide)     STORAGE_FLAG="" ;;
    *)
        echo "STORAGE must be one of: albireo, m4, ide" >&2
        exit 2
        ;;
esac

# FAT16=1: build the IDE kernel FAT16-only (drops the FAT32 read branches, ~81 B
# smaller resident). Real CPC IDE/SD cards are FAT16 (#130, #148); the dev/test
# FAT32 images need the default full build. Albireo is unaffected (chip does FAT).
FAT16_FLAG=""
if [ "${FAT16:-0}" = "1" ]; then
    FAT16_FLAG="-DFAT16_ONLY=1"
    echo "FAT16=1: building a FAT16-only IDE kernel (no FAT32 read path)"
fi

mkdir -p build
rm -f build/gbkern.dsk                        # save-to-DSK appends; start clean
PAINT_GBLIB="build/GBLIBPAINT.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$PAINT_GBLIB" "$PAINT_APP_DIR/gblib.symbols"
TELNET_GBLIB="build/GBLIBTELNET.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$TELNET_GBLIB" apps/telnet/gblib.symbols
VIEWER_GBLIB="build/GBLIBVIEWER.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$VIEWER_GBLIB" apps/viewer/gblib.symbols
NOTEPAD_GBLIB="build/GBLIBNOTEPAD.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$NOTEPAD_GBLIB" apps/notepad/gblib.symbols
ICONED_GBLIB="build/GBLIBICONED.s"
python3 tools/gblib_subset.py \
    lib/gb/gblib.s "$ICONED_GBLIB" apps/iconed/gblib.symbols

BUILD_COMMIT="$(git rev-parse --short=12 HEAD 2>/dev/null || printf unknown)"
if ! git diff --quiet --ignore-submodules -- 2>/dev/null \
    || ! git diff --cached --quiet --ignore-submodules -- 2>/dev/null; then
    BUILD_COMMIT="${BUILD_COMMIT}-dirty"
fi
echo "Build id: GB $BUILD_COMMIT"

python3 tools/make_bootsplash.py assets/SPLASH.png build/SPLASH_BUILD.png "$BUILD_COMMIT" GEOBENCH
python3 tools/png2cpc.py build/SPLASH_BUILD.png build/SPLASH.BIN splash 96x184
python3 tools/make_bootsplash.py assets/SPLASH.png build/SPLASHD_BUILD.png "$BUILD_COMMIT"
python3 tools/png2cpc.py build/SPLASHD_BUILD.png build/SPLASHD.BIN splash 96x184  # DEBUG=TRUE bootsplash with build id
# Default GEOBENCH.CFG (#205): one source for BOTH distributions - the card root (stage_dist.sh)
# and the floppy DSK (pack_apps3.asm). CR+LF, as the CPC requires. Without it on the floppy the
# Settings app read all-blank and could not persist a change (the kernel falls back to defaults).
printf 'FONT=DEFAULT\r\nICONS=REFINED\r\nCURSOR=DEFAULT\r\nTITLEBAR=ORIGINAL\r\nGADGETS=ORIGINAL\r\nVIEW=DEFAULT\r\nBACKDROP=SOLID\r\nWALLPAPER=LOGO\r\nSAVER=SQUARES\r\nSAVERTIME=2\r\nSTARFLD_SPEED=4\r\nSTARFLD_STARS=64\r\nXMATRIX_GLYPHS=0\r\nXMATRIX_SPEED=2\r\nXMATRIX_COLOR=18\r\nMOUNTAIN_SPEED=2\r\nMOUNTAIN_PEAKS=15\r\nMOUNTAIN_HOLD=120\r\nPROXY=\r\n' > build/GEOBENCH.CFG
cp build/GEOBENCH.CFG build/DEFAULT.CFG
python3 tools/genfont.py build/DEFAULT.FNT   # 6x8 font -> PAGE_DATA
python3 tools/packfont.py build/CLASSIC.FNT lib/font.asm  # 8x8 ROM font (FONT=CLASSIC)
python3 tools/packicons.py build/DEFAULT.IST \
    lib/icon_floppy.asm lib/icon_clock.asm lib/icon_trash.asm \
    lib/icon_geobench.asm lib/icon_basic.asm lib/icon_binary.asm \
    lib/icon_picture.asm lib/icon_text.asm lib/icon_folder.asm \
    lib/icon_app.asm lib/icon_font.asm \
    lib/icon_desktop.asm lib/icon_filemanager.asm \
    lib/icon_sd.asm \
    lib/icon_up.asm lib/icon_screensaver.asm \
    lib/icon_cf.asm lib/icon_ide.asm lib/icon_fractal.asm \
    lib/icon_settings.asm lib/icon_calculator.asm \
    # slots: 8=folder 9=.APP 10=.FNT 11=DESKTOP 12=FILEMGR
    # 13=SD (Disk C, #104) 14=UP (FileMgr ".." entry, #142)
    # 15=SCREENSAVER (.SAV, #221 reused gear slot)
    # 16=CF 17=IDE 18=FRACTAL 19=SETTINGS 20=CALCULATOR
    # App-owned BASIC/NOTEPAD/ICONED/PAINT/BROWSER/VIEWER/TELNET/MAHJONG/SHELL
    # icons live in GBAP headers and are loaded on demand by File Manager.
    # NOTE (#198): icon_iconset removed - it was byte-identical to icon_app; .IST files
    # now show the .APP icon, shrinking DEFAULT.IST by one slot (the floppy AMSDOS reader
    # garbles the icon set above a size threshold). Later app-owned slots are removed
    # from every tracked set in lockstep.
# PAINT's portable 24x21 toolchest set. The picture document now occupies a
# borrowed page, leaving PAINT's app page free for all thirteen live tools.
python3 tools/packicons.py build/PAINT.IST \
    "$PAINT_ASSET_DIR/pencil.asm" "$PAINT_ASSET_DIR/line.asm" \
    "$PAINT_ASSET_DIR/square.asm" "$PAINT_ASSET_DIR/boxfill.asm" \
    "$PAINT_ASSET_DIR/circle.asm" "$PAINT_ASSET_DIR/circlefill.asm" \
    "$PAINT_ASSET_DIR/bucket.asm" "$PAINT_ASSET_DIR/spray.asm" \
    "$PAINT_ASSET_DIR/select.asm" "$PAINT_ASSET_DIR/cut.asm" \
    "$PAINT_ASSET_DIR/copy.asm" "$PAINT_ASSET_DIR/paste.asm" \
    "$PAINT_ASSET_DIR/undo.asm"
"$RASM" kernel/modules/picedit_low.asm >/dev/null
# Backdrop tiles (#128): stage every assets/backdrops tile as build/<NAME>.BDP - copy any
# ready-made *.BDP, and convert any *.png that has no matching .BDP. Uppercased 8.3 names.
for bdp in assets/backdrops/*.BDP; do
    [ -e "$bdp" ] && cp "$bdp" "build/$(basename "$bdp" | tr 'a-z' 'A-Z')"
done
for png in assets/backdrops/*.png; do
    [ -e "$png" ] || continue
    name="$(basename "$png" .png | tr 'a-z' 'A-Z')"
    [ -e "build/$name.BDP" ] || python3 tools/png2backdrop.py "$png" "build/$name.BDP"
done
python3 tools/png2mahjong.py assets/katakana.png assets/hiragana.png apps/mahjong/kana.h
APP_ICON=apps/telnet/icon.asm GBLIB_SRC="$TELNET_GBLIB" DATA_LOC=0x7300 NET=1 DOC=1 tools/build_capp.sh apps/telnet build/TELNET.RAW # TELNET (#238): 78x22 windowed (4x8 charset, #351) ANSI/VT terminal + telnet client (+ Mode-2 80x25 fullscreen)
DATA_LOC=0x7000 NET=1 tools/build_capp.sh apps/nettest build/NETTEST.RAW # NETTEST (#261): card-side DNS/TCP/HTTP diagnostic for the active network backend
APP_ICON=apps/formref/icon.asm APP_ICON16=apps/formref/icon16.asm DATA_LOC=0x6200 WIDGETS=1 STEPPER=1 SELECTOR=1 ACTIONS=1 FORM=1 FORM_SELECT=1 tools/build_capp.sh apps/formref build/FORMREF.RAW # FORMREF (#420/#424/#426/#428): compact action diagnostic + dual embedded APP icon reference
DATA_LOC=0x6200 BUTTON=1 SOUND=1 tools/build_capp.sh apps/sndtest build/SNDTEST.RAW # SNDTEST (#452): app-linked PSG/beeper diagnostic; zero resident kernel bytes
DATA_LOC=0x7A50 DIALOGS=1 WIDGETS=1 NET=1 tools/build_capp.sh apps/wget build/WGET.RAW # WGET (#363/#367): streaming HTTP downloader with redirects + CPC resume
APP_ICON=apps/browser/icon.asm GBWIN=0 GBLIB_SRC=lib/gb/gblib_browser.s APP_CFLAGS="--max-allocs-per-node 100000" DATA_LOC=0x7E00 NET=1 tools/build_capp.sh apps/browser build/BROWSER.RAW # BROWSER (#367/#371/#373/#476): demand stream + offline/proxy/GET-form/table support
DATA_LOC=0x6200 tools/build_capp.sh apps/brsave build/BRSAVE.RAW # transient Browser .HTM source writer
APP_ICON=apps/shell/icon.asm DATA_LOC=0x6D00 SCROLL=1 tools/build_capp.sh apps/shell build/SHELL.RAW # SHELL (#365): portable command shell with streamed cat/cp
APP_ICON=apps/mahjong/icon.asm DATA_LOC=0x7100 DIALOGS=1 tools/build_capp.sh apps/mahjong build/MAHJONG.RAW # Kana Mahjong: solvable 144-tile Turtle game
DATA_LOC=0x6800 BUTTON=1 tools/build_capp.sh apps/calculator build/CALC.RAW # CALC (#437): compact fixed-point desktop calculator
if [ "$PREEMPTIVE" = "1" ]; then
    TASK_ROOT=1 TASK_RUNTIME_RAW=build/GBSCHED.RAW \
        TASK_STACK_RESERVE=256 DATA_LOC=0x7300 DOC=1 TITLEBAR=1 \
        tools/build_capp.sh apps/desktop build/DESKTOP.RAW
else
    DATA_LOC=0x7100 DOC=1 TITLEBAR=1 tools/build_capp.sh apps/desktop build/DESKTOP.RAW
fi
                                   # DESKTOP (C/SDCC): System
                                   # menu via the shared gb_doc menu system (#142). Higher data-loc
                                   # for the wallpaper config parse (#212/#216), saver trigger (#219),
                                   # and clip-aware wallpaper repaint path.
FILEMGR_APP_PROBE=1
if [ "$PREEMPTIVE" = "1" ]; then FILEMGR_APP_PROBE=0; fi
APP_CFLAGS="--max-allocs-per-node 5000" DATA_LOC=0x7960 DOC=1 SCROLL=1 APP_PROBE="$FILEMGR_APP_PROBE" REPAINTTOP=1 tools/build_capp.sh apps/filemgr build/FILEMGR.RAW # FILEMGR: tight split; app-specific icon names use a compact table
                                   # the gb_doc-grown code + ".." entry; the 128-entry listing cache
                                   # (#118) fits the rest. DOC=1 = View menu (Fullscreen/Icons-List) (#142)
APP_ICON=apps/viewer/icon.asm GBLIB_SRC="$VIEWER_GBLIB" DATA_LOC=0x68B0 DOCRO=1 SCROLL16=1 REPAINTTOP=1 tools/build_capp.sh apps/viewer build/VIEWER.RAW # VIEWER: image-only, read-only
                                   # gb_doc (DOCRO=1 omits Save/Save As); pictures use banked RAM
                                   # when available and demand-stream visible rows otherwise.
                                   # File>Load + View>Fullscreen (#142/#144)
APP_ICON=apps/notepad/icon.asm GBLIB_SRC="$NOTEPAD_GBLIB" APPDEFS="$NOTEPAD_APPDEFS" APP_CFLAGS="$NOTEPAD_CFLAGS" DATA_LOC="$NOTEPAD_DATA_LOC" DOC=1 REPAINTTOP="$NOTEPAD_SCROLL" tools/build_capp.sh apps/notepad build/NOTEPAD.RAW # NOTEPAD: doc framework (#142),
                                   # code-heavy, so a higher data-loc gives it ~1.9K code room
                                   # (#97); shared File popup + name prompt (gbdlg/gbprompt, #114)
APP_ICON=apps/iconed/icon.asm GBLIB_SRC="$ICONED_GBLIB" APPDEFS="-DGBUI_APPICON_PICKER -DGBDOC_BOUNDED_IO" APP_CFLAGS="--max-allocs-per-node 100000" DATA_LOC=0x7000 DOC=1 BUTTON=1 REPAINTTOP=1 tools/build_capp.sh apps/iconed build/ICONED.RAW # ICONED: header-aware .APP picker; document lives in a borrowed app page
                                   # the gb_doc/fullscreen code so the 6656-B icon-set buffer
                                   # (BUFSZ, holds DEFAULT.IST) + 256-B packed grid fit (#110/#142)
DATA_LOC=0x6780 DOC=1 WIDGETS=1 STEPPER=1 FORM=1 TIMESET=1 tools/build_capp.sh apps/clock  build/CLOCK.RAW # CLOCK (C/SDCC): View>Fullscreen + Options
                                   # via the shared gb_doc menu system (#142) -> build/CLOCK.RAW
APP_ICON="$PAINT_APP_DIR/icon.asm" GBLIB_SRC="$PAINT_GBLIB" APP_CFLAGS="--opt-code-size --max-allocs-per-node 100000" HELPER_CFLAGS="--opt-code-size --max-allocs-per-node 100000" DATA_LOC=0x7D00 PICKER=1 SIZEPROMPT=1 GBWIN=0 tools/build_capp.sh "$PAINT_APP_DIR" build/PAINT.RAW # PAINT: three app-owned panes + banked 20x20 editor
                                   # + name prompt (gbdlg.c + gbprompt.c) for its File menu (#114)
if [ "$PREEMPTIVE" = "1" ]; then
    TASK=1 TASK_STACK_RESERVE=256 APP_ICON=apps/xaos/icon.asm DATA_LOC=0x6600 DOC=1 BUTTON=1 \
        tools/build_capp.sh apps/xaos build/XAOS.RAW
else
    APP_ICON=apps/xaos/icon.asm DATA_LOC=0x6400 DOC=1 BUTTON=1 \
        tools/build_capp.sh apps/xaos build/XAOS.RAW
fi                                                                 # XAOS fractal generator:
                                   # File>Save dialog (gbdlg + gbprompt) -> .PIC (#116)
APP_CFLAGS="--opt-code-size --max-allocs-per-node 100000" DATA_LOC=0x7C40 DIALOGS=1 STEPPER=1 SELECTOR=1 ACTIONS=1 TITLEBAR=1 tools/build_capp.sh apps/settings build/SETTINGS.RAW # SETTINGS (#129): the control
                                   # panel - pick FONT=/ICONS=/CURSOR= from /GBENCH (gb_popup),
                                   # rewrite GEOBENCH.CFG; data-driven rows grow with colours/etc.
DIALOGS=1 BUTTON=1 tools/build_capp.sh apps/diskutil build/DISKUTIL.RAW # DISKUTIL: floppy formatter - a physical
                                   # uPD765 FORMAT TRACK straight to the FDC (Data/System/exotic 80-trk DS);
                                   # gb_popup confirm. Reuses the floppy icon (DEFAULT.IST slot 0).
tools/build_capp.sh apps/saver build/SQUARES.RAW  # SAVER (#219/#281): random squares - a
                                   # full-screen blank + squares, shipped as SQUARES.SAV. Launched by
                                   # the desktop idle timer (SAVER=<seconds>); no menu/doc framework.
tools/build_capp.sh apps/deco  build/DECO.RAW     # DECO screensaver (ported from symsav-deco):
                                   # recursive rectangle subdivision -> art-deco panels. -> DECO.SAV
tools/build_capp.sh apps/xmatrix build/XMATRIX.RAW # XMATRIX screensaver (ported from symsav-xmatrix):
                                   # configurable binary/Kana rain on black, with a selectable trail. -> XMATRIX.SAV
tools/build_savercfg.sh apps/xmatrix build/XMATRIXCFG.RAW # same-stem XMATRIX.MOD Configure UI
tools/build_capp.sh apps/mountain build/MOUNTAIN.RAW # MOUNTAIN screensaver (ported from symsav-mountain):
                                   # isometric filled terrain + white wireframe, direct #C000 plot. -> MOUNTAIN.SAV
tools/build_savercfg.sh apps/mountain build/MOUNTAINCFG.RAW # same-stem MOUNTAIN.MOD Configure UI
tools/build_capp.sh apps/fractalic build/FRACTALI.RAW # FRACTALIC screensaver (ported from symsav-fractalic):
                                   # random fractal (Sierpinski/Koch/Dragon/Fern), direct #C000 plot.
                                   # CARD-ONLY (too big for the floppy) -> FRACTALI.SAV via stage_dist.sh
tools/build_capp.sh apps/starfield build/STARFLD.RAW # STARFIELD screensaver (fresh impl, inspired by
                                   # symsav-starfield): 3D stars flying toward the viewer, direct #C000 plot.
tools/build_savercfg.sh apps/starfield build/STARFLDCFG.RAW # same-stem STARFLD.MOD Configure UI
tools/build_capp.sh apps/xroach build/XROACH.RAW  # XROACH screensaver (ported from symsav-xroach):
                                   # 16x16 cockroaches scatter + flee a wandering "odd roach", direct
                                   # #C000 blit. CARD-ONLY (floppy pack full) -> XROACH.SAV via stage_dist.sh
tools/build_capp.sh apps/munch build/MUNCH.RAW    # MUNCH screensaver (xscreensaver port): munching
                                   # squares XOR moire, direct #C000. CARD-ONLY -> MUNCH.SAV
tools/build_capp.sh apps/rorschach build/RORSCH.RAW # RORSCHACH (xscreensaver port): 4-fold-symmetric
                                   # random-walk ink-blots, direct #C000. CARD-ONLY -> RORSCH.SAV
tools/build_capp.sh apps/truchet build/TRUCHET.RAW # TRUCHET (xscreensaver port): random diagonal-tile
                                   # maze, direct #C000 lines. CARD-ONLY -> TRUCHET.SAV
tools/build_capp.sh apps/ant build/ANT.RAW        # ANT (xscreensaver port): Langton's ant on an 80x50
                                   # grid, gb_fill cells. CARD-ONLY -> ANT.SAV
tools/build_capp.sh apps/lightning build/LIGHTN.RAW # LIGHTNING (xscreensaver port): midpoint-displacement
                                   # forked bolts, direct #C000 lines. CARD-ONLY -> LIGHTN.SAV
tools/build_capp.sh apps/pyro build/PYRO.RAW      # PYRO (xscreensaver port): fixed-point fireworks
                                   # rockets + shrapnel, direct #C000. CARD-ONLY -> PYRO.SAV
tools/build_capp.sh apps/forest build/FOREST.RAW  # FOREST (xscreensaver port): recursive fractal trees
                                   # with red blossoms, direct #C000 lines. CARD/EXTRAS -> FOREST.SAV
tools/build_capp.sh apps/helix build/HELIX.RAW    # HELIX (xscreensaver port): woven harmonograph curves
                                   # (sin-table), direct #C000 lines. CARD-ONLY -> HELIX.SAV
DATA_LOC=0x6700 tools/build_capp.sh apps/catclock build/CATCLK.RAW # CATCLOCK (inspired by X11 catclock):
                                   # Kit-Cat clock - embedded body bitmap (catimg.h, from png2catclock.py) +
                                   # moving pupils + real hour/minute hands (gb_time). CARD/EXTRAS -> CATCLK.SAV
tools/build_cfgmod.sh build/GBCFG.RAW              # config-parser C kernel module -> build/GBCFG.RAW
tools/build_fatmod.sh                              # FAT16/IDE write module -> build/GBFAT.RAW
tools/build_floppymod.sh                           # AMSDOS/floppy write module -> build/FLOPPYSV.RAW
tools/build_uimod.sh build/GBUI.RAW                # paged dialog module (#142) -> build/GBUI.RAW
tools/build_appickmod.sh build/GBAPICK.RAW         # ICONED header-aware .APP picker (#426)
tools/build_webmod.sh build/GBWEB.RAW              # Browser source/config helper (#373)
tools/build_imgmod.sh build/GBIMG.RAW              # Browser inline-image cache helper (#393)
tools/build_doxmod.sh build/GBDOX.RAW              # Browser bounded DOX/PIC decoder (#487)
tools/build_netmod.sh build/GBNET.RAW             # W5100 networking module (#238) -> build/GBNET.RAW
tools/build_m4netmod.sh build/GBNETM4.RAW         # M4 TCP networking module (#259) -> build/GBNETM4.RAW
tools/build_m4savemod.sh                          # M4 file save module (#259) -> build/M4SAVE.RAW
# Card distribution: the apps/modules/assets above are shared; we assemble the Albireo
# and M4 kernels, capture their raw images, and stage QA/CPC/CARD/ holding the BASIC loader
# GB.BAS + both kernels (GBALB.BIN, GBM4.BIN). Plus a bootable floppy image
# QA/CPC/Floppies/GEOBENCH.DSK using the Albireo kernel (it falls back to floppy when no card is
# present). The IDE backend remains archived (frozen in-tree, not shipped).
build_variant() {                                # $1 = kernel name, $2 = rasm -D flag
    rm -f build/gbkern.dsk                       # save-to-DSK appends; start clean
    "$RASM" kernel/gbkern.asm -eo $2 ${EXTRA_RASM:-} $TITLEBAR_RASM # incbins apps + font + icons -> .dsk + RAW
    "$RASM" kernel/pack_modules.asm -eo $TITLEBAR_RASM # paged modules that no longer fit gbkern.asm
    "$RASM" kernel/pack_apps.asm -eo             # 2nd pass: overflow apps -> same .dsk (#114)
    "$RASM" kernel/pack_apps2.asm -eo            # 3rd pass: VIEWER + FILEMGR -> same .dsk (#142)
    "$RASM" kernel/pack_apps3.asm -eo $TITLEBAR_RASM # 4th pass: visual assets -> same .dsk
    cp build/GBKERN.RAW "build/$1.RAW"           # capture this card's kernel for the unified stage
}
# Clean only the CPC outputs - QA/MSX (the MSX2 target, #287) survives a CPC build.
# Remove the old root-level layout too, so stale artifacts cannot mask a bad migration.
rm -rf "$CARD_QA" "$FLOPPY_QA" "$CARD_IMG" \
    QA/CARD QA/GEOBENCH.DSK QA/COMPANION.DSK QA/MEDIA.DSK QA/EXTRAS.DSK QA/GEOBENCH.IMG
mkdir -p "$FLOPPY_QA"
# Build both card kernels. GEOBENCH.DSK keeps the Albireo/floppy-capable kernel
# because that is the normal floppy boot image; CARD and GEOBENCH.IMG carry both.
echo "Building the Albireo (GBALB) and M4 (GBM4) card kernels + the shared card -> $CPC_QA/"
build_variant GBALB "-DSTORAGE_ALBIREO=1"
cp build/gbkern.dsk "$FLOPPY_QA/GEOBENCH.DSK"     # bootable floppy image (the GBALB kernel)
build_variant GBM4 "-DSTORAGE_M4=1"
# Add a GB.BAS loader so the floppy also boots via RUN"GB (-> RUN"GBKERN). Must be a
# HEADERLESS ASCII file - RASM's DSK save adds an AMSDOS header, so use iDSK (-t 0).
# Graceful: without iDSK the floppy still boots via RUN"GBKERN.
IDSK="${IDSK:-$HOME/Dev/cpc-mastering/idsk}"
if [ -x "$IDSK" ]; then
    printf '10 RUN"GBKERN\r\n' > build/GB.BAS
    "$IDSK" "$FLOPPY_QA/GEOBENCH.DSK" -i build/GB.BAS -t 0 >/dev/null 2>&1 \
        && echo "  + GB.BAS on $FLOPPY_QA/GEOBENCH.DSK (floppy RUN\"GB)" \
        || echo "  (iDSK present but GB.BAS insert failed - floppy still RUN\"GBKERN)"
else
    echo "  (no iDSK at \$IDSK - floppy boots via RUN\"GBKERN; set IDSK= to add the GB.BAS loader)"
fi
# Companion floppy QA/CPC/Floppies/COMPANION.DSK (#250): a non-bootable DATA
# disk with the extra applications and screensavers.
RASM="$RASM" tools/package_cpc_companion.sh "$FLOPPY_QA/COMPANION.DSK"
EXTRAS_ADDS=(
    --add assets/WELCOME.TXT
    --add build/DISKUTIL.RAW=DISKUTIL.APP
    --add build/XROACH.RAW=XROACH.SAV
    --add build/CATCLK.RAW=CATCLK.SAV
    --add build/HELIX.RAW=HELIX.SAV
    --add build/FOREST.RAW=FOREST.SAV
    --add build/MOUNTAIN.RAW=MOUNTAIN.SAV
    --add build/MOUNTAINCFG.RAW=MOUNTAIN.MOD
)
for tbr in build/titlebars/*.TBR; do
    case "$(basename "$tbr")" in
        ORIGINAL.TBR) continue ;;
    esac
    EXTRAS_ADDS+=(--add "$tbr")
done
for gdt in build/gadgets/*.GDT; do
    case "$(basename "$gdt")" in
        ORIGINAL.GDT) continue ;;
    esac
    EXTRAS_ADDS+=(--add "$gdt")
done
while IFS= read -r pic; do
    EXTRAS_ADDS+=(--add "$pic")
done < <(python3 tools/picture_catalog.py portable)
python3 tools/mkcpcmedia.py "$FLOPPY_QA/EXTRAS.DSK" "${EXTRAS_ADDS[@]}"
echo "  + $FLOPPY_QA/EXTRAS.DSK (picture gallery + secondary title/gadget themes + Disk Utility + XROACH/CATCLK/HELIX/FOREST/MOUNTAIN savers + WELCOME.TXT; extended 80-track AMSDOS data disk)"
echo "Building GB-BASIC CPC payload from $GB_BASIC_DIR"
mkdir -p "$GB_BASIC_DIR/build" "$GB_BASIC_DIR/build/basic"
make -C "$GB_BASIC_DIR" raws GEOBENCH="$GEOBENCH_ROOT"
GB_BASIC_DIR="$GB_BASIC_DIR" tools/stage_dist.sh "$CARD_QA" # GB.BAS auto-detect + GBALB.BIN + GBM4.BIN + /GBENCH
# A ready-to-flash card image (partitioned FAT16) for the Albireo CH376 card and M4 image mode.
tools/build_card_img.sh "$CARD_QA" "$CARD_IMG" \
    || echo "  ($CARD_IMG skipped - needs sfdisk + mkfs.fat + mtools)"
echo "  $CARD_QA: loose files; $CARD_IMG: Albireo/M4 card; $FLOPPY_QA: floppy set"

# Leave build/ as the STORAGE-selected variant (default Albireo) so the --disk-a
# test harness sees a predictable build/gbkern.dsk + build/GBKERN.RAW.
rm -f build/gbkern.dsk
"$RASM" kernel/gbkern.asm -eo $STORAGE_FLAG ${FAT16_FLAG:+$FAT16_FLAG} ${EXTRA_RASM:-} $TITLEBAR_RASM >/dev/null
"$RASM" kernel/pack_modules.asm -eo $TITLEBAR_RASM >/dev/null  # paged modules that no longer fit gbkern.asm
"$RASM" kernel/pack_apps.asm -eo >/dev/null      # 2nd pass: overflow apps -> .dsk (#114)
"$RASM" kernel/pack_apps2.asm -eo >/dev/null     # 3rd pass: VIEWER + FILEMGR -> .dsk (#142)
"$RASM" kernel/pack_apps3.asm -eo $TITLEBAR_RASM >/dev/null     # 4th pass: visual assets
if [ -x "$IDSK" ]; then
    "$IDSK" build/gbkern.dsk -i build/GB.BAS -t 0 >/dev/null 2>&1 || true
fi
echo "Built $CARD_QA + $CARD_IMG (Albireo/M4 card deploy) + $FLOPPY_QA; build/ = ${STORAGE:-albireo} variant"
