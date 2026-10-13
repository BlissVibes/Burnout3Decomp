# Browser log 002: speed of the corrected race, and where the emulation thread's time goes — 2026-10-13

Fork: `burnout3` at b2c8a15, page in `~/b3/out/web` (`ps2web.wasm` 24,845,564 bytes, generated code with the hook check of `docs/re/harness-log-003.md`). The race is the corrected one (after gap 012). Browser runs by Opus 2 (Chrome, muted, faked pad with presses timed inside the page, a separate profile); analysis and native counts by Opus 1. Data under `~/b3/out/odd01/web/` and `~/b3/out/prof01/`.

**On a quiet machine the corrected race holds 60 frames a second in Chrome, with little to spare on the emulation thread. Half of that thread's time is the game's VU0 microprograms, which run in the slowest interpreter the runtime has: 285,000 VU0 instructions a frame at about 25 ns each.**

## Speed

`speed.mjs`, no profiler, load average under 0.2, race window frames 12650 to 14100:

| Frames | fps (wall) | Emulation thread idle | VU thread idle | GS (main thread) idle | Audio played with an empty queue |
|---|---|---|---|---|---|
| 12650 to 14100 | 60.0 (lowest sample 58.8) | 8.8% (0 to 19) | 27.4% | 58% | none |
| 12650 to 13050 | 60.1 | 3.9% | | | |
| 13050 to 13400 | 60.2 | 2.8% | | | |
| 13400 to 14100 | 60.0 | 14.7% | | | |

An earlier measurement the same night gave 51 to 59 fps with the audio queue running dry. That was the machine: eight native emulator runs were going at the time (load average 9 to 35). It is superseded by the table above. What it does show is how the page behaves when the emulation thread cannot keep up: frames drop and the audio queue empties, so the sound crackles.

Against `docs/re/web-log-001.md` (the broken race: 58 to 64 fps, emulation idle 4 to 16% at the busy samples): the corrected race simulates six cars and the traffic on the road, and the margin is thinner.

The check that generated code now makes at every function entry (one global load and a branch) is in this page; it was not measured on its own.

## Where the time goes (Chrome's profiler)

`prof.mjs` (`~/b3/out/prof01/web/`) drives to the race like `speed.mjs`, attaches to the page's workers over the same debugging connection and runs Chrome's CPU profiler on every thread for frames 12650 to 13400 (13.0 s, 57.6 fps while profiling). The fork already links the page with function names. Four profiles: the page's main thread, two pthreads and the audio worklet.

Emulation thread (EE, IOP and everything they call directly):

| Share | What |
|---|---|
| 37.0% | `ps2::vu::run`, the VU interpreter loop |
| about 11% | the rest of the VU and VIF code on this thread (VIF transfer and unpack, other VU helpers) |
| 19.6% | generated EE code (the game itself), the largest unit 3.2% |
| 8.1% | `ps2::dispatch` |
| about 12% | GIF packet handling: `ps2::gif` 6.3%, a byte-vector insert 4.1%, queue post 1.5% |
| 2.7% | SPU2 mixer (with reverb and the four-point interpolation) |
| 2.0% | IOP interpreter |
| 2.4% | EE memory access helpers that were not inlined |

VU thread: 49.5% in code the VU JIT generated, 17.7% waiting for work, GIF transfer 7.9%, VIF about 5%, the JIT's helper functions about 12% (fixed-point conversion, divide, clip, kick).

Main thread: 31% in the GS and WebGPU code of the runtime (vertex assembly 12.6%, the GPU back end 10%, GIF render 4.5%), the rest in the browser.

## The VU0 microprograms

`vcallms` always runs VU0 code through `vu::run`, the exact loop: each instruction is decoded every time, and every add and multiply is done lane by lane with the integer routines of `fpu.hpp`, which reproduce the VU's own rounding. Only VU1 has the faster paths (recorded programs compiled ahead, the JIT, the fast interpreter).

Counted natively over the same 750 frames with a temporary counter (kept as a patch beside the data, not in the fork):

- 3,542,778 calls and 214,109,149 instructions: 4,724 calls and 285,479 VU0 instructions a frame, from 22 entry points.
- Four entries make up 71% of the instructions: `0x0138` (28.3%, 513 calls a frame, 157 instructions each), `0x0A18` (22.9%, 908 calls, 72 each), `0x0000` (10.1%), `0x0028` (9.5%). The segment-against-triangle test of gap 012, `0x03D8`, is 4.9% (315 calls a frame).
- Natively the loop takes 25.0 ns an instruction, 7.1 ms a frame (measured with the CPU rasteriser's threads busy on the same machine, so somewhat inflated). In the page, 37% of a thread that is busy for nearly the whole 16.7 ms frame is about 6 ms, or 21 ns an instruction: the same cost.

So the cost is the arithmetic, not WebAssembly: four lanes of integer float emulation for each instruction.

## What could be done about it

Not started; listed with what each would need.

1. Exact arithmetic done with the host's floats plus a correction, in place of integer emulation: the corrected add of gap 013 (host sum, then at most one unit in the last place from the exact error term) and a multiply that takes the product in double precision, where it is exact, and truncates. Then VU0 can run on the fast interpreter's decoded instructions with the same results bit for bit. This needs a differential test against the integer routines and a check on real calls (run both loops on each `vcallms` and compare the VU0 registers, as shadow mode does for hooks). Expected gain: most of the 37%.
2. Decompiled replacements for the hot callers. The function that wraps `0x03D8` is known (`0x00274C90`); the callers of `0x0138` and `0x0A18` are not identified yet. A hook removes the interpreter for that call entirely, and the hook mechanism now reaches direct callers.
3. Smaller items on the same thread: `ps2::dispatch` at 8% and the GIF packet copy at 12% are both runtime overhead, not game work.

Not measured: what the SPU2 changes of this week cost before and after (the mixer is 2.7% of the thread now).
