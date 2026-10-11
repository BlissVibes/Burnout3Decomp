#!/usr/bin/env bash
# Recompiles Burnout 3 with the fork's port.sh and scans the exploring run's log.
# Run inside WSL. Optional args are passed through to the fork's port.sh
# (for example --frames 1800 --press 300:start --press 900:cross).
# Two limits guard each exploring run (tools/watch-run.py kills ps2run when one is passed):
#   B3_LOG_CAP_BYTES  largest games/burnout3/explore.log, default 1073741824 (1 GB); 0 = no cap
#   B3_RUN_TIMEOUT    longest run in seconds, default 1800 (30 minutes); 0 = no limit
# When a limit is hit the wrapper prints "log cap hit" or "run timeout hit" and exits 1.
# Frames of an earlier run (games/burnout3/shots/frame_*.ppm) are removed first, so the
# frames listed at the end always belong to this run.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
FORK="$HOME/b3/ps2-web-recomp"
ISO="$HOME/b3/iso/burnout3.iso"
NAME=burnout3

[ -f "$ISO" ] || { echo "no disc at $ISO; see docs/LOCAL_PATHS.md and Task 3" >&2; exit 1; }
python3 "$REPO/tools/verify-iso.py" "$ISO"

# Headless runs in WSL have no reliable Vulkan device; PS2_GPU_FALLBACK lets the
# runtime use the fallback adapter rather than fail (the fork reads it in gpu.cpp).
export PS2_GPU_FALLBACK=1
FRAMES=3600
if [ "${1:-}" = "--frames" ]; then FRAMES="$2"; shift 2; fi
cd "$FORK"
mkdir -p "$HOME/b3/out"
STAMP="$(date +%Y%m%d-%H%M%S)"
MARKER="$HOME/b3/out/port-$STAMP.limit"
rm -f "$MARKER"
python3 "$REPO/tools/watch-run.py" --log "$FORK/games/$NAME/explore.log" --match "games/$NAME/misses.txt" --marker "$MARKER"   --cap-bytes "${B3_LOG_CAP_BYTES:-1073741824}" --timeout "${B3_RUN_TIMEOUT:-1800}" &
WATCHER=$!
trap 'kill "$WATCHER" 2> /dev/null || true' EXIT
rm -f "$FORK/games/$NAME/shots"/frame_*.ppm
scripts/port.sh "$NAME" "$ISO" --frames "$FRAMES" "$@" 2>&1 | tee "$HOME/b3/out/port-$STAMP.log"
kill "$WATCHER" 2> /dev/null || true

echo
echo "== scan of the last exploring run"
python3 "$REPO/tools/scan-log.py" "$FORK/games/$NAME/explore.log" --expect-frames "$FRAMES" || true
echo "frames in $FORK/games/$NAME/shots:"; ls "$FORK/games/$NAME/shots" | tail -5
if [ -s "$MARKER" ]; then
  echo
  echo "== run stopped by a limit"
  cat "$MARKER"
  exit 1
fi
