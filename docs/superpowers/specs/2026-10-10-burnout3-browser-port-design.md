# Burnout 3: Takedown (PS2) in the browser — design

Date: 2026-10-10. Status: approved in conversation, awaiting written review.

## Goal

Run Burnout 3: Takedown (PS2, NTSC-U, SLUS-21050) in a modern browser with no
install, with modern gamepad support (DualSense first), using static
recompilation as the scaffold and progressive decompilation as the long-term
product. The player points the browser at their own ISO once; afterwards the
page boots straight into the game.

Success for the first release: Mark and Scott open a private URL in Chrome,
pick their ISO once, and play menus and a race with audio at 60 fps with a
DualSense. Later releases replace recompiled code with our own readable C++.

## What this is not

- Not a public distribution of the game. The ISO, the extracted ELF, the VU
  microcode, the BIOS, the generated C++ and the built WebAssembly (which is a
  translation of EA's executable) are never committed and never served
  publicly. The deployed site is private to the two of us until our own
  decompiled code has replaced the translated code.
- Not a matching (byte-exact) decompilation. We decompile for behaviour and
  readability, verified by differential execution, not by rebuilding the ELF.
- Not an emulator. No interpreter or JIT of EE code at runtime. (VU1
  microprograms are the one exception; the runtime already compiles those
  on the fly.)

## Approach chosen

Recomp-first. Rejected alternatives: pure matching decomp (5,500 functions
before anything runs; duplicates a GPL project we cannot merge with), and a
from-scratch reimplementation in the style of reB3 (nothing faithful for a
long time). See `docs/research/PROVENANCE.md` for every project consulted.

## Architecture

```
Burnout3Decomp/                      (this repo, MIT, public)
  external/FORK.md                   pins the BlissVibes fork of snuri00/ps2-web-recomp (URL, branch, commit);
                                     the fork itself is cloned at ~/b3/ps2-web-recomp inside WSL, outside the repo,
                                     because its port.sh writes game-derived files into its own tree
  game/
    port config                      Burnout-specific recompiler config (entry points, patches)
    hooks/                           list of guest addresses overridden by our code
  src/                               our decompiled C++ (one file per subsystem), compiled into the runtime
  web/                               page shell: disc picker + OPFS cache, gamepad mapping, UI
  tools/                             scripts: iso verify, headless run, differential test harness, progress report
  docs/
    research/PROVENANCE.md           credits ledger (keep current)
    superpowers/specs, plans
    re/                              reverse-engineering notes per subsystem (addresses, structs, theories)
  (no .gitignore by Mark's choice: all game-derived files and build output live outside the repo tree, in the WSL home directory)
```

Three layers:

1. **Recompiler + runtime** (the fork, pinned by commit in `external/FORK.md`). Generic fixes (missing syscalls, IOP
   library functions, GS features, recompiler bugs) land in the fork on a
   `burnout3` branch and are offered upstream as PRs. The fork is MIT; we keep
   its license and credit.
2. **Game layer** (this repo). Port config, the web shell, and `src/` where our
   decompiled functions override recompiled ones. Overrides use the runtime's
   function-hook mechanism: a guest address maps to a host C++ function with
   the same calling convention. If the runtime lacks a hook table, we add one
   in the fork (weak symbol per recompiled function, as XenonRecomp does).
3. **Disc**. Supplied by the player, cached in the browser, never on a server.

## Disc flow (load-once)

1. First visit: the page shows one button, "Choose your Burnout 3 ISO".
2. The player picks a plain `.iso`. Archives (`.zip`, `.7z`) are not accepted
   in v1; they would add a decompressor to the page.
3. The page reads `SYSTEM.CNF` from the ISO-9660 image and checks for
   `SLUS_210.50`. Wrong disc: show which disc it is and stop.
4. The ISO is copied into the Origin Private File System (OPFS) with a
   progress bar. Typical size is about 3.5 GB; Chrome's OPFS quota allows it.
5. The runtime boots. Every later visit skips to step 5.
6. An "Eject disc" control deletes the cached file.

Implementation: a `DiscSource` interface in the runtime with
`read(offset, length)`. Backends: `HttpRangeDiscSource` (today's behaviour,
used in local development with `serve.py`) and `OpfsDiscSource` (reads via
`FileSystemSyncAccessHandle` inside the emulation worker). Saves already live
in browser storage in the upstream runtime and are kept.

## Gamepad

The upstream page uses the browser Gamepad API. DualSense reports as a
standard-mapping pad in Chrome, so face buttons, shoulders, sticks and D-pad
map directly to the DualShock 2 layout. Work needed: confirm the mapping in
the page, add analog stick dead zones matching the PS2's, expose a remap
screen only if the default mapping is wrong. Keyboard mapping stays as
upstream.

## Deployment

- Host: Vercel. It can set the `Cross-Origin-Opener-Policy` and
  `Cross-Origin-Embedder-Policy` headers the page needs for
  SharedArrayBuffer. GitHub Pages cannot.
- Access: Vercel Deployment Protection (password or Vercel auth) so only Mark
  and Scott can load the build. Public repo, private deployment.
- Build: produced on Mark's machine in WSL, uploaded as a prebuilt static
  directory. Vercel never sees the ISO. CI does not build the game (it would
  need the ELF).

## Milestones

| # | Milestone | Done when | Who |
|---|---|---|---|
| 0 | Toolchain | WSL2 Ubuntu with clang, cmake, ninja, python3, emsdk, 7z installed; fork cloned as submodule; upstream smoke test (the fork's own tests or a headless run) passes | Opus |
| 1 | Burnout boots headless | `scripts/port.sh burnout3 <iso>` completes; headless run produces frames past the boot screens; log shows no `unhandled syscall`, `[iop] missing`, or unsupported-GS messages | Opus runs; Fable diagnoses and fixes runtime gaps |
| 2 | Playable in Chrome | Local server build: menus, car select, one race with audio; DualSense works; 60 fps at 1x on the RTX 3090 Ti | Opus |
| 3 | Load-once disc, private deploy | Vercel deployment behind protection; pick ISO once; revisit boots directly; saves persist; eject works | Opus |
| 4 | Decomp into the scaffold (repeating) | Ghidra project with R5900 module and the ELF imported; differential test harness runs recompiled vs. our C++ on recorded inputs and compares register and memory state; first ten functions replaced and passing | Fable analyses and writes RE notes; Opus implements and tests |

Milestone 4 is the open-ended phase. Function selection order: start with
leaf functions that are small and pure (math, vehicle parameter lookup), then
vehicle physics, then game rules (boost, takedowns, scoring), then AI and
traffic, then presentation. Vendor code (RenderWare, Sony libs, EA
DirtySock) stays recompiled indefinitely.

## Verification

- Headless runner is the first check for every change: frame screenshots at
  fixed frame numbers plus a log scan for known error strings.
- Browser check second, by hand or Playwright, on Chrome with WebGPU.
- Decompiled functions: differential execution. Record guest state at entry,
  run both the recompiled and our version, compare state at exit. A function
  is accepted only after passing on a recorded corpus from real play.
- Progress report script: counts bytes of game code still recompiled vs.
  replaced, written to `docs/PROGRESS.md`.
- Credits: `docs/research/PROVENANCE.md` is updated in the same commit that
  starts using any external code or data.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| RenderWare 3.6 PS2 path uses GS features the WebGPU rasterizer lacks | Software rasterizer reference in the runtime shows the correct image; implement the missing feature in the fork; Fable task |
| IOP sound/stream driver calls an unimplemented IOP library function | Error log names the library and ordinal; implement in the fork's IOP reimplementation |
| Recompiler misses functions reached only by pointer | `port.sh` already iterates headless runs to discover them; add recorded-input runs that reach menus and races |
| Disk space: 107 GB free on C: | Keep WSL distro, emsdk and builds under 10 GB; put the WSL vhdx on the drive with the most room |
| Fork drifts from upstream | Rebase the `burnout3` branch on upstream monthly; keep generic fixes separate from Burnout config |
| OPFS write of 3.5 GB fails or is evicted | Use `navigator.storage.persist()`; show quota before copying; eject-and-retry path |
| Legal exposure | No EA-derived bytes in repo or public hosting; private deployment; bring-your-own-disc; document in README |

## Decisions recorded

- ISO region: NTSC-U SLUS-21050 only for v1. Other regions rejected with a
  message. Mark's ISO verified 2026-10-10: SHA-1 11a7f335a37d2f3f5c13b967f3072c84f8eded02 (2,867,822,592 bytes), matching the documented NTSC-U SLUS-21050 image. Local path recorded in docs/LOCAL_PATHS.md.
- Archive input: not in v1.
- Resolution: 1x default, selector kept from upstream.
