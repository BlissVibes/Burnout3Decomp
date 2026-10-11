# Burnout 3: Takedown in the browser

An unofficial fan project that runs the PlayStation 2 game *Burnout 3: Takedown*
(NTSC-U, SLUS-21050) in a modern browser with no install, by statically
recompiling its executable to WebAssembly and progressively replacing that
translated code with our own readable C++. Not affiliated with EA or Criterion.

**You need your own disc.** The page asks once for your ISO, keeps it in your
browser's private storage, and never uploads it. Nothing from the game is in
this repository or on any server we run: not the disc, not the executable, not
code generated from it. The built page is deployed privately for the project's
two developers for the same reason.

## How it works
- [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) (MIT, by Samir Nuri),
  through our fork pinned in `external/FORK.md`, translates the game's MIPS code
  to C++, compiles it to WebAssembly and supplies the PS2 runtime (kernel, IOP,
  GS on WebGPU, SPU2, VU).
- `web/` is our page: disc picker, browser-side cache, gamepad handling.
- `tools/` are our scripts: ISO check, port wrapper, log scanner, build, deploy.
- `docs/re/` holds reverse-engineering notes as we decompile.

Design: `docs/superpowers/specs/2026-10-10-burnout3-browser-port-design.md`.
Credits for every project we learned from or built on: `docs/research/PROVENANCE.md`.

## Build (WSL2 Ubuntu)
```
bash tools/wsl-setup.sh          # once
python3 tools/verify-iso.py ~/b3/iso/burnout3.iso
tools/port.sh                    # recompile + headless exploring runs
tools/build-web.sh               # page into ~/b3/out/web
python3 ~/b3/ps2-web-recomp/scripts/serve.py ~/b3/out/web ~/b3/iso/burnout3.iso
```
`tools/port.sh` stops a headless run whose log passes 1 GB or that runs longer than
30 minutes, prints `log cap hit` or `run timeout hit` and exits 1. Change the limits
with `B3_LOG_CAP_BYTES` (bytes) and `B3_RUN_TIMEOUT` (seconds); 0 turns one off.

Tests: `python3 -m unittest discover -s tests -p 'test_*.py'` and `node --test`.

## Controls
Standard-mapping gamepads (DualSense, DualShock 4, Xbox) work through the
browser. Keyboard: Z cross, X circle, A square, S triangle, Q/W L1/R1, 1/2
L2/R2, Enter start, Backspace select, arrows D-pad, IJKL left stick, F fullscreen.

## License
MIT for our code. Adapted files carry their origin in a header comment.
