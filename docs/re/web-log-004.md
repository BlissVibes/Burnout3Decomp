# Browser log 004: VU0 on a second, faster loop that leaves the same bits — 2026-10-13

Fork: `burnout3`, six files changed on top of a854891 in the main checkout, committed the same evening as d9ae500. The page builds with it (`ps2web.wasm` 24,913,657 bytes; before: 24,883,793). Sections 1 to 6 were written before it ran in a browser, so their WebAssembly figures are from node; section 7 has the run in Chrome, made the same evening with the audit's fixes on top. Done by a three-stage workflow (arithmetic, loop, measurement) on Opus, then reviewed, with one fix pass after the review; data under `~/b3/out/vu0/` (`a/`, `b/`, `c/`, `f/`). This is item 1 of `docs/re/web-log-002.md`, "What could be done about it".

**The VU0 interpreter now costs 10.2 ns an instruction natively where it cost 24.4: 2.4 times less, 7.0 ms down to 2.9 ms of emulation-thread time a frame in the race. In WebAssembly under node it is about 2 times less (fourteen runs between 1.82 and 2.05). The game cannot tell the two loops apart: both were run on every one of the 7,438,271 VU0 calls of the race and left the same registers, flags and memory each time, and the race is the same run with either (cycles, primitives, audio bytes). This is not "several times" cheaper. An interpreter with exact arithmetic stops near here; more needs compiled code per microprogram.**

## 1. What changed

VU0 has two loops now, in `runtime/src/vu_micro.cpp`:

- The **exact loop** is the old one, unchanged, under the name `run_exact`. It decodes every instruction as it meets it and computes each lane with the integer routines of `runtime/include/ps2/fpu.hpp`.
- The **fast loop**, `run_vu0`, runs instructions decoded once per code image and does the arithmetic on four lanes of host floats, with the routines of the new `runtime/include/ps2/vu_exact.hpp`.

`vu::run(0, ...)` picks between them, so `vcallms` and a VIF0 MSCAL share the choice. VU1 is not touched.

### The arithmetic

The routines give the bits of the integer model (`fp::add`, `fp::sub`, `fp::mul`), not the host's rounding:

- **Multiply.** The product of two floats has 48 significant bits and is exact in a double. The VU multiplier keeps the top 24 and drops the rest; clearing the low 29 bits of the double leaves the same 24, and what is left fits a float, so narrowing it does not round.
- **Add and subtract.** The host sum is the exact sum rounded to nearest; the VU adder truncates after cutting the smaller operand. The host sum is corrected by at most one unit in the last place from its exact rounding error, by the three rules of `docs/re/gap-013-vu-adder-host-rounding.md`. The operands are put larger magnitude first, so the error comes from three float operations and none of them can overflow.
- **What host floats cannot do.** Every routine returns, beside the result, a mask of lanes it could not compute: an operand or result with exponent field 255 (a number on a VU, an infinity or a NaN on the host), and one product next to the smallest normal number. The loop then computes that instruction with the exact loop's own routine. Ordinary numbers never take that path: in the whole race it was taken 0 times.

A multiply by the `w` of `vf0` (1.0) needs no multiply: `fp::mul(s, 1.0)` is `s` with a denormal flushed, for all 2^32 values of `s`. An instruction with one lane goes straight through the integer model.

The arithmetic needs a host that rounds to nearest and keeps denormals, and a compiler that does not fuse a multiply into an add. So `CMakeLists.txt` now passes `-ffp-contract=off` for both hosts, and when the mode is chosen a few sums and products are compared with the model; on a thread that fails (flush-to-zero set, for one) VU0 prints one line and stays on the exact loop.

### The loop

- Steps, their order, the flag pipeline, the delivery times of Q and P and every lower instruction are the exact loop's: the same `FlagPipe`, `Delayed` and `lower()`. Sixteen common lowers are decoded and run in the loop itself; the rest go through `lower()`.
- Decoded code is kept per code image, 16 images, most recently used first, found again by comparing the 4 KB of code. The search happens only after `vu::code_changed(0)`. The race uses 5 images and searches 17,370 times in 7.4 million calls.
- The exact loop sets `vf0` and `vi0` after every instruction. No instruction writes them, so the fast loop only checks them when a call starts; a call that starts with either wrong goes to the exact loop (0 in the race).
- Neither loop charges anything to the EE's cycle budget, before or after.

### Choosing the loop

Read from the environment at the first VU0 call:

| Setting | VU0 runs |
|---|---|
| none | the fast loop |
| `PS2_VU0_EXACT=1` | the exact loop |
| `PS2_VU_EXACT=1` | the exact loop, and VU1 its exact loop too (as before) |
| `PS2_VU0_CHECK=1` | both loops on every call, the whole state compared, the exact loop's outcome kept |
| `PS2_VU0_CHECK=live` | the same, the fast loop's outcome kept |

The check compares 596 bytes of registers (all of them up to the program counter), the 4,096 bytes of VU0 data memory, the number of instructions run, the EE's cycle budget, and whether the decoded image is still the code in memory. It prints the first 8 differences as `[vu0-check]` lines and a count at the end. Every run that called VU0 ends with a `[vu0] mode ...` line that names the loop it really used.

### Two smaller changes

- `slow_st128` (a 128-bit store by the EE into VU code memory) did not tell the VU that its code changed; it does now, as the narrower stores did. It was the only writer of VU0 code that did not.
- `tools/scan-log.py` knows the `[vu0-check]` lines; one that reports a differing call is a gap.

## 2. Is it the same?

### The arithmetic alone

`tests/unit/test_vu_exact.cpp`, 14 forms against `fp::add`, `fp::sub`, `fp::mul`, `fp::max` and `fp::min` on raw bits, made-up operands:

| Run | Result |
|---|---|
| Native, clang++ 18.1.3 `-O3 -ffp-contract=off`, 300 million lanes per form per stream and all 2^32 patterns of the times-one fold | 32,149,234,044 lane comparisons, 0 wrong |
| WebAssembly, em++ `-O3 -msimd128 -ffp-contract=off` under node v24.19.0, the same | the same count, 0 wrong; its 142 lines of output are the native run's |
| Against a stub that uses the host's own product and sum | 238 of 1,857 checks fail; 41% of ordinary products and 11% of ordinary sums are wrong |
| 30 one-line mutants of the header | all 30 fail the test |

The stream of ordinary numbers had 0 lanes handed back to the model in every form.

### The loop alone

`tests/runtime/test_vu0_loop.cpp` (976 checks) runs every call three times from one state, through the exact loop, the fast loop and the check, on made-up microprograms: every upper instruction under all 16 lane masks, flag readers at every distance from a setter, Q and P readers at every distance from a divide, the ends of a program, the decoded lowers at their edges, XGKICK, the entry guard, more than 16 code images, a code change nobody announced, one flipped bit in the fast loop's outcome, and generated programs. At scale 300 that is 1,800,000 generated calls and 54,692,440 instructions each way, natively and under node, 0 different. 49 one-line mutants of `vu_micro.cpp`: all 49 fail the test.

Those cases choose the loop through `set_vu0_mode`. `ps2run` and the page never do: they read the environment at the first VU0 call, and for the page that is its only way to the exact loop and to the check. The review found that nothing tested it. Now the test starts itself once for each of seven settings of the three variables (none; each switch for the exact loop; the check; the check that keeps the fast outcome; each check with an exact-loop switch beside it, where the check wins), with nothing else in the environment. Each run sets no mode itself and must show the mode it names and what only that mode does: 1,200 calls compared or none, no code image decoded by the exact loop, and one damaged outcome reported by both checks and kept only by the one that keeps the fast outcome. Nine one-line mutants of that reading (a misspelt variable, the two checks swapped, a switch ignored, the order turned, the environment never read): 8 of them passed this test, the only one built from `vu_micro.cpp`, before the case existed; all 9 fail now, natively and in the page build under node.

### The race

From the saved state at frame 12300, accelerate held from 12600, native:

| Run | Cycles | Primitives | Audio | VU0 |
|---|---|---|---|---|
| Fork at a854891 (before), race to 14100 | 69,308,449,691 | 72,746,114 | reference | |
| New, default | same | same | identical | fast loop: 7,438,271 calls, 446,283,649 instructions, 0 through the model, 5 code images |
| New, `PS2_VU0_EXACT=1` | same | same | identical | exact loop |
| New, `PS2_VU0_CHECK=1` | same | same | identical | 7,438,271 calls compared, 0 differ |
| New, to 12700, `PS2_VU0_CHECK=live` | 62,427,154,335 | 16,624,350 | identical to the default run's | 1,358,698 compared, 0 differ |
| Before and new, `PS2_VU_EXACT=1`, race | 69,308,449,691 both | 72,746,004 both | identical to each other | |

`PS2_VU_EXACT=1` is **not** the default run, before this change or after it: 110 primitives fewer over the race (77 in the window to 12700), same cycles, same audio. That switch also moves VU1 to its exact loop, and VU1's fast paths still multiply as the host rounds. The A/B for this change is `PS2_VU0_EXACT=1`.

The review ran the check over everything before the race as well, from a cold boot through the title, the menus, the training video and the loading screen to frame 12300: 19,872 calls compared, 0 differ. The fix pass after the review changed no fork file and ran the A/B once more on the same binary: the race to 14100 with the fast loop and with `PS2_VU0_EXACT=1`, both 69,308,449,691 cycles and 72,746,114 primitives, audio identical to each other byte for byte and to the reference; the window to 12700 with the check, 1,358,698 calls compared, 0 differ.

On top of that, 9,990 real calls (every 136th of the window to 12700, recorded to a file outside the repository and deleted afterwards) were replayed through `vu_micro.cpp` alone, natively and as WebAssembly under node: 0 of 9,990 differ in the check on either, and one hash over everything each call left behind is the same from the exact loop and the fast loop, natively and under node.

## 3. How much faster

### Native, in the race

A private copy of `vu_micro.cpp` with a timer at the `vu::run` dispatch (the processor's time stamp counter around whichever loop runs, turned into seconds against the steady clock over the whole run; the timer's own 8.9 ns a call taken off), linked from the build's own objects. The fork has no timing code. Frames 12650 to 13400, the frames `web-log-002.md` counted: 3,542,778 calls and 214,109,149 instructions, 4,724 calls and 285,479 instructions a frame, as counted there. Runs one after the other, nothing else running; each used about 3.6 cores itself (the CPU rasteriser).

| Loop | ns an instruction | ns a call | VU0 ms a frame |
|---|---|---|---|
| exact, run 1 | 24.47 | 1,479 | 6.99 |
| exact, run 2 | 24.30 | 1,468 | 6.94 |
| fast, run 1 | 10.19 | 616 | 2.91 |
| fast, run 2 | 10.13 | 612 | 2.89 |

2.40 times; 4.1 ms a frame less. All four runs and the run of the fork before the change end frame 13400 at 65,867,802,165 cycles and 47,145,247 primitives with the same audio bytes.

### WebAssembly, under node

The 9,990 recorded calls, each loop timed over 20 passes, best of three, the cost of setting a call up taken off. Ten native runs and fourteen under node across the afternoon:

| | Exact, ns an instruction | Fast | Ratio |
|---|---|---|---|
| native | 23.2 to 25.5 | 9.7 to 10.7 | 2.27 to 2.40 |
| node v24.19.0 | 27.3 to 32.7 | 14.2 to 16.7 | 1.82 to 2.05, thirteen of the fourteen between 1.93 and 2.05 |

The planning stage's prototype measured 2.2 under node on the same calls; the finished loop does not reach that. The plan asked for "at least 2.0" there, and it is met on some runs and missed on others.

The review linked the same replay with the page's own object file of `vu_micro.cpp` (the one `tools/build-web.sh` compiled, with the page's flags and threads) and recorded its own calls: 0 of 9,990 differ, the same hash, 2.05, 2.21 and 2.09 times; on 76,365 calls recorded to frame 13400, 0 differ and 1.91 times.

### What node shows about the page, and what it does not

Nothing here ran in a browser. The two things claimed about the page stand on different ground.

**The bits: what node computes, the page computes.** WebAssembly fixes the result of every float operation bit for bit, in every engine, with one exception: the sign and payload of a NaN result. No NaN's bits get out of `vu_exact.hpp`: a lane whose host result has exponent field 255 is handed back to the integer model, and in every lane that is kept all operands and all intermediate values are finite (the larger operand goes first, so nothing overflows while the sum is a number). WebAssembly has one rounding mode and no flush-to-zero mode, so the self-test of section 1 cannot fail there. That holds as long as the page is not built with relaxed SIMD or a fast-math switch, which hand the choice back to the engine or the compiler; `tests/wasm/run.sh` now reads the page's compile flags from the fork's `CMakeLists.txt` and builds the tests with them: with `-ffast-math` among them both tests fail under node (tried on a stand-in for the fork), and relaxed SIMD is refused outright, because node on one machine cannot speak for another engine's choice.

**The speed: an estimate, somewhere between 1 and 3 ms a frame.** Node's exact loop takes 27 to 33 ns an instruction; `web-log-002.md` worked out 21 ns for the page from the share of Chrome's profile samples that fell in `ps2::vu::run`. The two were not measured the same way, and which is nearer the page's real cost is not known. So:

- If the ratio carries over (1.8 to 2.2), the 6 ms a frame of `vu::run` on the page's emulation thread (`web-log-003.md`) becomes about 3 ms, and that thread's 11.6 ms of work a frame about 8.6.
- If the fast loop costs in the page what it costs under node (14.2 to 16.7 ns, 4.1 to 4.8 ms a frame), the saving is 1.2 to 1.9 ms.

Neither is a measurement.

### The browser run that is still owed

The page as built serves as its own before and after, because the address's `PS2_` parameters become the runtime's environment (`web/shell.html`):

1. **Speed.** `prof.mjs` as for `web-log-003.md`, frames 12650 to 13400, once on the plain address and once with `PS2_VU0_EXACT=1`: the share of `ps2::vu::run` (with `run_vu0` or `run_exact`, if the profile lists them apart) and the emulation thread's idle time.
2. **The check in the page.** With `PS2_VU0_CHECK=1` the console gets `[vu0-check] N calls compared, M differ` after every 1,000,000 calls, which in the race is a line about every 3.5 s; `M` must stay 0, and a call that differs also prints a `[vu0-check] call ...` line (the first 8). The menus make few VU0 calls (19,872 in the 12,300 frames before the race), so the first line comes in the race; no line after ten seconds of racing means the parameter did not arrive. The page never ends its run, so the closing lines `ps2run` prints do not appear. Both loops run on every call, which costs about one and a half times what the old page spent on VU0: the emulation thread will be at or past its 16.7 ms and the sound may crackle. That is not a failure of the check.

What can be tested of this without a browser is tested (section 6): the mode is taken from the environment, handed over through `Module.ENV` as the page hands it over, by code built with the page's flags and running on a thread of its own, under node.

### Where the time goes now

One made-up program of 60 instructions of a kind, ns an instruction:

| 60 of | Native exact | Native fast | Node exact | Node fast |
|---|---|---|---|---|
| nothing (idle upper and lower) | 9.8 | 2.4 | 12.4 | 3.9 |
| ADD, four lanes | 23.9 | 8.3 | 31.0 | 13.8 |
| ADD, one lane (through the model) | 15.3 | 10.0 | 20.9 | 12.9 |
| MUL, four lanes | 22.7 | 7.2 | 26.5 | 11.0 |
| MUL by the `w` of `vf0` | 23.6 | 5.7 | 29.3 | 8.0 |
| MADD, four lanes | 39.2 | 11.8 | 44.9 | 20.5 |
| MSUB, three lanes, one lane of `ft` | 26.0 | 11.7 | 33.5 | 18.4 |
| MAX | 11.9 | 3.7 | 15.0 | 5.3 |
| ITOF | 16.0 | 9.9 | 21.5 | 11.5 |
| a decoded lower (IADDIU, FMAND, LQ, SQ) | 10.5 to 11.1 | 2.9 to 3.5 | 12.7 to 14.8 | 4.4 to 4.9 |
| a lower through `lower()` (MR32) | 11.1 | 8.6 | 13.4 | 11.4 |

So the loop itself is 2.4 ns and an arithmetic instruction with its flags 5 to 9 ns more. What is left is the arithmetic, and WebAssembly does that part about 1.7 times slower than the native build.

### Tried here and not taken

- **Narrowing the products with WebAssembly's vector instruction.** The compiler turns the narrowing of the two product halves into four single conversions in WebAssembly (39 of them in the module, no vector one). A form that keeps the vector instruction was built and measured against the plain one on the recorded calls, in turn, four times: 14.66 ns against 14.67. No difference, so the header stays as stage A proved it.
- **The MAC flags of a one-lane result computed directly**, not through the exact loop's `set_flags`: 6% less natively (10.5 to 9.8 ns), 3% less under node, measured the same way on a scratch copy. One line. Not taken, because it changes the proven binary and every proof run would have to be repeated for it. It is the first thing to do when the loop is next opened.
- **No one-lane forms at all** (four lanes of host floats always): 4% less natively, 1.5% under node. Less than the line above.

## 4. How others handle it

- **reB3** (the Xbox reimplementation) has no vector unit to model: on the Xbox this maths is host SSE code. What it does have is a measurement of what rounding does to this game. Its own physics against the retail code on the same inputs gives the same bits for the first 32 to 156 frames, is 2 to 4 units in the last place apart after that, 1 cm apart by frame 286 and 1 m by frame 672; its notes call that divergence something a port cannot remove short of being exact. That is why "the same run" is the test here, and "close" is not.
- **PCSX2** computes VU arithmetic on host floats after flushing an exponent field of zero and optionally clamping the top binade, with clamp and rounding modes that its game database overrides per game. Its entry for this game sets one VU clamp override ("fixes buggy lighting in the garage") and no rounding override. A pull request (#12001) adds a software float class its interpreters can be switched to; it is not on the master branch that was read.
- **DobieStation** does the same flush and clamp and then uses host floats.
- **Play!** sends VU adds through a software add that truncates, on every platform except its JavaScript build.
- **PS2Float** (TellowKrinkle) is a model of the console's adder, multiplier and divider with test vectors, written as a reference for a future PCSX2 JIT.

None of the ones read gets the truncating multiply out of a double product or the adder out of a corrected host sum. Behaviour only; nothing copied. The files read are in `docs/research/PROVENANCE.md`.

`docs/re/gap-012-vu-fmor-fsor-wrong-meaning.md` is sometimes remembered as "one unit in the last place threw cars off the map". It does not say that: there two instructions had the wrong meaning and the collision test accepted every hit.

## 5. Found on the way

- **The model is not the hardware's multiplier.** The planning stage compiled PS2Float beside `fpu.hpp` in scratch and compared them on 100 million random operands each: `fp::add` agrees everywhere; `fp::mul` is one unit larger in 0.11% of products; `fp::div` differs in 38%, `fp::sqrt` in 24%, and the reciprocal square root in 60%, by one unit and sometimes two. In that model the console's multiply is also not commutative (one times `x` is not always `x`; `x` times one is). Whether the game depends on any of it is not known. The fast loop is identical to `fpu.hpp`, so it inherits this and makes it neither better nor worse.
- **VU1's corrected adder has a hole.** `vuops::add` gives a result one unit off when one operand has magnitude `0x7F7FFFFF` and the sum overflows inside its two-sum (the error term becomes a NaN and no correction is applied); with the operands swapped it is right. The new header sorts the operands and does not have it. VU1 is unchanged here.
- **VU1's fast paths still multiply as the host rounds**: the 110 primitives above.
- **Flush-to-zero breaks the corrected adder** (the error term of a tiny sum is a denormal). Hence the self-test.
- A log of a run started with `--load-state` has a `[state]` line that `tools/scan-log.py` does not know, so such a log never scans clean. Not changed here.
- From the plan's reading, not checked again: hook shadow mode compares neither VU0 registers nor VU0 memory; reading VU0 control register 28 prints a line on every call; a VIF0 MSCAL overwrites the TOP values it shares with VU1.

## 6. Tests

- `tests/unit/test_vu_exact.cpp` (1,857 checks) and `tests/runtime/test_vu0_loop.cpp` (976 checks natively, with its seven environment runs of 9 checks each; 966 as WebAssembly, which cannot start a program), both described above. Both need clang, which the fork is built with; `tests/unit/run.sh` and `tests/runtime/run.sh` now use clang++ when it is installed and `CXX` does not say otherwise, and pass `-ffp-contract=off`. Before, the documented command ran g++ and `test_vu_adder` only printed "skipped".
- `tests/wasm/run.sh <fork> [build dir]` (new): builds both tests with the fork's em++, runs them under its node, and requires their hash lines to be the native build's. Exit 2 without the toolchain. Each test is built twice: plainly (`-O3 -msimd128`, on node's main thread), and as the page builds that code, with the compile flags read from the fork's `CMakeLists.txt` (threads, tail calls, WebAssembly exceptions) and `main` on a thread of its own, as the page runs the emulation. The page build of the loop test is then run once for each of the seven environments, the variables handed over through `Module.ENV` as `web/shell.html` hands over the address's parameters. Before the fix pass the script built with its own flags only, and its header said they were the page's.
- `tests/test_scan_log.py`: two cases for the check mode's lines.
- On the final tree: unit 23 passed; wasm 4 passed (two tests, two builds each); runtime 6 passed with the committed tests plus `test_vu0_loop`.
- Not green, and not from this change: another session's uncommitted tests share the working tree and belong to another fork worktree. As documented, `tests/runtime/run.sh` gives 2 passed, 10 failed, and the Python suite 212 tests with 3 failures (`test_recomp_hook_check`, `PointedFunctionTest`).

## 7. In Chrome, the same evening

The page of fork `burnout3` after d9ae500 (VU0 here, the audit's fixes of the same day on top, see `docs/re/open-issues.md`), `ps2web.wasm` 24.9 MB, driven by `speed.mjs` as in `web-log-003.md` on a quiet machine. The page is its own before and after: `?PS2_VU0_EXACT=1` selects the old loop. Every run held 60.0 fps over the race with no JavaScript error.

Race, frames 12650 to 14100. Since this build the page splits the emulation thread's time held at the present fence ("held") from its idle time; `web-log-003.md`'s "idle" was the two together.

| | Fast loop (three runs) | Old loop | Before the day's work (`web-log-003.md`) |
|---|---|---|---|
| Emulation thread idle | 49.2%, 50.3%, 49.3% | 27.1% | 30.5% with held |
| held at the present fence | 3.6%, 3.2%, 2.9% | 1.9% | |
| Emulation thread busy, per frame | 7.9 ms | 11.8 ms | 11.6 ms |
| First 400 frames, idle | 51.2%, 52.8%, 50.5% | 26.7% | |
| Frames 13050 to 13400 (the takedown), idle | 35.3%, 36.8%, 37.2% | 16.0% | |
| All of Chrome, in cores | 2.08, 2.06, 2.14 | 2.35 | 2.34 |
| WebAssembly memory | 256 MB | 256 MB | 307 MB |

An earlier pair of runs of the same page gave 51.6% against 28.1% and 2.11 against 2.29 cores.

- **The fast loop saves 3.9 ms of the emulation thread a frame in the page**, a third of what the thread did. Section 3's estimate of 1 to 3 ms was low: the page gains as much as the native program (4.1 ms).
- **The check in the page:** with `?PS2_VU0_CHECK=1` to frame 13400, `[vu0-check]` lines up to 5,000,000 calls compared, 0 differ.
- The lowest one-second sample is 0% idle in every run, in the takedown. That is the VU thread: it is 10% idle there, and the emulation thread waits for room in the VU ring rather than at the fence. The VU thread is now the one to speed up (`docs/re/open-issues.md`, the JIT items).
- Memory: 256 MB from start to end, so the page no longer grows to 307 MB; the render rings are half their old size and the heap's top after a race is 249 MB.
- `?pace=old` against the new pacing: the same on this machine (49.5% idle), as expected on a display at 60 Hz or more.

**A freeze found and fixed on the way.** The first runs of this page stopped at frame 13228 in six of nine runs, with the old loop as well as the new. A dump of every thread's waits (`Module._ps2web_describe()`, new) and a profile of the stopped page showed the emulation thread asleep in `Ring::post`, waiting for room in the VU ring, with the ring empty and the VU thread's wake already sent: the poster had slept on a freed count 20,351 lower than the ring's. The VU ring is 16 MiB since the audit's change of the same day and now fills in the takedown, so the poster waits there often, and a lost wake became a freeze. The wait now ends after 2 ms and looks again; the page counts 4 to 7 lost wakes a race. Why the wake is lost (the waiting code follows the usual futex rule) is not known.

## Not done

- ~~**No browser run.**~~ Done the same evening: section 7.
- Compiled code per VU0 microprogram, flags worked out ahead of time: the only way past about 3 times. The header, the decoded form and the check mode can be reused for it.
- The one-lane flags of section 3.
- Anything in VU1: its multiply, and the adder hole. Both change pictures and need their own A/B.
- Moving `fp::mul`, `fp::div` and the square roots towards the hardware (section 5).
- The EE's own float code (COP1 and COP2 macro mode) still runs on the integer routines.
- Why a futex wake between the page's threads can be lost (section 7).
