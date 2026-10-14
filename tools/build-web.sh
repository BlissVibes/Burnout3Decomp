#!/usr/bin/env bash
# Builds the browser page for Burnout 3 with this repo's web/shell.html.
# Output: ~/b3/out/web (outside the repo on purpose: it contains the game's code).
# Environment (all optional):
#   FORK=...       another checkout of the fork (default ~/b3/ps2-web-recomp)
#   OUT=...        the build directory, where the page ends up (default ~/b3/out/web)
#   GENERATED=...  the game's recompiled code (default $FORK/generated/burnout3), e.g. a tree translated
#                  by a work-in-progress recompiler, so the shared one stays as it is
#   UNITS=...      the unit list: how each generated unit is compiled (default tools/units.txt);
#                  UNITS=none compiles every unit the same way, as before the list existed
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
FORK="${FORK:-$HOME/b3/ps2-web-recomp}"   # another checkout of the fork: FORK=... OUT=... tools/build-web.sh
NAME=burnout3
OUT="${OUT:-$HOME/b3/out/web}"
EMSDK="${EMSDK:-$FORK/tools/emsdk}"
GENERATED="${GENERATED:-$FORK/generated/$NAME}"
UNITS="${UNITS:-$REPO/tools/units.txt}"

boot=$(sed -n 's/^BOOT2 *= *//p' "$FORK/games/$NAME/disc/SYSTEM.CNF" | tr -d '\r')
program=${boot#*\\}; program=${program%;*}
elf="$FORK/games/$NAME/disc/$program"
[ -f "$elf" ] || { echo "no executable at $elf; run tools/port.sh first" >&2; exit 1; }
[ -f "$GENERATED/table.cpp" ] || { echo "no generated code in $GENERATED" >&2; exit 1; }

# The unit list (tools/units.txt): "hot <unit>" and "warm <unit>" lines. Hot units get their guest loads
# and stores inlined, warm ones are compiled as usual, and every other unit is compiled for size (-Oz).
hot=""; warm=""; cold=""
if [ "$UNITS" != none ]; then
  [ -f "$UNITS" ] || { echo "no unit list at $UNITS (UNITS=none builds without one)" >&2; exit 1; }
  hot=$(awk '$1 == "hot" { printf "%s%s", sep, $2; sep = ";" }' "$UNITS")
  warm=$(awk '$1 == "warm" { printf "%s%s", sep, $2; sep = ";" }' "$UNITS")
  cold=-Oz
fi

mkdir -p "$OUT"
printf '%s\n' "$boot" > "$OUT/boot.txt"
source "$EMSDK/emsdk_env.sh" > /dev/null 2>&1
emcmake cmake -S "$FORK" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPS2_GENERATED="$GENERATED" -DPS2_WEB_ELF="$elf" -DPS2_WEB_BOOT_FILE="$OUT/boot.txt" \
  -DPS2_WEB_SHELL="$REPO/web/shell.html" -DPS2_HOOKS="$REPO/src/hooks" \
  "-DPS2_HOT_UNITS=$hot" "-DPS2_WARM_UNITS=$warm" "-DPS2_COLD_UNIT_OPTIONS=$cold" > /dev/null
ninja -C "$OUT" ps2web
cp "$REPO"/web/*.js "$OUT"/ 2>/dev/null || true
gzip -kf9 "$OUT/ps2web.wasm" "$OUT/ps2web.data" "$OUT/ps2web.js"
ls -la "$OUT"/ps2web.*
echo "built $OUT/ps2web.html"
echo "serve:  python3 $FORK/scripts/serve.py $OUT ~/b3/iso/burnout3.iso   then open http://localhost:8620/"
