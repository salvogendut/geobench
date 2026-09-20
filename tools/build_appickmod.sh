#!/usr/bin/env bash
# Build GBAPICK.MOD, ICONED's paged header-aware Open dialog.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-build/GBAPICK.RAW}"
APPDEFS="${APPDEFS:-} ${GLOBAL_APPDEFS:-}"
GB="lib/gb"
SRC="kernel/kc/gbappick_mod.c"
SDCC="${SDCC:-sdcc}"
BIN="$(dirname "$(command -v "$SDCC")")"
SDAS="$BIN/sdasz80"
MAKEBIN="$BIN/makebin"
work="build/appickmod"
mkdir -p "$work" "$(dirname "$OUT")"
. tools/build_cache.sh

deps=("$0" "tools/build_cache.sh" "$GB/crt0.s" "$GB/gblib.s" "$GB/gb.h" \
      "$GB/gbdlg.c" "$GB/gbappick.c" "$GB/gbapprobe.s" "$SRC")
stamp="$OUT.stamp"
cache_key=$(printf '%s\n' "build_appickmod.v1" "SDCC=$SDCC" \
    "SDAS=$SDAS" "MAKEBIN=$MAKEBIN" "APPDEFS=$APPDEFS")
if ! gb_needs_rebuild "$OUT" "$stamp" "$cache_key" "${deps[@]}"; then
    echo "Up to date $OUT ($(stat -c%s "$OUT") bytes)"
    exit 0
fi

"$SDAS" -o "$work/crt0.rel"  "$GB/crt0.s"
"$SDAS" -o "$work/gblib.rel" "$GB/gblib.s"
PROBE_REL=""
case " $APPDEFS " in
    *" -DGB_MSX2 "*|*" -DGB_PCW "*) ;;
    *)
        "$SDAS" -o "$work/gbapprobe.rel" "$GB/gbapprobe.s"
        PROBE_REL="$work/gbapprobe.rel"
        ;;
esac
# APPDEFS contains target flags such as -DGB_MSX2 or -DGB_PCW.
# shellcheck disable=SC2086
"$SDCC" -mz80 --opt-code-size --fomit-frame-pointer -I "$GB" \
    $APPDEFS -c "$SRC" -o "$work/gbappick_mod.rel"
# shellcheck disable=SC2086
"$SDCC" -mz80 --opt-code-size --fomit-frame-pointer -I "$GB" \
    $APPDEFS -c "$GB/gbdlg.c" -o "$work/gbdlg.rel"
# shellcheck disable=SC2086
"$SDCC" -mz80 --opt-code-size --fomit-frame-pointer -I "$GB" \
    $APPDEFS -c "$GB/gbappick.c" -o "$work/gbappick.rel"
"$SDCC" -mz80 --no-std-crt0 --code-loc 0x6000 --data-loc 0x7600 \
    "$work/crt0.rel" "$work/gbappick_mod.rel" "$work/gbdlg.rel" \
    "$work/gbappick.rel" $PROBE_REL "$work/gblib.rel" -o "$work/mod.ihx"

python3 - "$work/mod.map" <<'PY'
import re
import sys

area = {}
for line in open(sys.argv[1]):
    match = re.match(
        r'^(_CODE|_DATA|_BSS|_INITIALIZED|_GSINIT|_GSFINAL|_INITIALIZER)'
        r'\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})',
        line,
    )
    if match:
        area[match.group(1)] = (int(match.group(2), 16), int(match.group(3), 16))
load = ("_CODE", "_GSINIT", "_GSFINAL", "_INITIALIZER")
image_end = max(area[name][0] + area[name][1] for name in load if name in area)
top = max(start + size for start, size in area.values())
if image_end > 0x7600 or top > 0x8000:
    raise SystemExit(
        "FIT ERROR (GBAPICK): image=0x%04X top=0x%04X" % (image_end, top)
    )
PY

"$MAKEBIN" -p "$work/mod.ihx" "$work/mod.bin"
tail -c +24577 "$work/mod.bin" > "$OUT"
gb_write_stamp "$stamp" "$cache_key"
echo "Built $OUT ($(stat -c%s "$OUT") bytes)"
