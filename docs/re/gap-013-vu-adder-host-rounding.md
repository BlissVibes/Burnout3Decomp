# Gap 013: the fast VU paths add as the host rounds, not as the VU adder does, so sparks, dust and debris were drawn as white sheets

Date: 2026-10-13. By Opus 2, reviewed by Opus 1. Found on the corrected race (after gap 012) while looking at the remaining oddities. Analysis and fix in the worktree `~/b3/ps2-gs008` (branch `gap008`). The C++ paths have the whole rule; the WebAssembly JIT has it for the ADD family only, because the whole rule costs the VU thread its margin in the page (see "Cost in the page"). Frames and measurements are under `~/b3/out/odd01/`; nothing from the game is in this repository.

## Symptom

In the corrected race, wherever the game draws sparks, dust, smoke or debris:

- a white haze over the whole view with ghost images in it (frame 12900);
- a tall white column in the road (13050);
- in the crash cinematic of a rival, a white sheet behind the car (13150) and very large white, cyan and orange shards and flames (13200);
- a cyan flash and then a nearly white screen for a few frames while trading paint (13340, 13350), and the same at 13800.

The HUD is not affected. These are the "white-out during a SLAM" and part of the "red crosses in front of buildings" of the list in `gap-010`: the crosses seen through a shop front at 12900 were ghosts in the haze.

## What the Xbox reimplementation and the PS2 emulators say

reB3 (`docs/RE_POSTFX.md` section 4d, `RE_TAKEDOWN_FX.md` section 3): retail has no flash on a slam, a crash or a takedown, and changes no post-processing for them; a slam is a camera shake, a sound and a boost award. Its particle notes (`RE_MASTER.md` section 9) say particle quads are sized on screen and that the size is clamped. So a white screen is not intended. It has nothing on VU arithmetic.

PCSX2, read as Mark asked for each graphical fault (its per-game database entry for this game and the comments of its two Burnout hooks in the hardware renderer; behaviour only, row in `docs/research/PROVENANCE.md`):

- Its VU recompilers run with the rounding mode set to chop, and the per-game database has rounding and clamping overrides for this family: Burnout 2 has a VU1 rounding override commented "Bright lights in cars", and Burnout 3 has the VU clamp mode "extra + preserve sign" commented "Fixes buggy lighting in the garage". The same kind of symptom from the same kind of cause.
- The rest of the entry describes techniques that a memory-accurate GS model handles by itself, and ours is one: the bloom is built column by column in a spare page, thresholded by blending the frame into a page cleared to 191 with twice the difference, with the next column's page cleared by writing depth values into it; the sun's glare is hidden behind objects through a palette that the GS draws as points; the glass-smash texture and the clouds are drawn into the alpha channel of a frame early and used as 8-bit textures later. These were read as possible causes of the white-outs and ruled out by the test below (the CPU rasteriser and the GPU renderer agree).

## Cause

Narrowed with frames from the race02 snapshots:

1. The CPU rasteriser and the GPU renderer draw the same white frames. Not the renderer.
2. With the exact VU interpreter (`PS2_VU_EXACT=1`) every one of the symptoms is gone. With the recompiled VU1 programs (the default) and with the fast interpreter (`PS2_VU_INTERPRET=1`) they are all there. The exact interpreter computes with the runtime's model of the hardware (`fp::add`, `fp::mul`); the recompiled programs, the fast interpreter and the WebAssembly JIT compute with host floats.
3. Setting the host rounding mode toward zero for the fast interpreter (an experiment, native only) removes them.
4. Making one kind of instruction exact at a time in the fast interpreter: addition alone removes them; subtraction, multiplication, multiply-add, multiply-subtract and the outer product alone do not.

The VU has one adder. ADD, SUB, the forms that write ACC, MADD and MSUB (the product goes through the adder with ACC) and OPMSUB all use it; SUB is the adder with the second operand's sign changed. It is not an IEEE adder: it keeps one guard bit. The smaller operand is cut off below half a unit in the last place of the larger, the two are added or subtracted, and the result is truncated. `fp::add` is written that way. A host adds exactly and rounds to nearest, which differs by one unit in the last place in a large share of sums.

One unit in the last place is enough because programs use the adder to make integers: adding a large constant to a value and taking it away again drops the fraction. On a VU that gives the integer toward zero; on the host it gives the nearest one, which is one more for any fraction above a half. Which of the game's VU1 programs does this, and with which value, was not traced: the test in step 4 identifies the instruction kind, not the program. The effect is consistent with a particle's size or frame index coming out one step too large.

## Fix (generic, in the fork: `runtime/include/ps2/vu_ops.hpp`, `runtime/src/vu_micro.cpp`, `runtime/src/vu_jit.cpp`)

WebAssembly has no rounding mode to set, so the host sum is kept and corrected by at most one unit in the last place. The exact error of the host sum comes from a two-sum (six float operations, exact under round to nearest). With "away" meaning the host sum was rounded away from zero:

- same signs: the adder truncates the exact sum, so the sum is stepped back if away;
- opposite signs, exact difference below the larger operand's power of two: the bits the adder dropped from the smaller operand make its difference too large, so the sum is stepped forward unless away;
- opposite signs otherwise: the adder gives the nearest value, and the lower one on a tie, so the sum is stepped back if away on a tie.

A first version that only truncated the exact sum (the first case applied to both signs) fixed the picture but did not match `fp::add`: it disagreed in 19 percent of random sums and would be a whole unit off when a large constant is added to a negative value. The three cases above match `fp::add` exactly.

The same correction is in the three places that compute with host floats: the scalar and four-lane helpers the recompiled VU programs use (`vu_ops.hpp`: add, sub, madd, msub and their vector forms), the fast interpreter (`vu_micro.cpp`), and the WebAssembly JIT (`vu_jit.cpp`, the same steps emitted as SIMD instructions).

The JIT applies the correction to the ADD family only: ADD with a vector, a broadcast lane, I or Q as the second operand, and the forms that write ACC. SUB, MADD and MSUB use the same adder on a VU and are corrected in the C++ paths, but in the JIT they keep the host's rounding for now. This is not the whole rule. It is a strict improvement: every sum that is corrected is the VU's sum, and none of the others is worse than before. The ADD family is what the visible fault needs (step 4 above), and it is about a third of the adder's use: of the upper instructions of a race stretch that use the adder, ADD is 32 percent, SUB 8, MADD 53, MSUB 5 and OPMSUB 1. The rest waits for room on the VU thread.

### Not covered

Multiplication stays as the host rounds it. The VU multiplier truncates too (`fp::mul`), so products in the fast paths can still differ from the hardware's by one unit in the last place. No effect of that was seen, and correcting it costs more than the adder.

### What this changes elsewhere

Every game, VU0 and VU1 microcode, on the fast paths; the exact interpreter and VU0 macro mode already used `fp::add`. No saved-state field changes and `kStateVersion` stays as it is. Recompiled VU programs do not have to be regenerated: the change is in the helpers they call.

## Verification

- `tests/unit/test_vu_adder.cpp` (new): the scalar and four-lane helpers against `fp::add` and `fp::sub` on 320,028 checks (the drop-the-fraction case, random pairs at five exponent spreads, a multiply-add whose product is exact). 68,119 fail with the header as it was, none with the fix. `vu_ops.hpp` is written for clang, so the test reports itself skipped under another compiler. The JIT's emitted steps are not covered by it.
- Native, race02 states, CPU and GPU renderers: frames 12900, 13050, 13150, 13200, 13340 and 13350 in the default mode and with the fast interpreter are free of every symptom and look like the exact interpreter's (small sparks, small debris, no haze). They are not identical to it pixel for pixel: multiplication still differs and the blur carries small differences from frame to frame.
- Page built from the worktree: boots, reaches the race, runs to 14100, status "Running", no JavaScript error.

## Cost in the page

Chrome, the corrected race, quiet machine (load average under 0.2), no screenshots, frames 12650 to 14100:

| Page | Race fps | 13050 to 13400 fps | VU thread idle | Emulation thread idle | Audio played with an empty queue |
|---|---|---|---|---|---|
| without the fix (fork b2c8a15) | 60.0 | 60.2 | 27.4% | 8.8% | none |
| fix, every correction always emitted (two runs) | 57.0, 58.2 | 51.6, 53.1 | 4.0%, 3.9% | 15.0%, 12.7% | 1.3 s, 0.8 s |
| fix, corrections behind two tests (two runs) | 55.5, 54.5 | 49.9, 48.4 | 2.1%, 3.1% | 16.2%, 18.0% | 2.0 s, 2.5 s |
| fix for the ADD family only, as proposed (two runs) | 59.1, 60.0 | 57.9, 59.9 | 17.4%, 19.6% | 5.6%, 8.0% | 0.5 s, under 1 ms |

The first two forms correct every instruction that uses the adder.
With either of them the VU thread has no idle time in the first twelve seconds of the race and the frame rate falls with it. Putting the corrections behind tests did not help. Counted in the fast interpreter over 200 million add-type instructions of the race: 45 percent have no inexact lane, 55 percent have one, 36 percent have an inexact lane with opposite signs, and 6.6 percent need one of the two uncommon corrections; so the tests pass more often than not and the two-sum is paid by every add.

With the ADD family only, the race holds 60 on a quiet machine (the second run; during the first another emulator run was going, load average 3). The VU thread keeps 17 to 20 percent idle over the race against 27 without the fix; in the heaviest stretch, the rival's crash cinematic (13050 to 13400), it is down to 8 to 9 percent against 17. The screenshots of that page (`~/b3/out/odd01/web/gap013-addonly/`, frames 12625 to 14000) show none of the symptoms: no haze at 12900, small debris in the cinematic at 13200, no white at 13800.

## Status

Fixed in the fork in two commits on `burnout3`: `a765dc8` (the whole rule in the C++ paths: `vu_ops.hpp` and `vu_micro.cpp`) and `f2f31d2` (the ADD family in the WebAssembly JIT, `vu_jit.cpp`). They are separate so that the JIT part can be taken back on its own. The decision to take the JIT part with VU-thread idle at 8 to 9 percent in the crash cinematic: the quiet run holds 59.9 to 60.0 fps there with the picture right, and without it the page shows the white sheets. Patch as reviewed: `~/b3/out/odd01/gap013-vu-adder.patch`.

Open: SUB, MADD and MSUB in the JIT, which need either a cheaper correction or more room on the VU thread; and the multiplier everywhere on the fast paths. VU0 is not affected: its microprograms always run through the exact loop (see `docs/re/web-log-002.md` for what that costs).
