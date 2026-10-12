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

## Private deploy (Vercel)
The built page contains the game's executable (`ps2web.data`) and its translated
code (`ps2web.wasm`), so it only ever goes to a project behind a login: Vercel
project `burnout3-web`, with Vercel Authentication switched on in Project →
Settings → Deployment Protection before the first upload. The disc itself is never
part of a deploy; every player picks their own ISO in the page.

```
tools/build-web.sh                      # page into ~/b3/out/web
bash tools/deploy/deploy.sh             # dry run: stages ~/b3/out/deploy, uploads nothing
bash tools/deploy/deploy.sh --yes       # links the project and deploys to production
```
The dry run copies the page, `ps2web.wasm`, `ps2web.data` and the scripts into
`~/b3/out/deploy` (outside this repository), lists them with their sizes and prints
the two `vercel` commands that `--yes` would run. `--yes` needs `npm i -g vercel`
and `vercel login` once. `tools/deploy/vercel.json` sets the headers the page needs:
`Cross-Origin-Opener-Policy` and `Cross-Origin-Embedder-Policy` (threads need a
cross-origin isolated page), `Cache-Control: no-cache` so a new build is picked up
on the next load, and the `application/wasm` content type.

A second developer gets in through a Protection Bypass link generated in the same
settings page. Send it privately; never commit it.

## Controls
Standard-mapping gamepads (DualSense, DualShock 4, Xbox) work through the
browser. Keyboard: Z cross, X circle, A square, S triangle, Q/W L1/R1, 1/2
L2/R2, Enter start, Backspace select, arrows D-pad, IJKL left stick, F fullscreen.

## License
MIT for our code. Adapted files carry their origin in a header comment.
