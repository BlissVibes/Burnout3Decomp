#!/usr/bin/env bash
# Recompiles Burnout 3 with the fork's port.sh and scans the exploring run's log.
# Run inside WSL. Optional args are passed through to the fork's port.sh
# (for example --frames 1800 --press 300:start --press 900:cross).
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
scripts/port.sh "$NAME" "$ISO" --frames "$FRAMES" "$@" 2>&1 | tee "$HOME/b3/out/port-$(date +%Y%m%d-%H%M%S).log"

echo
echo "== scan of the last exploring run"
python3 "$REPO/tools/scan-log.py" "$FORK/games/$NAME/explore.log" --expect-frames "$FRAMES" || true
echo "frames in $FORK/games/$NAME/shots:"; ls "$FORK/games/$NAME/shots" | tail -5
