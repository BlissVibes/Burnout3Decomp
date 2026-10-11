# ps2-web-recomp fork

- Upstream: https://github.com/snuri00/ps2-web-recomp (MIT, (c) 2026 Samir Nuri)
- Our fork: https://github.com/BlissVibes/ps2-web-recomp, branch `burnout3`
- Pinned commit: `17a5eee43eb013d43f26ace6dbff5139e5e8043c` (upstream `17a5eee43eb013d43f26ace6dbff5139e5e8043c`, 2026-10-10)
- Local clone (WSL): `~/b3/ps2-web-recomp`

Why not a submodule: the fork's `port.sh` writes `games/`, `generated/` and `build/`
inside its own tree. Those hold game-derived files that must never enter this repo,
so the fork lives outside the repo and is pinned here instead.

## Update procedure
1. `cd ~/b3/ps2-web-recomp && git fetch upstream && git rebase upstream/main`
2. Rebuild: `scripts/setup.sh`, then rerun `tools/port.sh` from this repo.
3. If the headless run still passes `tools/scan-log.py`, `git push --force-with-lease`,
   update the pinned commit above, and note the upstream date.

## Our changes on `burnout3` (keep generic, offer upstream)
- `cmake: PS2_WEB_SHELL lets a port supply its own shell page`: `CMakeLists.txt` gains a
  `PS2_WEB_SHELL` cache variable (default: the fork's own `web/shell.html`) used for
  `--shell-file` and the link dependency. `tools/build-web.sh` points it at this repo's shell.
- `analysis: seed functions from pointer tables in data`: `recomp/analysis.cpp` gains
  `pointer_tables()`; eerecomp now translates functions reached only through a table of four
  or more code pointers (static constructor lists, vtables). See
  `docs/re/gap-001-static-constructor-table.md`.
- `hw: deliver INTC interrupts after a short latency so status polls can see the bit`:
  `runtime/src/hw.cpp`, `hw.hpp`, `system.cpp`. An INTC line that rises while enabled is
  delivered 32 EE cycles later, so a tight poll of INTC_STAT (libgraph's `sceGsSyncV`) sees
  the bit before the kernel acknowledges it. State version 2 → 3. This is a behaviour change
  for every game: when offering it upstream, ask for a run against GT4 and God of War II,
  which could not be tested here. See `docs/re/gap-002-vblank-poll-interrupt-latency.md`.
- `codegen: remove stale unit files before writing`: `recomp/codegen.cpp`. eerecomp deletes
  `unit_*.cpp` files in its output directory that the current pass did not emit; the build
  globs that directory, and leftovers from a pass with more units broke the compile.
- `gif: reflect VIF1 MSKPATH3 and GIF_MODE in GIF_STAT`: `runtime/src/gs/gif.cpp`, `gif.hpp`,
  `runtime/src/vif.cpp`. VIF1 code 0x06 now sets a PATH3 mask flag that reads back as
  GIF_STAT bit 1, and GIF_MODE bits 0 and 2 are mirrored into GIF_STAT. PATH3 transfers are
  not actually stalled while masked. See `docs/re/gap-003-gif-status-path3-mask.md`.
- `gif: hold PATH3 while masked and report the FIFO count`: `runtime/src/gs/gif.cpp`,
  `runtime/src/vif.cpp`. PATH3 data that arrives while PATH3 is masked is held and forwarded
  when the mask clears; GIF_STAT reports the held amount as the FIFO count (capped at 16) and
  the P3Q bit. Only the EE pass of the VIF1 stream changes the mask. See
  `docs/re/gap-004-path3-stall-and-gif-fifo.md`.
