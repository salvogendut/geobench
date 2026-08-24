#!/usr/bin/env bash
# Repack only the PCW companion floppy from already-built application payloads.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-QA/PCW/COMPANION.DSK}"
COMP_ADDS=()

mkdir -p build/pcw "$(dirname "$OUT")"
for bdp in assets/backdrops/*.BDP; do
    name=$(basename "$bdp" .BDP | tr a-z A-Z)
    cp "$bdp" "build/pcw/$name.BDP"
    COMP_ADDS+=(--add "build/pcw/$name.BDP=$name.BDP")
done

python3 tools/mkpcwdsk.py "$OUT" \
    "${COMP_ADDS[@]}" \
    --add build/pcw/TELNET.RAW=TELNET.APP \
    --add build/pcw/NETTEST.RAW=NETTEST.APP \
    --add build/pcw/FORMREF.RAW=FORMREF.APP \
    --add build/pcw/SNDTEST.RAW=SNDTEST.APP \
    --add build/pcw/WGET.RAW=WGET.APP \
    --add build/pcw/BROWSER.RAW=BROWSER.APP \
    --add build/GBIMG.RAW=GBIMG.MOD \
    --add build/pcw/XAOS.RAW=XAOS.APP \
    --add build/pcw/MAHJONG.RAW=MAHJONG.APP \
    --add build/pcw/CALC.RAW=CALC.APP \
    --add assets/WELCOME.TXT=WELCOME.TXT
