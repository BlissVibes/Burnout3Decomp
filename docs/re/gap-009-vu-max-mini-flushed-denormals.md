# Gap 009: MAX and MINI flushed denormals, so a VU1 program's GIF tag lost its register list and the cars were not drawn

Date: 2026-10-12. Found in headless run 011 (`docs/re/boot-log-011.md`) as the second fault of the race picture; gap 008 is the first. Analysis, fix and verification: Opus 2, in the worktree `~/b3/ps2-gs008` (branch `gap008`).

## Symptom

With the textures of gap 008 fixed, the player's car, the rival cars and the traffic were still not drawn. Their shadows were. Turning off the depth test, the alpha test, the destination-alpha test, blending or texturing in the CPU rasteriser (temporary switches) did not make a car appear, so the geometry was not being rasterised at all.

## Cause

A tally of what each VU1 program sends to the GS showed one program, entered at microprogram address `0x14E0`, that ran 446 times per frame and kicked 222,690 qwords in three frames without producing a single vertex. Its packets looked like this (first qword, most significant word first):

```
3F800000 00000000 302E4000 00000034
```

That is a GIF tag with NLOOP 0x34, PRE set, PRIM 0x5C (textured, shaded, blended triangle strip), packed format and NREG 3, whose register list (the upper 64 bits) is 0 in its low word. Three registers of number 0 means PRIM, PRIM, PRIM: each vertex's ST, RGBAQ and XYZF2 qwords were written to the PRIM register and nothing was drawn. The working programs send the same tag with the register list `0x412` (ST, RGBAQ, XYZF2).

The program builds the tag itself. Tracking which instruction last wrote each word of the tag gave the store at microprogram address `0x0158`, and the code before it:

```
0130: upper 80200040  lower 302E4000     I bit set: the lower word is loaded into I
0138: upper 8080005D  lower 00000412     MAXi vf1.y, vf0, I   with I = 0x00000412
0140: upper 8040005D  lower 477FFF00     MAXi vf1.z, vf0, I   with I = 65535.0
```

`MAXi` against `vf0` (whose x, y and z are 0.0) with an integer in the I register is how the program puts the integer `0x412` into a float register: as a float, `0x00000412` is a denormal. On the VU (and on the EE's FPU) MAX and MINI do no arithmetic. They compare the two bit patterns as sign-magnitude integers and return one of them untouched, so `max(0.0, 0x00000412)` is `0x00000412`.

The fork's `fp::max` and `fp::min` compared through `fp::key`, which flushes denormals to zero first, as the adder and multiplier do. Both operands then compared equal and the first one, the zero from `vf0`, was returned: the register list became 0. The same functions serve the VU interpreter, the compiled VU programs (`vmax`/`vmin` in `vu_ops.hpp`), the WebAssembly VU JIT and the EE's `max.s`/`min.s`.

Ruled out on the way: the VU0 microprograms loaded through VIF0 (not involved in this program's output), depth, alpha and blending state (see Symptom), and missing textures (no car texture was ever set, because the tag never reached TEX0-using vertices).

Corroboration: PCSX2's interpreters implement both the VU and the FPU instructions with an integer comparison of the raw patterns and no denormal handling (`fp_max`/`fp_min` in `pcsx2/VUops.cpp` and `pcsx2/FPU.cpp`, read 2026-10-12, knowledge only; row in `docs/research/PROVENANCE.md`).

## Fix (generic, in the fork: `runtime/include/ps2/fpu.hpp`, `runtime/include/ps2/vu_ops.hpp`, `runtime/src/vu_jit.cpp`)

- `fpu.hpp`: new `fp::order(x)`, the pattern as a sign-magnitude integer (`x` when the sign bit is clear, `x ^ 0x7FFFFFFF` when it is set). `fp::max` and `fp::min` compare with it. Nothing is flushed: a denormal is above zero and keeps its bits, and -0 is below +0. `fp::key`, which the comparison instructions use, is unchanged.
- `vu_ops.hpp`: `vmax` and `vmin` use the vector form `vorder` in place of `vkey` (which had no other user and is removed).
- `vu_jit.cpp`: the WebAssembly JIT emits the same ordering (`vorder_of`: `v128.xor` with `0x7FFFFFFF` selected by `x < 0`) in place of the flushed key.

`kStateVersion` is unchanged (no state is involved).

### What this changes elsewhere

`fp::max` and `fp::min` are also what recompiled EE code calls for `max.s` and `min.s`, and what decompiled code calls through `ee::max`/`ee::min`, so both sides of the oracle change together. Results differ from before only when an operand is a denormal or when the operands are +0 and -0. `tests/unit/test_f32.cpp` in this repository pinned the old behaviour for the zeros; it now states the new one and adds a denormal test. It passes against the worktree and fails against a fork without this fix, so it has to be committed together with (or after) the fork change.

## Verification

1. Build `ps2run`, `vujit_dump` and `vurecomp` in the worktree: no warnings.
2. Race states `s012750.state` and `s013650.state`, 10 frames each: the player's car and the cars ahead must be drawn.
3. Menu state and cold boot as in gap 008.
4. `bash tests/unit/run.sh ~/b3/ps2-gs008` in this repository.

## Result (Opus 2, 2026-10-12)

Outputs under `~/b3/out/gap008/`.

- `both-12750/frame_12760.png` and `both-13650/frame_13660.png` (both fixes): the player's car is drawn with its livery and number plate, and rival and traffic cars are visible ahead. `PS2_GS=off` agrees (`both-soft-12750/`). The same frames with only gap 008 fixed are `after-12750/` and `after-13650/` (shadow only).
- Menu state: frames 3325 to 3400 byte-identical to the unfixed build.
- Cold boot, 3600 frames (`cold-menu-both/`): reaches the main menu; 1,210,406 primitives (control 1,211,906, the gap 008 build 1,212,031; noise about 1,250, so this run is 250 outside the quoted band, with identical GIF tag counts, uploads, CLUT loads and raster passes); frames 300, 600, 1200, 2100, 2700, 3300 and 3600 do not differ from the gap 008 build's by more than 8 in any channel of any pixel.
- Unit tests: 21 programs pass against the worktree.
- Not run: the WebAssembly JIT path (`vu_jit.cpp`) is only executed in the browser build; the change was compiled natively and needs one browser run after the next `build-web.sh`.

## Still wrong after both fixes

A patch on the player's car, on the rear window and roof, shows blocks of saturated green, yellow and orange where a reflection should be. It was the only fault left in the two frames looked at; it is `docs/re/gap-010-vu-q-and-p-result-latency.md`.

## Status

Fixed in the worktree `~/b3/ps2-gs008` (branch `gap008`), uncommitted; the three files of this gap are also saved as `~/b3/out/gap008/gap009-maxmini.patch`.
