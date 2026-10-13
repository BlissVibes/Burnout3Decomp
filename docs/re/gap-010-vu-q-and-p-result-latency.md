# Gap 010: VU divide and EFU results appeared at once, so the car's reflection coordinates were garbage

Date: 2026-10-12. Found after gaps 008 and 009 as the last visible fault on the player's car. Analysis, fix and verification: Opus 2, in the worktree `~/b3/ps2-gs008` (branch `gap008`, on top of c7abe06 and 1334998).

## Symptom

With gaps 008 and 009 fixed the cars were drawn, but on the roof and rear window of the player's car there were blocks of saturated green, yellow and orange where a reflection of the sky should be.

## Cause

The reflection is a second texture pass over the car with a 256 by 256 environment map at GS block `0x2D60`, which the game renders every frame from three 128 by 128 views of the scene. Only the inscribed disc of that map is drawn. The memory under its top corners is also the depth buffer of the three views (`ZBUF` at `0x2D60`, `0x2E60` and `0x2F60`), so the top corners hold depth values, which look like coloured noise, and the bottom corners are black. That is harmless as long as the reflection coordinates stay inside the disc.

They did not. Of 15,891 vertices drawn with the map in one frame, 12,056 had coordinates outside the disc, with values from -800 to 1e20. The coordinates are not computed by the program that draws the car. A separate VU1 program (around microprogram addresses `0x0B70` to `0x0D90`, one of the programs that kick nothing) computes them for four vertices at a time and stores them back into VU memory. It is scheduled to the cycle around the divider and the EFU (the listing is not kept in this repository; microprogram addresses below):

- It starts an ESQRT at `0x0BC8` and a SQRT at `0x0BD0`, and reads Q at `0x0BF0`, four instructions after that SQRT: it wants the previous Q.
- It starts another SQRT at `0x0C08` and reads Q at `0x0C10`, one instruction later: it wants the result of the SQRT at `0x0BD0`.
- It reads P at `0x0C20` and `0x0C28`, 11 and 12 instructions after the ESQRT: the first wants the previous P, the second the ESQRT's result.
- It starts an ERSQRT at `0x0C30` and an RSQRT at `0x0C40`, and reads Q at `0x0C48`, one instruction after the RSQRT: it wants the result of the SQRT at `0x0C08`.
- It reads P at `0x0CC0`, 18 instructions after the ERSQRT: it wants that result.

On the VU, DIV and SQRT deliver Q seven instructions after they start and RSQRT thirteen; the EFU instructions deliver P after 11 to 54. Until then Q and P hold the previous result, and this program reads them in that window on purpose. The fork's interpreter wrote Q and P the moment the instruction executed, so each read above got the result of a later operation than the one intended, and square roots, reciprocal roots and unrelated vertices were mixed.

Ruled out on the way: the environment map itself (its disc is a correct fisheye view of the scene), the clamp mode of the reflection pass, and the I register: `LOI` values are used by the instruction after the one that carries them, which the interpreter already does and which gap 009's tag (`0x302E4000` in y, `0x412` in z) confirms.

Corroboration for the numbers: PCSX2's VU interpreter uses the same latencies (`pcsx2/VUops.cpp`: 7 for DIV and SQRT, 13 for RSQRT; ESADD 11, ERSADD 18, ELENG 18, ERLENG 24, EATANxy/xz 54, ESUM 12, ERCPR 12, ESQRT 12, ERSQRT 18, ESIN 29, EATAN 54, EEXP 44) and stalls a new divide until a pending one has delivered. Read 2026-10-12, knowledge only. The program above pins four of them by itself: SQRT at most 7, RSQRT at most 13, ESQRT exactly 12 and ERSQRT at most 18.

## Fix (generic, in the fork: `runtime/src/vu_micro.cpp`)

A small `Delayed` record in both interpreter loops (`run_fast`, the default for VU1, and the exact loop that also runs VU0 microprograms):

- when DIV, SQRT, RSQRT or an EFU instruction has executed, its result is taken back out of `q` or `p`, the old value is restored, and the result is delivered at the start of the instruction `latency` places later;
- WAITQ and WAITP deliver at once, and so does the end of the program;
- starting another operation of the same kind while one is pending delivers the pending result first (the hardware stalls for it).

Stalls from register hazards between ordinary instructions are not modelled, so latency is counted in instructions, not cycles. `kStateVersion` is unchanged: nothing is pending between programs.

### Not covered

- The WebAssembly VU JIT (`vu_jit.cpp`, browser build only) and the ahead-of-time VU recompiler (`vurecomp`, no program is compiled for this game: `[vu1] compiled 0`) still deliver Q and P at once. In the browser the reflection will stay wrong for any program the JIT compiles until the JIT either models the delay or leaves programs that read Q or P inside a latency window to the interpreter. Not attempted here: it cannot be tested without a browser build.
- VU0 macro mode (COP2 instructions on the EE) is unchanged.

## Verification

1. Build `ps2run`: no warnings.
2. Race states `s012750.state` and `s013650.state`, 10 frames each, with the WebGPU renderer, `PS2_GS=off`, and `PS2_VU_INTERPRET=1 PS2_VU_EXACT=1`.
3. Menu state and cold boot as in gap 008.

## Result (Opus 2, 2026-10-12)

Outputs under `~/b3/out/gap008/`.

- A temporary count of the reflection pass's coordinates in one frame: 12,056 of 15,891 vertices outside the disc before, 0 after, with s in 0.00002 to 0.999999 and t in 0.00006 to 0.99994.
- `g10-12750/frame_12760.png`, `g10-13650/frame_13660.png`: the rear window and roof show a sky reflection and no coloured blocks. The software renderer (`g10-soft-13650/`) and the exact interpreter (`g10-exact-13650/`) agree.
- Menu state: frames 3325 to 3400 byte-identical to the unfixed build.
- Cold boot, 3600 frames (`cold-menu-g10/`): main menu reached; 1,213,031 primitives (control 1,211,906; the three cold boots of today gave 1,212,031, 1,210,406 and 1,213,031 with identical GIF tag counts, uploads, CLUT loads and raster passes, so the spread is run-to-run noise); frames 300 to 3600 do not differ from the two-fix build's by more than 8 in any channel of any pixel.

## What still looks wrong in the race (frames 12600 to 14100, all three fixes, one frame every 75)

Not investigated, and each needs a look at the real game before it is called a fault. Frames are in `~/b3/out/gap008/survey/`.

- 12675 to 12825: the "CRASHED!" banner is on screen while the car drives off normally at 57 to 81 mph. Either the state the run starts from had already crashed, or the wrong banner is shown at the start.
- 12675 and 12975: a row of three red crosses is drawn over the scene, in front of buildings; it may be a road-closed marker that should be hidden by them.
- 13050: the speed readout says 4205 mph for one sampled frame, just before the crash.
- 13125 to 13350, the crash sequence ("IMPACT TIME"): one frame is a view from inside a building's roof, and the next three show mountains over a dark blue faceted plane with no car and no road. The stunt text and banner are right.
- 13800: the whole 3D view is washed out to white during a "SLAM!" (the HUD is unaffected). It may be an intended flash.
- Throughout: strong radial blur even at 57 mph, and the boost bar at the bottom left is drawn as a dotted strip. Both may be right.

## Status

Fixed in the worktree `~/b3/ps2-gs008`, uncommitted, one file; also saved as `~/b3/out/gap008/gap010-vu-latency.patch`. Built binary: `build/gs008/ps2run-gap010`.
