# Function harness log 001 — 2026-10-12

Fork: branch `burnout3` at 3e42c71 plus the uncommitted harness changes (Parts 1, 2, 3a of
`docs/re/function-harness-spec.md`). Repo side: `src/hooks/hooks_table.cpp`,
`src/hooks/pack_fixed16.cpp`, `tools/difftest.py`, `tests/test_difftest.py`. All runs from
the fork root in WSL with `PS2_GPU_FALLBACK=1`; every `--call` also with `PS2_GS=off`.
Outputs are under `~/b3/out/` (listed at the end); nothing game-derived is in the repo.

**Verification steps 1 to 7 of the spec pass. The recompiled function at `0x00389700` and
its decompiled hook agree on 1320 of 1320 cases; a deliberately wrong copy of the hook is
caught on 108 of them.** Step 8 belongs to Part 3b and was not run.

## Step 1: build
`ninja -C build/native-burnout3 ps2run` builds. Touching every changed runtime and host
source and rebuilding gives 0 lines containing "warning". The build directory is configured
with `-DPS2_HOOKS=` pointing at this repo's `src/hooks` (a path with spaces works). Turning
`PS2_HOOKS` on recompiles all 225 generated units once; changing a hook afterwards is a
4-target rebuild. A configure without `PS2_HOOKS` builds too (stub `register_hooks()`).

## Step 2: snapshots
Command (also in `~/b3/out/snaps/menu01/CMD.txt`): `ps2run SLUS_210.50 --disc disc.iso
--memcard … --frames 3600 --press 1900:start:10 --press 2300:start:10 --press 2500:down:6
--press 2600:cross:10 --press 2800:cross:10 --press 3000:cross:10 --press 3150:up:6
--press 3250:cross:10 --snapshot-every 300 --snapshot-dir ~/b3/out/snaps/menu01`.
60 s, rc 0, twelve states `s000300.state` … `s003600.state`, 46.2 MB each, 530 MB in all.
```
[state] saved frame 300 to /home/mark/b3/out/snaps/menu01/s000300.state (45165 KB)
[state] saved frame 3300 to /home/mark/b3/out/snaps/menu01/s003300.state (45175 KB)
[state] saved frame 3600 to /home/mark/b3/out/snaps/menu01/s003600.state (45176 KB)
[iop] 0 faults
[gs] 1212156 primitives drawn
[ee] stopped after 3600 frames, 17698702702 cycles: frame limit reached
```
(one `[state] saved` line per 300 frames; three shown.) The first attempt wrote eleven
states: the one due on frame 3600 was lost to the frame-limit halt. That is implementer
correction 1 in the spec; the run above is after the fix.

### Run-to-run noise
The spec asked for a run "otherwise identical to run 010". Run 010 used another press
schedule (791,207 primitives), so the comparison was made against a control run with the
same presses and no snapshots.

| Run | Primitives | EE cycles |
|---|---|---|
| snapshots, first attempt (eleven states) | 1,210,906 | 17,698,702,660 |
| control, no snapshots | 1,211,906 | 17,698,702,721 |
| snapshots, after the fix (twelve states) | 1,212,156 | 17,698,702,702 |

The control log and the first snapshot log are the same line for line apart from the
`[state]` lines, the memory-card path, the `[gs]` totals, DMA channel 8 starts (111,743
against 111,763) and the cycle total. Three runs of one schedule differ by up to 1,250
primitives and 61 cycles whether or not snapshots are taken, so the difference is noise
between runs, not an effect of saving. Its cause was not looked for.

## Step 3: a WebGPU-saved state under the software renderer
It loads. `PS2_GS=off ps2run … --load-state ~/b3/out/snaps/menu01/s003300.state --frames 3400`:
```
[state] loaded frame 3300
[ee] halted: frame limit reached
[ee] stopped after 3400 frames, 16715659801 cycles: frame limit reached
```
rc 0, no `state section mismatch`.

## Step 4: first calls
All on `s003300.state`, about 0.2 s of wall time per call.

| Arguments | Line | rc |
|---|---|---|
| `--call 0x00389700 --f12 f:1.5 --a0 8 --a1 1` | `[call] 0x00389700 ok v0 0000000000000180 v1 0000000000000100 f0 00000080 cycles 31 ram 0 spr 0 stack 0 io 0 syscalls 0` | 0 |
| same with `--a1 0` | `[call] 0x00389700 ok v0 0000000000000180 … cycles 28 …` | 0 |
| same with `--f12 f:-1.5 --a1 1` | `[call] 0x00389700 ok v0 0000000000008180 … cycles 38 …` | 0 |
| `--call 0x00389704 …` | `[call] 0x00389704 no_code: no translated entry at that address … cycles 0 …` | 3 |

The JSON records `"state_version":4,"frame":3300`; the `.mem` file of a call that changes
no memory is empty.

## Step 5: guards
- The libkernel heap wrapper, `--call 0x001130B0 --a0 0x1000`:
  `[call] 0x001130B0 forbidden_syscall: syscall 0x44 at 0x0010BE64 v0 0000000000001272 v1 0000000000000044 f0 3D088888 cycles 312 ram 6 spr 0 stack 2 io 0 syscalls 3`,
  rc 3. The syscall list is `[64,119,68]` (0x40 CreateSema, 0x77, 0x44 WaitSema). The log
  has no `[frame]`, `[sys]`, `[thread] ->` or `[iop` line.
- A loop under a small budget: `strcpy` at `0x001272D0` copying a 1500-byte string, with
  `--stack-reserve 0x2000 --buf a0:2048 --buf a1:2048:<hex>`:
  `[call] 0x001272D0 ok v0 0000000001FFDC40 v1 0000000001FFE21D … cycles 1149 ram 1 spr 0 stack 0 io 0 syscalls 0`;
  the one range is `0x01FFDC40`, 1504 bytes (the destination buffer, reported as a result),
  and the `.mem` file is 1513 bytes. With `--budget 1000`:
  `[call] 0x001272D0 hung: cycle budget exhausted … cycles 1010 ram 1 …`, rc 3, 0.18 s.
- `--buf a0:8192` with the default reserve:
  `bad_request: buffers do not fit in the stack reserve`, rc 3.
- Not exercised: the custom-handler branch. No call made here met a game-installed syscall
  handler, so that path is built and untested.

## Step 6: hook A/B and the driver
- `--call 0x00389700 --f12 f:1.5 --a0 8 --a1 1 --hook pack_fixed16`:
  `[call] 0x00389700 ok v0 0000000000000180 v1 0000000000000000 f0 3D088888 cycles 0 ram 0 spr 0 stack 0 io 0 syscalls 0 (hook)`.
  With `--f12 f:-1.5 --hook 0x00389700`: `v0 0000000000008180 … (hook)`.
- `--trace-calls` without a hook: `"entries":["0x00389700"]`. `--hook nosuch`:
  `unknown hook nosuch`, rc 2.
- `tools/difftest.py --function 0x00389700 --hook pack_fixed16 --snapshots
  ~/b3/out/snaps/menu01 --args-file cases.txt --returns v0 -j 12`: twelve snapshots times
  110 argument lines (values 0, ±0.5, ±1.5, ±1000.25, ±70000.0, ±3e9; fraction bits 0, 4,
  8, 12, 15; signed format 0 and 1), 54 s:
  `pack_fixed16: 1320 pass, 0 fail, 0 skip`, rc 0.
  `--returns v0` is needed for this function: the hook leaves `v1`, `f0` and `f1` alone,
  and the default set (`v0,v1,f0,f1`) would report those scratch registers.
- The wrong-variant demonstration. A copy of the hook that takes the magnitude before
  truncating (instead of truncating and then negating) was built from a folder under
  `~/b3/out/harness01/`: `pack_fixed16: 1212 pass, 108 fail, 0 skip`, rc 1, for example
  `s000300_c100 FAIL v0 000000000000ffff != 0000000000005e7f`. All 108 failures are the
  −3e9 lines, nine of the ten per snapshot (fraction bits 15 with the signed format happens
  to agree). ±70000.0 does not saturate the conversion, which is why ±3e9 was added to the
  grid. The build was then pointed back at the repo's hooks and rechecked.
- After the hooks glob became recursive (correction 10): rebuild rc 0 with no warning, the
  hooked call prints the same line, and the driver again reports
  `pack_fixed16: 1320 pass, 0 fail, 0 skip`. At that point `src/hooks/` held only the two
  files above, so no subfolder source has been compiled yet.

`tests/test_difftest.py`: 32 tests, pass under Windows Python 3.14 and WSL `python3`; the
`ps2run` calls are replaced by a stand-in passed to `main()`.

## Step 7: fork tree
```
 M CMakeLists.txt
 M host/main.cpp
 M runtime/include/ps2/generated.hpp
 M runtime/include/ps2/system.hpp
 M runtime/src/dispatch.cpp
 M runtime/src/kernel.cpp
 M runtime/src/memory.cpp
 M runtime/src/system.cpp
?? runtime/include/ps2/harness.hpp
?? runtime/include/ps2/hook.hpp
?? runtime/src/harness.cpp
?? runtime/src/hooks.cpp
```
Only files named in the spec's Fix.

## Outputs
| Path | Contents |
|---|---|
| `~/b3/out/snaps/menu01/` | the twelve states, `CMD.txt`, `run.log`, the memory card (530 MB) |
| `~/b3/out/snaps/menu01-control/` | the control run's log |
| `~/b3/out/harness01/` | logs and JSON of the calls above, `cases.txt`, the driver's output, the wrong-variant hook folder |
| `~/b3/out/diff/pack_fixed16/` | the driver's `recomp/`, `decomp/` and `summary.json` (22 MB) |

`~/b3` is at 7.1 GB.

## Verdict
The harness works as designed for leaf functions: snapshots, isolated calls, the four
guards that could be exercised, the hook switch and the driver. `pack_fixed16`
(`0x00389700`) passes on the `menu01` corpus with the grid above. The corpus is tied to
state version 4 and this fork build.
