#!/usr/bin/env bash
# Build a GEOBENCH C app with SDCC into a raw #4000 image - the same format as
# the RASM .RAW app binaries, so the kernel can incbin/package it identically.
#
# The C app is just apps/<name>/main.c; it reaches the kernel through the shared
# libgb (lib/gb/gblib.s + gb.h) and shared crt0 (lib/gb/crt0.s, the #4000 entry).
# Linked: crt0 FIRST (so _start is at #4000), then main, then the libgb trampolines.
# APP_ICON=<canonical 32x32 icon.asm> reserves a GBAP v1 preamble. Adding
# APP_ICON16=<native Screen-7 icon.asm> emits a GBAP v2 dual-icon preamble.
# Its JP keeps the kernel's #4000 entry ABI unchanged in either format.
#
#   tools/build_capp.sh [app_dir] [out.RAW]
#   tools/build_capp.sh apps/chello build/CHELLO.RAW   (defaults)
set -euo pipefail
cd "$(dirname "$0")/.."

APP="${1:-apps/clock}"
OUT="${2:-build/CLOCK.RAW}"
GB="lib/gb"                                 # shared libgb (gb.h, gblib.s, crt0.s)
GBLIB_SRC="${GBLIB_SRC:-$GB/gblib.s}"
APP_CFLAGS="${APP_CFLAGS:-}"
HELPER_CFLAGS="${HELPER_CFLAGS:-}"
GLOBAL_APPDEFS="${GLOBAL_APPDEFS:-}"
ALL_APPDEFS="$GLOBAL_APPDEFS ${APPDEFS:-}"
APP_ICON="${APP_ICON:-}"
APP_ICON16="${APP_ICON16:-}"
LOAD_LIMIT="${LOAD_LIMIT:-0x7F00}"
TASK_STACK_RESERVE="${TASK_STACK_RESERVE:-0}"
TASK_FLAG="${TASK:-0}"
TASK_ROOT_FLAG="${TASK_ROOT:-0}"
TASK_RUNTIME_RAW="${TASK_RUNTIME_RAW:-}"
# DATA_LOC: where this app's data starts (code is #4000.. below it, data ..#7FFF
# above). The default 0x6200 is a 50/50 split; a code-heavy/data-light app (NOTEPAD)
# can pass a higher value to trade its spare data room for code room. Per-app so a
# data-heavy app (VIEWER) keeps the low split. (#97)
DATA_LOC="${DATA_LOC:-0x6200}"

SDCC="${SDCC:-sdcc}"
BIN="$(dirname "$(command -v "$SDCC")")"   # sdasz80 / makebin sit beside sdcc
SDAS="$BIN/sdasz80"
MAKEBIN="$BIN/makebin"
CODE_LOC="0x4000"
# Adding icon16.asm beside an app-owned icon.asm automatically upgrades only
# the MSX build to a dual-resource GBAP v2 header. CPC and PCW retain v1.
case " $ALL_APPDEFS " in
    *" -DGB_MSX2 "*)
        if [ -n "$APP_ICON" ] && [ -z "$APP_ICON16" ]; then
            icon16_candidate="$(dirname "$APP_ICON")/icon16.asm"
            [ ! -f "$icon16_candidate" ] || APP_ICON16="$icon16_candidate"
        fi
        ;;
esac
if [ -n "$APP_ICON16" ] && [ -z "$APP_ICON" ]; then
    echo "ERROR: APP_ICON16 requires the portable APP_ICON fallback" >&2
    exit 1
fi
if [ -n "$APP_ICON" ]; then
    icon_args=("$APP_ICON")
    if [ -n "$APP_ICON16" ]; then icon_args+=("$APP_ICON16"); fi
    APP_PREAMBLE_SIZE=$(python3 tools/embed_app_icon.py size "${icon_args[@]}")
    CODE_LOC=$(printf '0x%X' $((0x4000 + APP_PREAMBLE_SIZE)))
fi

# Keep target-specific object files apart. CPC, MSX and PCW builds may run close
# together (or concurrently outside the top-level Makefile); sharing main.rel
# allowed one target to link another target's conditional drawing code. That is
# fatal on MSX for CPC apps that write directly to #C000.
case " $ALL_APPDEFS " in
    *" -DGB_MSX2 "*) work="build/msx-obj/$(basename "$APP")" ;;
    *" -DGB_PCW "*)  work="build/pcw-obj/$(basename "$APP")" ;;
    *)                work="build/$(basename "$APP")" ;;
esac
mkdir -p "$work"
mkdir -p "$(dirname "$OUT")"
. tools/build_cache.sh

DIALOGS_FLAG="${DIALOGS:-0}"
PROMPT_FLAG="${PROMPT:-0}"
PICKER_FLAG="${PICKER:-0}"
DOC_FLAG="${DOC:-0}"
DOCRO_FLAG="${DOCRO:-0}"
NET_FLAG="${NET:-0}"
GBWIN_FLAG="${GBWIN:-1}"
GBWIN_DRAG_ONLY_FLAG="${GBWIN_DRAG_ONLY:-0}"
WIDGETS_FLAG="${WIDGETS:-0}"
BUTTON_FLAG="${BUTTON:-0}"
ACTIONS_FLAG="${ACTIONS:-0}"
SCROLL_FLAG="${SCROLL:-0}"
SCROLL16_FLAG="${SCROLL16:-0}"
TOGGLE_FLAG="${TOGGLE:-0}"
STEPPER_FLAG="${STEPPER:-0}"
SELECTOR_FLAG="${SELECTOR:-0}"
SLIDER_FLAG="${SLIDER:-0}"
FORM_FLAG="${FORM:-0}"
FORM_SELECT_FLAG="${FORM_SELECT:-0}"
TIMESET_FLAG="${TIMESET:-0}"
SOUND_FLAG="${SOUND:-0}"
TITLEBAR_FLAG="${TITLEBAR:-0}"
SIZEPROMPT_FLAG="${SIZEPROMPT:-0}"
APP_PROBE_FLAG="${APP_PROBE:-0}"
REPAINTTOP_FLAG="${REPAINTTOP:-0}"
NET_SRC="$GB/gbnet_stub.c"
case " $ALL_APPDEFS " in
    *" -DGB_MSX2 "*) NET_SRC="$GB/gbnet_unapi_stub.c" ;;
esac

if [ "$FORM_FLAG" = "1" ] && [ "$WIDGETS_FLAG" != "1" ]; then
    echo "ERROR: FORM=1 requires WIDGETS=1" >&2
    exit 1
fi
if [ "$GBWIN_DRAG_ONLY_FLAG" = "1" ] && [ "$GBWIN_FLAG" != "1" ]; then
    echo "ERROR: GBWIN_DRAG_ONLY=1 requires GBWIN=1" >&2
    exit 1
fi
if [ "$FORM_SELECT_FLAG" = "1" ] &&
   { [ "$FORM_FLAG" != "1" ] || [ "$SELECTOR_FLAG" != "1" ]; }; then
    echo "ERROR: FORM_SELECT=1 requires FORM=1 and SELECTOR=1" >&2
    exit 1
fi
if [ "$TASK_FLAG" = "1" ] && (( TASK_STACK_RESERVE < 256 )); then
    echo "ERROR: TASK=1 requires TASK_STACK_RESERVE=256 (or larger)" >&2
    exit 1
fi
if [ "$TASK_ROOT_FLAG" = "1" ] && (( TASK_STACK_RESERVE < 256 )); then
    echo "ERROR: TASK_ROOT=1 requires TASK_STACK_RESERVE=256 (or larger)" >&2
    exit 1
fi
if [ "$TASK_FLAG" = "1" ] && [ "$TASK_ROOT_FLAG" = "1" ]; then
    echo "ERROR: TASK and TASK_ROOT are mutually exclusive" >&2
    exit 1
fi
if [ "$TASK_ROOT_FLAG" = "1" ] && [ ! -s "$TASK_RUNTIME_RAW" ]; then
    echo "ERROR: TASK_ROOT=1 requires a non-empty TASK_RUNTIME_RAW" >&2
    exit 1
fi

deps=("$0" "tools/build_cache.sh" "tools/check_app_layout.py" "$GB/crt0.s" "$GBLIB_SRC" "$GB/gb.h")
if [ -n "$APP_ICON" ]; then
    deps+=("tools/embed_app_icon.py" "$APP_ICON")
fi
if [ -n "$APP_ICON16" ]; then
    deps+=("$APP_ICON16")
fi
if [ "$GBWIN_FLAG" = "1" ]; then
    deps+=("$GB/gbwin.c")
fi
if [ "$WIDGETS_FLAG" = "1" ] || [ "$BUTTON_FLAG" = "1" ]; then
    deps+=("$GB/gbwidgets.c")
fi
if [ "$ACTIONS_FLAG" = "1" ]; then
    deps+=("$GB/gbactions.c")
fi
if [ "$SCROLL_FLAG" = "1" ]; then
    deps+=("$GB/gbscroll.c")
fi
if [ "$SCROLL16_FLAG" = "1" ]; then
    deps+=("$GB/gbscroll16.c")
fi
if [ "$TOGGLE_FLAG" = "1" ]; then
    deps+=("$GB/gbtoggle.c")
fi
if [ "$STEPPER_FLAG" = "1" ]; then
    deps+=("$GB/gbstepper.c")
fi
if [ "$SELECTOR_FLAG" = "1" ]; then
    deps+=("$GB/gbselect.c")
fi
if [ "$SLIDER_FLAG" = "1" ]; then
    deps+=("$GB/gbslider.c")
fi
if [ "$FORM_FLAG" = "1" ]; then
    deps+=("$GB/gbform.c")
fi
if [ "$FORM_SELECT_FLAG" = "1" ]; then
    deps+=("$GB/gbform_select.c")
fi
if [ "$TIMESET_FLAG" = "1" ]; then
    deps+=("$GB/gbsettime.c")
fi
if [ "$SOUND_FLAG" = "1" ]; then
    deps+=("$GB/gbsound.c")
fi
if [ "$SIZEPROMPT_FLAG" = "1" ]; then
    deps+=("$GB/gbsizedlg.c")
fi
if [ "$APP_PROBE_FLAG" = "1" ]; then
    deps+=("$GB/gbapprobe.s")
fi
if [ "$REPAINTTOP_FLAG" = "1" ]; then
    deps+=("$GB/gbrepaint.s")
fi
if [ "$TASK_FLAG" = "1" ]; then
    deps+=("$GB/gbtask.s")
fi
if [ "$TASK_ROOT_FLAG" = "1" ]; then
    deps+=("tools/embed_scheduler.py" "$TASK_RUNTIME_RAW")
fi
if [ "$TITLEBAR_FLAG" = "1" ]; then
    deps+=("$GB/gbtitle.c" "$GB/gbtitle.h")
fi
while IFS= read -r dep; do
    deps+=("$dep")
done < <(find "$APP" -type f | sort)
if grep -Rqs 'gbhttp\.h' "$APP"; then
    deps+=("$GB/gbhttp.h")
fi
if grep -Rqs 'gbhtml\.h' "$APP"; then
    deps+=("$GB/gbhtml.h")
fi
if grep -Rqs 'gbbrowser\.h' "$APP"; then
    # Browser's shared low-RAM ABI is not part of gb.h. Missing this dependency
    # previously left MSX/PCW with stale binaries after a protocol-state change.
    deps+=("$GB/gbbrowser.h")
fi
if [ "$DIALOGS_FLAG" = "1" ] || [ "$PROMPT_FLAG" = "1" ] || [ "$PICKER_FLAG" = "1" ] || [ "$DOC_FLAG" = "1" ] || [ "$DOCRO_FLAG" = "1" ]; then
    deps+=("$GB/gbui_stub.c")
fi
if [ "$DOC_FLAG" = "1" ] || [ "$DOCRO_FLAG" = "1" ]; then
    deps+=("$GB/gbdoc.c")
fi
if [ "$NET_FLAG" = "1" ]; then
    deps+=("$NET_SRC")
fi

stamp="$OUT.stamp"
cache_key=$(printf '%s\n' \
    "build_capp.v2" \
    "APP=$APP" \
    "APP_ICON=$APP_ICON" \
    "APP_ICON16=$APP_ICON16" \
    "CODE_LOC=$CODE_LOC" \
    "DATA_LOC=$DATA_LOC" \
    "APPDEFS=$ALL_APPDEFS" \
    "DIALOGS=$DIALOGS_FLAG" \
    "PROMPT=$PROMPT_FLAG" \
    "PICKER=$PICKER_FLAG" \
    "DOC=$DOC_FLAG" \
    "DOCRO=$DOCRO_FLAG" \
    "NET=$NET_FLAG" \
    "NET_SRC=$NET_SRC" \
    "GBWIN=$GBWIN_FLAG" \
    "GBWIN_DRAG_ONLY=$GBWIN_DRAG_ONLY_FLAG" \
    "WIDGETS=$WIDGETS_FLAG" \
    "BUTTON=$BUTTON_FLAG" \
    "ACTIONS=$ACTIONS_FLAG" \
    "SCROLL=$SCROLL_FLAG" \
    "SCROLL16=$SCROLL16_FLAG" \
    "TOGGLE=$TOGGLE_FLAG" \
    "STEPPER=$STEPPER_FLAG" \
    "SELECTOR=$SELECTOR_FLAG" \
    "SLIDER=$SLIDER_FLAG" \
    "FORM=$FORM_FLAG" \
    "FORM_SELECT=$FORM_SELECT_FLAG" \
    "TIMESET=$TIMESET_FLAG" \
    "SOUND=$SOUND_FLAG" \
    "TITLEBAR=$TITLEBAR_FLAG" \
    "SIZEPROMPT=$SIZEPROMPT_FLAG" \
    "APP_PROBE=$APP_PROBE_FLAG" \
    "REPAINTTOP=$REPAINTTOP_FLAG" \
    "TASK=$TASK_FLAG" \
    "TASK_ROOT=$TASK_ROOT_FLAG" \
    "TASK_RUNTIME_RAW=$TASK_RUNTIME_RAW" \
    "GBLIB_SRC=$GBLIB_SRC" \
    "APP_CFLAGS=$APP_CFLAGS" \
    "HELPER_CFLAGS=$HELPER_CFLAGS" \
    "LOAD_LIMIT=$LOAD_LIMIT" \
    "TASK_STACK_RESERVE=$TASK_STACK_RESERVE" \
    "SDCC=$SDCC" \
    "SDAS=$SDAS" \
    "MAKEBIN=$MAKEBIN")
if ! gb_needs_rebuild "$OUT" "$stamp" "$cache_key" "${deps[@]}"; then
    echo "Up to date $OUT ($(stat -c%s "$OUT") bytes) from $APP"
    exit 0
fi

"$SDAS" -o "$work/crt0.rel"  "$GB/crt0.s"
"$SDAS" -o "$work/gblib.rel" "$GBLIB_SRC"
APP_PROBE_REL=""
if [ "$APP_PROBE_FLAG" = "1" ]; then
    case " $ALL_APPDEFS " in
        *" -DGB_MSX2 "*|*" -DGB_PCW "*)
            echo "ERROR: APP_PROBE is the CPC-only whole-APP preamble reader" >&2
            exit 1
            ;;
    esac
    "$SDAS" -o "$work/gbapprobe.rel" "$GB/gbapprobe.s"
    APP_PROBE_REL="$work/gbapprobe.rel"
fi
REPAINTTOP_REL=""
if [ "$REPAINTTOP_FLAG" = "1" ]; then
    "$SDAS" -o "$work/gbrepaint.rel" "$GB/gbrepaint.s"
    REPAINTTOP_REL="$work/gbrepaint.rel"
fi
TASK_REL=""
if [ "$TASK_FLAG" = "1" ]; then
    "$SDAS" -o "$work/gbtask.rel" "$GB/gbtask.s"
    TASK_REL="$work/gbtask.rel"
fi
TASK_ROOT_REL=""
if [ "$TASK_ROOT_FLAG" = "1" ]; then
    case " $ALL_APPDEFS " in
        *" -DGB_MSX2 "*) TASK_RUNTIME_BASE=0xC900 ;;
        *)               TASK_RUNTIME_BASE=0x3C00 ;;
    esac
    python3 tools/embed_scheduler.py "$TASK_RUNTIME_RAW" "$work/gbtaskroot.s" \
        --base "$TASK_RUNTIME_BASE"
    "$SDAS" -o "$work/gbtaskroot.rel" "$work/gbtaskroot.s"
    TASK_ROOT_REL="$work/gbtaskroot.rel"
fi
# --fomit-frame-pointer: frame on IY, not IX. The kernel/fs code uses IX as a
# scratch (it never touches IY) and firmware calls preserve the caller's IY, so
# this stops a kernel call from wrecking an app's frame pointer (which crashed
# the notepad's return - SDCC's epilogue is `ld sp,<fp>`).
# APPDEFS (e.g. -DGB_MSX2) MUST reach every libgb C unit, not just main.c: gb.h
# derives GB_COLS/GB_LINES/GB_XPIX from it, and gbwin.c/gbdoc.c clamp window
# drag/resize + fullscreen to those extents. Omitting it built libgb with the
# CPC 320x200 extents, so on MSX windows would not drag past x=320 (#287).
"$SDCC" -mz80 --fomit-frame-pointer $APP_CFLAGS $ALL_APPDEFS -I "$GB" -c "$APP/main.c" -o "$work/main.rel"
GBWIN_REL=""
if [ "$GBWIN_FLAG" = "1" ]; then
    GBWIN_DEFS=""
    [ "$GBWIN_DRAG_ONLY_FLAG" != "1" ] || GBWIN_DEFS="-DGBWIN_DRAG_ONLY"
    "$SDCC" -mz80 --fomit-frame-pointer $GBWIN_DEFS $ALL_APPDEFS -I "$GB" -c "$GB/gbwin.c" -o "$work/gbwin.rel"
    GBWIN_REL="$work/gbwin.rel"
fi
WIDGETS_REL=""
if [ "$WIDGETS_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbwidgets.c" -o "$work/gbwidgets.rel"
    WIDGETS_REL="$work/gbwidgets.rel"
elif [ "$BUTTON_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer -DGB_BUTTON_ONLY $ALL_APPDEFS -I "$GB" -c "$GB/gbwidgets.c" -o "$work/gbwidgets.rel"
    WIDGETS_REL="$work/gbwidgets.rel"
fi
ACTIONS_REL=""
if [ "$ACTIONS_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbactions.c" -o "$work/gbactions.rel"
    ACTIONS_REL="$work/gbactions.rel"
fi
SCROLL_REL=""
if [ "$SCROLL_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbscroll.c" -o "$work/gbscroll.rel"
    SCROLL_REL="$work/gbscroll.rel"
fi
SCROLL16_REL=""
if [ "$SCROLL16_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbscroll16.c" -o "$work/gbscroll16.rel"
    SCROLL16_REL="$work/gbscroll16.rel"
fi
TOGGLE_REL=""
if [ "$TOGGLE_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbtoggle.c" -o "$work/gbtoggle.rel"
    TOGGLE_REL="$work/gbtoggle.rel"
fi
STEPPER_REL=""
if [ "$STEPPER_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbstepper.c" -o "$work/gbstepper.rel"
    STEPPER_REL="$work/gbstepper.rel"
fi
SELECTOR_REL=""
if [ "$SELECTOR_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbselect.c" -o "$work/gbselect.rel"
    SELECTOR_REL="$work/gbselect.rel"
fi
SLIDER_REL=""
if [ "$SLIDER_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbslider.c" -o "$work/gbslider.rel"
    SLIDER_REL="$work/gbslider.rel"
fi
FORM_REL=""
if [ "$FORM_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbform.c" -o "$work/gbform.rel"
    FORM_REL="$work/gbform.rel"
fi
FORM_SELECT_REL=""
if [ "$FORM_SELECT_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbform_select.c" -o "$work/gbform_select.rel"
    FORM_SELECT_REL="$work/gbform_select.rel"
fi
TIMESET_REL=""
if [ "$TIMESET_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$GB/gbsettime.c" -o "$work/gbsettime.rel"
    TIMESET_REL="$work/gbsettime.rel"
fi
SOUND_REL=""
if [ "$SOUND_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $HELPER_CFLAGS $ALL_APPDEFS -I "$GB" \
        -c "$GB/gbsound.c" -o "$work/gbsound.rel"
    SOUND_REL="$work/gbsound.rel"
fi
SIZEPROMPT_REL=""
if [ "$SIZEPROMPT_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $HELPER_CFLAGS $ALL_APPDEFS -I "$GB" \
        -c "$GB/gbsizedlg.c" -o "$work/gbsizedlg.rel"
    SIZEPROMPT_REL="$work/gbsizedlg.rel"
fi
TITLEBAR_REL=""
if [ "$TITLEBAR_FLAG" = "1" ]; then
    "$SDCC" -mz80 --opt-code-size --fomit-frame-pointer $HELPER_CFLAGS $ALL_APPDEFS -I "$GB" \
        -c "$GB/gbtitle.c" -o "$work/gbtitle.rel"
    TITLEBAR_REL="$work/gbtitle.rel"
fi
# Opt-in dialogs (#114, #142). The heavy render (popup/prompt/file-picker) now lives in
# the paged GBUI kernel module (#142 step 1b); an app that needs ANY dialog links only
# the tiny marshalling stub gbui_stub.c (gb_popup/gb_prompt/gb_pickfile/gb_pickdir ->
# GB_UI). That ~800-byte/app saving is what lets the data-heavy apps fit gb_doc.
#   DIALOGS / PROMPT / PICKER  -> gbui_stub.c (the general stubs)
#   SIZEPROMPT=1               -> gbsizedlg.c (opt-in two-field dimensions stub)
#   DOC=1                      -> gbdoc.c too (the document/File-menu framework)
DLG_REL=""
if [ "$DIALOGS_FLAG" = "1" ] || [ "$PROMPT_FLAG" = "1" ] || [ "$PICKER_FLAG" = "1" ] || [ "$DOC_FLAG" = "1" ] || [ "$DOCRO_FLAG" = "1" ]; then
    "$SDCC" -mz80 --fomit-frame-pointer $HELPER_CFLAGS $ALL_APPDEFS -I "$GB" -c "$GB/gbui_stub.c" -o "$work/gbui_stub.rel"
    DLG_REL="$work/gbui_stub.rel"
fi
# DOC=1 = the full document framework; DOCRO=1 = a READ-ONLY variant (-DGBDOC_RO) that
# omits the Save/Save As path, so a viewer-style app saves that code room (#144).
if [ "$DOC_FLAG" = "1" ] || [ "$DOCRO_FLAG" = "1" ]; then
    RO=""; [ "$DOCRO_FLAG" = "1" ] && RO="-DGBDOC_RO"
    "$SDCC" -mz80 --fomit-frame-pointer $RO $ALL_APPDEFS -I "$GB" -c "$GB/gbdoc.c" -o "$work/gbdoc.rel"
    DLG_REL="$DLG_REL $work/gbdoc.rel"
fi
# NET=1 uses the target's gb_net_* backend. CPC calls the active paged GBNET
# module; MSX apps call a discovered TCP/IP UNAPI implementation directly.
if [ "$NET_FLAG" = "1" ]; then
    "$SDCC" -mz80 --fomit-frame-pointer $ALL_APPDEFS -I "$GB" -c "$NET_SRC" -o "$work/gbnet_stub.rel"
    DLG_REL="$DLG_REL $work/gbnet_stub.rel"
fi
"$SDCC" -mz80 --no-std-crt0 --code-loc "$CODE_LOC" --data-loc "$DATA_LOC" \
    "$work/crt0.rel" "$work/main.rel" $GBWIN_REL $WIDGETS_REL $ACTIONS_REL $SCROLL_REL $SCROLL16_REL \
    $TOGGLE_REL $STEPPER_REL $SELECTOR_REL $SLIDER_REL $FORM_REL \
    $FORM_SELECT_REL $TIMESET_REL $SOUND_REL $SIZEPROMPT_REL $TITLEBAR_REL $DLG_REL $APP_PROBE_REL $REPAINTTOP_REL $TASK_REL $TASK_ROOT_REL \
    "$work/gblib.rel" -o "$work/app.ihx"
# STABILITY GUARD: the app must fit its 16K page. The whole LOADED IMAGE
# (_CODE + the startup tails _GSINIT/_GSFINAL/_INITIALIZER, which the linker places
# AFTER the code) must end below data-loc - otherwise the RAM data area starts inside
# it and gsinit zeroes its own code as it runs -> instant reboot (bit NOTEPAD: a
# _CODE-only check passed while _GSINIT overlapped _DATA). And data+bss must end below
# the kernel (#8000). LOAD_LIMIT mirrors the target's app loader ceiling; it is
# #7F00 by default and #7F80 only for PCW Browser's record-rounded image.
python3 tools/check_app_layout.py "$work/app.map" \
    --app "$APP" --data-loc "$DATA_LOC" --load-limit "$LOAD_LIMIT" \
    --task-stack-reserve "$TASK_STACK_RESERVE"

"$MAKEBIN" -p "$work/app.ihx" "$work/app.bin"

# makebin emits a flat image from #0000; the app lives at #4000 -> strip low 16K.
tail -c +16385 "$work/app.bin" > "$work/app.raw"
if [ -n "$APP_ICON" ]; then
    if [ -n "$APP_ICON16" ]; then
        python3 tools/embed_app_icon.py inject \
            "$APP_ICON" "$APP_ICON16" "$work/app.raw" "$OUT"
    else
        python3 tools/embed_app_icon.py inject "$APP_ICON" "$work/app.raw" "$OUT"
    fi
else
    cp "$work/app.raw" "$OUT"
fi
gb_write_stamp "$stamp" "$cache_key"
echo "Built $OUT ($(stat -c%s "$OUT") bytes) from $APP"
