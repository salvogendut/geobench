#!/usr/bin/env bash
# Fast cross-platform rebuild/staging for explicitly registered applications.
set -euo pipefail
cd "$(dirname "$0")/.."

APP=$(printf '%s' "${1:-}" | tr '[:upper:]' '[:lower:]')
case "$APP" in
    mahjong|mahjong.app)
        APP=mahjong
        BASE=MAHJONG
        APP_ICON="apps/mahjong/icon.asm"
        APP_CFLAGS=
        DATA_LOC=0x72C0
        DIALOGS=1
        BUTTON=0
        ;;
    calc|calc.app|calculator)
        APP=calculator
        BASE=CALC
        APP_ICON=
        APP_CFLAGS=
        DATA_LOC=0x6800
        DIALOGS=0
        BUTTON=1
        ;;
    *)
        echo "ERROR: unsupported fast app '${1:-}' (supported: mahjong, calculator)" >&2
        exit 2
        ;;
esac

APP_ICON="$APP_ICON" APP_CFLAGS="$APP_CFLAGS" DATA_LOC="$DATA_LOC" DIALOGS="$DIALOGS" BUTTON="$BUTTON" \
    tools/build_capp.sh "apps/$APP" "build/$BASE.RAW"
APP_ICON="$APP_ICON" APPDEFS="-DGB_MSX2" APP_CFLAGS="$APP_CFLAGS" DATA_LOC="$DATA_LOC" DIALOGS="$DIALOGS" BUTTON="$BUTTON" \
    tools/build_capp.sh "apps/$APP" "build/msx/$BASE.RAW"
APP_ICON="$APP_ICON" APPDEFS="-DGB_PCW" APP_CFLAGS="$APP_CFLAGS" DATA_LOC="$DATA_LOC" DIALOGS="$DIALOGS" BUTTON="$BUTTON" \
    tools/build_capp.sh "apps/$APP" "build/pcw/$BASE.RAW"

for dir in QA/CPC/CARD/GBENCH QA/MSX/CARD/GBENCH QA/CPC/Floppies QA/PCW/Floppies; do
    if [ ! -d "$dir" ]; then
        echo "ERROR: missing $dir; run the full build once before using make app" >&2
        exit 1
    fi
done

cp "build/$BASE.RAW" "QA/CPC/CARD/GBENCH/$BASE.APP"
tools/package_cpc_companion.sh QA/CPC/Floppies/COMPANION.DSK

cp "build/msx/$BASE.RAW" "QA/MSX/CARD/GBENCH/$BASE.APP"
tools/package_pcw_companion.sh QA/PCW/Floppies/COMPANION.DSK

patch_fat_image() {
    local image=$1
    local source=$2
    local destination=$3

    if [ ! -f "$image" ]; then
        echo "  ($image absent; loose staging updated)"
        return
    fi
    if ! command -v mcopy >/dev/null; then
        echo "  ($image not updated; install mtools or run the full build)"
        return
    fi
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$image@@16384" "$source" "::$destination"
    echo "  + $destination in $image"
}

patch_fat_image QA/CPC/GEOBENCH.IMG "build/$BASE.RAW" "/GBENCH/$BASE.APP"
patch_fat_image QA/MSX/GBMSX.IMG "build/msx/$BASE.RAW" "/GBENCH/$BASE.APP"

echo "Fast rebuild complete: $BASE.APP (CPC, MSX, PCW)"
