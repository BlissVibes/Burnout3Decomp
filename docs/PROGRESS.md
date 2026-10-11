# Progress

| Date | Milestone | State | Notes |
|---|---|---|---|
| 2026-10-10 | 0 Toolchain | done | WSL on S:, fork built |
| 2026-10-10 | 1 Headless boot | blocked on gaps | run 001 halts after 2 frames: recompiler misses the 158 static constructors; see docs/re/boot-log-001.md |
| 2026-10-10 | 1 Headless boot | blocked on gaps | run 002 (constructors seeded into entries.txt) runs 1800 frames with no missing code but draws nothing: EE spins at 0x0010C2F0 after the post-reboot SIF calls; see docs/re/boot-log-002.md |
| 2026-10-10 | 1 Headless boot | blocked on gaps | gap 001 fixed in the fork (eerecomp seeds functions from pointer tables): run 003 with an empty entries.txt matches run 002 exactly. Still spinning at 0x0010C2F0, nothing drawn; see docs/re/boot-log-003.md |
| 2026-10-10 | 1 Headless boot | blocked on gaps | gap 002 fixed in the fork (INTC delivery latency): run 004 gets past the vertical-blank wait, sets the display mode, draws 1 primitive and starts threads 3–6. All six frames are black; thread 1 now loops at 0x001F19C0. See docs/re/boot-log-004.md |
| 2026-10-10 | 1 Headless boot | blocked on gaps | gap 003 fixed in the fork (GIF_STAT reports the PATH3 mask): run 005 passes the wait at 0x001F19C0 and sends two image transfers, then the same routine waits again at 0x001F1A28. Frames still black. See docs/re/boot-log-005.md |
| 2026-10-11 | 1 Headless boot | blocked on gaps | gap 004 fixed in the fork (PATH3 held while masked, GIF FIFO count): run 006 shows the first picture, the LOADING screen with an empty progress bar; 143 primitives, 27 IOP modules loaded. The bar never moves: thread 1 loops at 0x0010D508. See docs/re/boot-log-006.md |
| 2026-10-10 | 2 Playable in Chrome | page builds and loads; game not drawing yet | `tools/build-web.sh` builds with our `web/shell.html` through the fork's `PS2_WEB_SHELL`. `ps2web.wasm` 24,731,032 bytes (5,627,599 gzipped), `ps2web.data` 4,073,838. Chrome at `http://localhost:8620/`: title "Burnout 3 in the browser", status `Disc 2.67 GB, GPU nvidia ampere`, `crossOriginIsolated` true, Start enabled; `pad.js` and `disc.js` served, `B3Pad` defined, no script errors. After Start: status `Running`, frame counter advancing, black canvas, no `[ee] halted` line (same state as headless run 002). DualSense check (Task 8 Step 6) waits for a menu. |
