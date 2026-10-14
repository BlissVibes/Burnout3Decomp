# Browser log 005: the VU thread and the graphics path, the audit's second round — 2026-10-14

Fork: `burnout3` at dbadb67 (five commits on 9431274, see `external/FORK.md`); page in `~/b3/out/web`, `ps2web.wasm` 24,953,170 bytes. Made by two agents, one per area, each in its own worktree, from the audit's queued findings (`docs/re/open-issues.md`, table 2); measured by the planner in Chrome with `speed.mjs` as in `web-log-003.md`, each change against itself switched off by a `PS2_` parameter.

**After `web-log-004.md` the VU thread was the page's limit: about 20% idle over the race and 10% in the takedown, where the emulation thread had to wait for room in the VU ring. Now it is 30% idle over the race and 21% in the takedown, the VU ring never fills, and Chrome as a whole uses 1.8 cores where it used 2.1. The native race is unchanged: 69,308,449,700 cycles and 72,746,114 primitives at frame 14100.**

## The VU1 JIT (fork e9f1aac, with ec3fe90)

- Every recurring 128-bit constant is set once per generated function instead of being rebuilt at each use (finding 0).
- The exact adder takes a cheaper form (finding 43) and now serves SUB, MADD and MSUB as well as ADD, which closes the half of gap 013 that was left open in the JIT (`gap-013-vu-adder-host-rounding.md`).
- FTOI, ITOF, CLIP, OPMULA and OPMSUB are emitted inline with the same bits instead of calling a helper lane by lane (finding 6).
- A Q or P result still pending at a subroutine return is followed to the instruction after the call (finding 45, return points only).
- `vu_ops.hpp`'s exact add had a hole when an operand's magnitude was `0x7F7FFFFF`: `vuops::add(0x73C00000, 0xFF7FFFFF)` gave `0xFF7FFFFE`, the hardware rule `0xFF7FFFFD` (found by the VU0 work). Fixed in the C++ paths and absent from the JIT's new form.

Measured on 6,657 VU1 calls recorded from the race (every 200th call, frames 12300 to 13400), replayed through the page's JIT under node: 24% less time with all parts on; with them, vf, ACC, VI, Q, P, I, clip and memory equal the exact C++ paths on every call (at 9431274 the JIT differed in 5,834 of the 6,657). MAC and status flags still differ in 1,024 calls, as before: the JIT leaves out flags no instruction reads.

In Chrome, the same page with `?PS2_VU_JIT_OFF=consts,exact,inline` against all parts on, two runs each:

| | Parts on | Parts off |
|---|---|---|
| VU thread idle, race | 26.0%, 25.7% | 22.3%, 21.6% |
| VU thread idle, frames 13050 to 13400 | 16.3%, 15.7% | 12.3%, 11.8% |

## The graphics path (fork dbadb67, 01ecfae)

- The VU thread writes GIF data into the render ring as it comes and makes it visible in batches (at any other message, every 128 KiB, and before it sleeps) instead of one message and one wake per piece (findings 7, 20, 42). XGKICK hands VU memory on without gathering it into a buffer first.
- The ring's reader now sleeps on its own waiting flag. The wake a poster sends can no longer be missed: this is the cause of the lost wakes behind the freeze of `web-log-004.md` section 7. The 2 ms limit on the poster's wait stays as a guard.
- In the page, a PATH3 packet released by MSKPATH3 now reaches the GS before the rest of the VIF1 chunk that released it, as natively (finding 39).
- The main loop no longer reads the clock around each wait (finding 30).
- The speed blur's strips sample the frame buffer they draw into. Such a primitive is now drawn alone, from a copy of the memory it reads (finding 17), about 103 a frame. GPU frames are repeatable between runs (20 of 20 dumps equal, were 2 of 20). The blur's pixels change by a step or two, now reading strictly the old memory as PCSX2's hardware renderer does; not compared with a console.

In Chrome, the same page with `?PS2_RING_EACH=1` (one message per piece, as before) against the default:

| | Default | One message per piece |
|---|---|---|
| All of Chrome, cores | 1.83, 1.83 | 2.08 (2.47 in a run that slowed to 43 to 54 fps in the takedown) |
| Main thread idle, race | 58.3%, 58.6% | 51.6% |
| VU thread idle, race | 25.5%, 26.7% | 23.1% |
| Poster waits for room in the VU ring | 0 | 482 |
| Lost wakes | 0 | 4 |

## Both together

The page of dbadb67, two runs: 60.0 fps, no JavaScript error, 256 MB.

| | Run 1 | Run 2 | This morning (`web-log-003.md`, before any of the day's work) |
|---|---|---|---|
| Emulation thread idle, race | 52.5% | 52.3% | 9.1%, 10.5% |
| lowest one-second sample | 46% | 45% | 0% |
| VU thread idle, race | 29.6% | 30.8% | 19 to 22% |
| VU thread idle, frames 13050 to 13400 | 20.5% | 21.2% | 8 to 10% |
| Main thread idle, race | 57.7% | 58.8% | 53 to 57% |
| All of Chrome, cores | 1.77 | 1.81 | 2.54 |
| WebAssembly memory | 256 MB | 256 MB | 307 MB |

The GPU reads 73% busy on this machine (it was 64 to 68%): the blur's copies, and frames that are drawn sooner. It is now the busiest part.

## Not done

- The VU thread's relay copy of PATH3 data (about 1.6% of it): removing it means the render thread reads from the VU ring, which both auditors advised against for now.
- The main thread's per-primitive costs (1 to 2%).
- Fewer compute dispatches (finding 18); the blur's copies add about 103 a frame.
- A JR to an in-region target that is not a return point is still not followed for Q and P.
- Nobody has looked at the blur against a console.
