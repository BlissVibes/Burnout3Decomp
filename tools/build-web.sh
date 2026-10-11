#!/usr/bin/env bash
# Builds the browser page for Burnout 3 with this repo's web/shell.html.
# Output: ~/b3/out/web (outside the repo on purpose: it contains the game's code).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
FORK="$HOME/b3/ps2-web-recomp"
NAME=burnout3
OUT="$HOME/b3/out/web"
EMSDK="${EMSDK:-$FORK/tools/emsdk}"

boot=$(sed -n 's/^BOOT2 *= *//p' "$FORK/games/$NAME/disc/SYSTEM.CNF" | tr -d '\r')
program=${boot#*\}; program=${program%;*}
elf="$FORK/games/$NAME/disc/$program"
[ -f "$elf" ] || { echo "no executable at $elf; run tools/port.sh first" >&2; exit 1; }

mkdir -p "$OUT"
printf '%s\n' "$boot" > "$OUT/boot.txt"
source "$EMSDK/emsdk_env.sh" > /dev/null 2>&1
emcmake cmake -S "$FORK" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPS2_GENERATED="$FORK/generated/$NAME" -DPS2_WEB_ELF="$elf" -DPS2_WEB_BOOT_FILE="$OUT/boot.txt" \
  -DPS2_WEB_SHELL="$REPO/web/shell.html" > /dev/null
ninja -C "$OUT" ps2web
cp "$REPO"/web/*.js "$OUT"/ 2>/dev/null || true
gzip -kf9 "$OUT/ps2web.wasm" "$OUT/ps2web.data" "$OUT/ps2web.js"
ls -la "$OUT"/ps2web.*
echo "built $OUT/ps2web.html"
echo "serve:  python3 $FORK/scripts/serve.py $OUT ~/b3/iso/burnout3.iso   then open http://localhost:8620/"
