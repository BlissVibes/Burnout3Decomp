#!/usr/bin/env bash
# Prepares the WSL Ubuntu box for the Burnout 3 browser port.
# Run inside WSL:  bash "/mnt/c/Users/Mark/Documents/_vibe coding projects/Burnout3Decomp/tools/wsl-setup.sh"
set -euo pipefail

FORK_URL="https://github.com/BlissVibes/ps2-web-recomp.git"
# Branch burnout3 is not on GitHub yet (2026-10-13), so the clone below fails on a new machine.
# Until it is pushed, restore the fork from the bundle named in external/FORK.md.
FORK_BRANCH="burnout3"
B3="$HOME/b3"
FORK="$B3/ps2-web-recomp"

echo "== packages"
sudo apt-get update -qq
sudo apt-get install -y -qq git cmake ninja-build clang lld python3 python3-pip curl unzip p7zip-full nodejs npm pkg-config libsdl2-dev

echo "== layout"
mkdir -p "$B3/iso" "$B3/out"

if [ ! -d "$FORK/.git" ]; then
  echo "== cloning the fork"
  git clone --branch "$FORK_BRANCH" "$FORK_URL" "$FORK"
else
  echo "== fork already cloned"
fi

echo "== fork toolchain (emsdk, wgpu-native, eerecomp, vurecomp)"
"$FORK/scripts/setup.sh"

echo "== versions"
clang++ --version | head -1
cmake --version | head -1
ninja --version
python3 --version
node --version
7z | head -2 | tail -1
ls -la "$FORK/build/tools/eerecomp" "$FORK/build/tools/vurecomp"
echo "== ready: fork at $FORK, work dir $B3"
