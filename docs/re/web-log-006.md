# Browser log 006: generated code and page memory, the audit's third round — 2026-10-14

Fork: `burnout3` at 9ca5587 (five commits on dbadb67, see `external/FORK.md`); generated code regenerated with that recompiler; page in `~/b3/out/web`, `ps2web.wasm` 24,327,378 bytes (5,187,656 gzipped). Made by two agents in their own worktrees from the audit's last queued findings (`docs/re/open-issues.md`, table 2); measured by the planner in Chrome with `speed.mjs`.

**The page is 626 KB smaller (510 KB less to download), starts with 192 MB of memory and stays there (it took 256 MB, and 307 MB the day before), and the emulation thread gained two to four more points of idle time. The native race is unchanged: 69,308,449,700 cycles, 72,746,114 primitives and the same audio at frame 14100.**

## Generated code (fork e0ba647, 9ca5587)

- **Regenerated.** The shared generated tree predated fork f79a015: 1,819 functions known only from an address constant or a data word now have the hook check (7,932 in all). The regenerated tree differs from the old one in exactly those 1,819 switch arms and the table format below; the planner's regeneration from the committed recompiler is identical to the agent's.
- **Guest memory access inline in the hot units (finding 14).** A unit's entry switch made the compiler treat every load and store as cold and call the helper. The 33 busiest units (the race profile's top 30, and three from native profiles of the menus) now get the helpers' fast path inline: no ld/st helper call is left in them. `tools/units.txt` holds the lists; the fork only offers the mechanism (empty lists change nothing).
- **Cold units compiled for size (finding 25).** 116 units that take 0.35% of the race's emulation thread are compiled with `-Oz`; 76 warm units stay at `-O3`. The training video was not profiled, so units that are busy there may sit in the cold set; the menus in Chrome are not slower (below).
- **Compact entry tables (finding 27).** 225 tables of 4,096 entries, mostly zero, were written out in full: 18,406 data segments and a 542 KB function to set them up. Each is now filled at start-up from a list of its entries: 29 segments, the set-up function 830 bytes.
- **COP2 macro arithmetic on the exact host-float forms (finding 15).** The EE's own VU0 macro instructions take the four-lane forms of `vu_exact.hpp`, bit-identical to the integer routines (tested against them, natively and in WebAssembly), about twice as fast under node. COP1 stays on the integer routines: the exact forms were slower for one lane.

Size of the page at each step: 24,953,170 bytes before; +47 KB for the regenerated checks; +1.21 MB for the inline vector code of COP2; -1.35 MB for the tables; +2.40 MB for the inline memory access; -2.93 MB for the cold units; 24,327,378 in the end.

## Page memory and the blur (fork c9e15c3, f5e47f1, 0baf222)

- **Stacks** from their measured use with a margin: main 1 MB (2,208 bytes used), VU thread 1 MB (2,800), emulation thread 8 MB (29,696 used; nested generated calls and handler loops can reach about 6.7 MB, so it keeps more). Each was 16 MB.
- **Disc cache** 96 chunks (24 MB) instead of 256 (64 MB). `ps2run --disc-log` logged the route's 5,464 chunk reads (598 distinct chunks); `scripts/disc_replay.cpp` replayed them against the page's cache at several sizes: 96 slots miss 6 more times than 256, all in the menus. In Chrome the disc waits over boot and the menus went from 130 to 242 ms in all, race loading from 50 to 66 ms, the race from 8 to 5 ms.
- **INITIAL_MEMORY** 192 MB (was 256), growth kept as a safety net. The heap's top after a race is 167 MB.
- **Blur strips that do not read each other share one copy and one dispatch** (finding 18, in part): about 48 fewer GPU dispatches a frame, frames byte-identical.
- Finding 21 (the main thread's per-primitive costs) was measured at 0.005 ms a frame and left.

## Chrome

Page of 9ca5587, two runs; the page before this round (dbadb67) for comparison, two runs each, same machine, same evening.

| | This round | dbadb67 |
|---|---|---|
| Emulation thread idle, race | 52.9%, 54.6% (codegen page alone: 54.8%, 54.9%) | 52.5%, 52.2% |
| lowest one-second sample | 48%, 50% | 47% |
| Emulation thread idle, menus and the video | 74.3%, 74.1% | 70.4%, 72.6% |
| VU thread idle, race / frames 13050 to 13400 | 29.3% / 19.7%, 30.7% / 20.7% | 30.5%, 29.3% |
| Main thread idle, race | 57.3%, 59.0% | 57.1%, 57.7% |
| All of Chrome, cores | 1.79, 1.76 | 1.85, 1.82 |
| WebAssembly memory | 192 MB throughout | 256 MB |
| ring lost wakes / poster waits for room | 0 / 0 | 0 / 0 |

All runs held 60 fps with no JavaScript error and no audio played from an empty queue in the race.

## Where the two days leave the page

| | 2026-10-13 morning (`web-log-003.md`) | Now |
|---|---|---|
| Emulation thread idle, race | 9 to 10%, 0% at the worst second | 53 to 55%, 48% at the worst second |
| VU thread idle, race / takedown | 19 to 22% / 8 to 10% | 29 to 31% / 20 to 21% |
| All of Chrome, cores | 2.54 | 1.76 to 1.79 |
| Memory | 307 MB | 192 MB |
| Download (wasm gzipped) | 5.68 MB | 5.19 MB |

## Not done

- The training video and the other game modes have no profile; the hot and cold unit lists come from the race and the menus.
- The GPU is now the busiest part (73 to 75% in the race on this machine). Nothing here measures its passes.
- The page's executable still comes in `ps2web.data` (`PS2_WEB_ELF_FROM_DISC` is built in and off).
- The VU thread's relay copy of PATH3 data.
- Pushing the fork and this repository: Mark's decision.
