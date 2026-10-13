# Function harness log 003: hooks in live play (Part 3b) — 2026-10-13

Part 3b of `docs/re/function-harness-spec.md`: the recompiler puts the hook check into generated code, so a decompiled function replaces the recompiled one for every caller and not only for calls that go through `dispatch()`. Work by Opus 1 in the fork's main checkout, fork commit `b2c8a15`; the regenerated code and its build are in folders of their own (`generated/burnout3-3b`, `build/native-3b`) until the shared ones are switched. Runs and logs are under `~/b3/out/harness04/` and `~/b3/out/harness05/`. Nothing from the game is in this repository.

All the runs below are the race from the pre-race state (`~/b3/out/run011/pre-race.state`, frame 12300 to 14100, accelerate held from 12600), built before gap 012 was fixed. That race is the broken one (cars thrown off the map); it serves here as 1,800 frames of the game calling these functions and nothing more.

**The mechanism works and costs nothing when no hook is on. Twelve of the nineteen decompiled functions are called in this stretch, 1.6 million calls, and on every call compared the hook's result and memory are the recompiled function's. A run in which hooks serve the calls takes a slightly different course all the same, because a hook uses no emulated time. A new shadow mode shows that this is the only reason: with the hooks' own outcomes left in place and the original time charged, the run is the same as without hooks, cycle for cycle.**

## What changed in the fork

- `recomp/codegen.cpp`: in each unit's entry switch, the arm of an address that is a function (`Program::functions`: every `jal` target, seed, symbol and pointer-table target) calls `enter()` when `g_hooks` is set, before the jump to the function's label. The spec put the check at the label; it is in the switch arm so that a branch inside the unit to a function's first instruction (a loop whose head is the entry) does not meet it. A function is entered through the switch by a direct call, a jump from another unit, a register jump and `dispatch()`. A `j` to a function in the same unit is a jump to the label and is not offered to the hooks; that tail call runs the recompiled code.
- `runtime/src/hooks.cpp`, `dispatch.cpp`, `generated.hpp`: `dispatch()` calls `offer()` and generated code calls `enter()`. An entry that `dispatch()` offered is not counted again by the check in the unit it then enters, so traces and entry snapshots see each entry once. Old generated code works with the new runtime (the runs below with the old code are that), and new generated code calls nothing an older runtime lacks.
- `--trace-calls` now lists directly called functions (it saw only calls through `dispatch()` before).
- At exit `ps2run` prints one `[hook]` line for each enabled hook with the number of calls it served.
- Shadow mode, `--shadow N` with `--hook`/`--hooks all`: enabled hooks do not serve their calls. On every Nth call of a hook the recompiled function runs on the live call, then the hook runs on a copy of the same call (registers, all of EE RAM and the scratchpad), and the two outcomes are compared; the recompiled outcome stays. The game therefore runs exactly as without hooks, and each hook is checked on the calls the game really makes. Reported per hook: calls compared; calls where a register the callee must keep or memory outside the callee's frame differs ("wrong"); calls where `v0` differs, where `f0` differs (one of the two is the function's result, the other a register it may use freely); calls where any other scratch register differs; and calls not compared because the runtime stopped to run events inside them. The first four of each kind are printed with the arguments.
- `PS2_SHADOW_LIVE=hook` (an experiment switch for shadow mode): the hook's outcome stays instead, memory and registers, while the cycles used are still the recompiled function's. Register groups named after it (`gpr`, `fpr`, `hilo`, `fcr`) are taken from the recompiled outcome.
- `preempt_count()` in `system.cpp`, which shadow mode uses to see that events ran inside a call.

## Regeneration

`eerecomp` on the game's executable takes 13 seconds: 861,427 instructions, 6,113 functions, 67,234 entries, 225 units. Against the code generated before, 6,113 lines differ in 214 units, one per function, and every one is a switch arm that gained the check. `ps2run` builds from the regenerated code in 1 minute 25 seconds on eight jobs.

## With no hook enabled, nothing changes

The race with the old generated code and with the regenerated code, side by side: both end at 69,308,450,101 cycles with 54,863,080 primitives drawn, and the two audio recordings (30 seconds, 5.76 MB) are byte for byte the same. Wall time 349.4 s and 349.8 s, three runs sharing the machine.

A harness call of `0x0015D0A0` (a registrar that makes 82 registrations) with `--trace-calls`: 247 entries of 4 distinct functions with the old code, 3,249 entries of 21 distinct functions with the regenerated code; the result and the cycle count (382,107) are the same, the entry itself is counted once in both, and so are the three functions it reaches through pointers (82 each).

## With hooks serving the calls

`--hooks all`: the race runs to frame 14100 with 0 IOP faults.

| Address | Function | Calls in 1,800 frames |
|---|---|---|
| `0x001B9820` | `sap_compare_x` | 1,533,768 |
| `0x00254970` | `slot_pool_register_entry` | 34,464 |
| `0x00156170` | `event_stamp_within_window` | 21,711 |
| `0x001478C0` | `ring_index_is_at_or_ahead` | 7,545 |
| `0x001C6F30` | `select_gear_under_rev_limit` | 6,108 |
| `0x00145E70` | `ring_index_advance` | 1,975 |
| `0x001553A0` | `stamped_value_if_fresh` | 1,168 |
| `0x0021BFA0` | `valuedb_hash_key` | 396 |
| `0x0021AEF0` | `decode_id_base40` | 377 |
| `0x002F9CA0` | `arena_alloc_aligned` | 328 |
| `0x002934D0` | `path_extension_after_dot` | 10 |
| `0x0015A750` | `recent_sample_ring_read` | 6 |

Not called in this stretch: `boost_stats_as_ints`, `find_key64_slot`, `compare_ints_cyclic_from_pivot`, `lowest_set_bit_index`, `compare_wide_strings`, `build_texture_uv_rect`, `pack_fixed16`.

The run is not the same run, though. It ends at 69,308,449,962 cycles with 54,857,801 primitives, the audio differs from one second in, and of 36 frames kept (one every 50) 29 differ, the first at frame 12400, before the race starts. One hook at a time, seven of the twelve change the primitive count and five do not; three different hooks give the same other count, and a hook with 328 calls changes it while one with 34,464 does not. That pattern is not what a wrong result looks like. A hook returns without using any of the cycles the recompiled function would have used (8 to 26 a call for these), so the EE is a little ahead of where it would be, and what the game has done by a given frame shifts.

## Shadow mode: the hooks against the game's own calls

Eight runs at once, `~/b3/out/harness05/`. Every one of them, comparison runs and experiment runs alike, ends at 69,308,450,101 cycles with 54,863,080 primitives, the same as the run without hooks next to them.

The comparison (the recompiled outcome stays):

| Function | Result in | Calls | Compared | Wrong | Result register differs | Other scratch registers differ | Not compared |
|---|---|---|---|---|---|---|---|
| `sap_compare_x` | `v0` | 1,533,768 | 3,067 (every 500th) | 0 | 0 | 3,057 | 0 |
| `slot_pool_register_entry` | memory | 34,464 | 4,302 (every 8th) | 0 | | 4,302 | 6 |
| `event_stamp_within_window` | `v0` | 21,711 | 5,427 (every 4th) | 0 | 0 | 4,325 | 0 |
| `ring_index_is_at_or_ahead` | `v0` | 7,545 | 7,545 | 0 | 0 | 7,545 | 0 |
| `select_gear_under_rev_limit` | `v0` | 6,108 | 6,099 | 0 | 0 | 6,099 | 9 |
| `ring_index_advance` | `v0` | 1,975 | 1,975 | 0 | 0 | 1,933 | 0 |
| `stamped_value_if_fresh` | `v0` | 1,168 | 1,168 | 0 | 0 | 1,168 | 0 |
| `valuedb_hash_key` | `v0` | 396 | 378 | 0 | 0 | 378 | 18 |
| `decode_id_base40` | `v0` | 377 | 372 | 0 | 0 | 372 | 5 |
| `arena_alloc_aligned` | `v0` | 328 | 328 | 0 | 0 | 328 | 0 |
| `path_extension_after_dot` | `v0` | 10 | 10 | 0 | 0 | 10 | 0 |
| `recent_sample_ring_read` | `f0` | 6 | 6 | 0 | 0 | 6 | 0 |

"Wrong" is a kept register or memory outside the callee's frame. No hook differs from the recompiled function in its result, in memory or in a register it must keep, on any call compared. Nearly every call differs in scratch registers: the recompiled function leaves its temporaries behind (`v1`, the argument registers, `hi` and `lo` after a divide, `f0` and `f1` after a float compare, the upper halves of 128-bit registers), and a hook leaves what was there. For four functions that includes `f0` or `v0`, whichever is not their result.

Do those leftovers matter to the game? The experiment runs answer it: with `PS2_SHADOW_LIVE=hook` and every call compared, so that after each call the registers and memory are exactly what the hook left, for eleven of the twelve functions in three runs (all but `sap_compare_x`): 74,088 calls, the hook's outcome left in place after all but 112 of them (those with events inside). The three runs are identical to the run without hooks in cycles and primitives. The leftovers do not change the course of the game in this stretch, and the different course of a run where hooks serve the calls comes from the missing cycles alone.

## The same on the corrected race

After gap 012 the race from the same state is a different and heavier one (72,745,908 primitives against 54,863,080). Shadow mode on it, same groups (`~/b3/out/harness06/`; `sap_compare_x` not rerun): both runs end at 69,308,449,691 cycles and 72,745,908 primitives, the figures of the run without hooks that recorded the race02 corpus. Eleven functions were called, 100,466 calls, 27,615 of them compared: no difference in result, memory or kept registers. `event_stamp_within_window` and `slot_pool_register_entry` are called about 41,500 times each here (21,711 and 34,464 before). The same seven functions are not called.

After that the shared generated code (`generated/burnout3`) was regenerated with the check and the main native build and the browser page rebuilt from it (`ps2web.wasm` 24,845,564 bytes, 150 KB more). The page has not been run since; what the check costs in WebAssembly has not been measured.

## The snapshot tests on the corrected race

The race01 corpus was recorded while cars were being thrown off the map (gap 012), so it is replaced: `~/b3/out/snaps/race02`, twelve states from frame 12450 to 14100 every 150, state version 6, recorded from the pre-race state with fork 76eb759 plus Part 3b (hooks off); `CMD.txt` and a frame for each state are beside them. race01 is deleted.

All eighteen functions of the two decomp logs were run again on race02 with the fork at f2f31d2 and the case files and comparisons of `docs/re/harness-log-002.md` and `docs/re/decomp-log-002.md`: 33,000 cases, 0 fail, 0 skip (`~/b3/out/harness07/`).

## Tests

- `tests/test_recomp_hook_check.py` runs `eerecomp` on a made-up three-function program (one of them a loop whose head is its first instruction) and reads the generated C++: the three function arms have the check, the arms of return addresses do not, and the loop branch goes to the label past it. Three tests, failing before the change; needs `B3_EERECOMP`, skipped without it.
- `tests/runtime/test_hooks.cpp` builds the fork's `hooks.cpp` alone with a stand-in for a generated unit: enabling, serving a call from generated code and from `dispatch()`, counting an entry once however it is reached, and shadow mode (the recompiled outcome and its cycles stay; a hook that stores another byte or changes a kept register is reported; a call with events inside is not compared; every Nth call). Eight tests, 46 checks. It was written after the code; breaking the registry in two ways (dropping the once-only rule, dropping the report) made 3 and 2 checks fail.

## Limits and what is not done

- `sap_compare_x` was compared on one call in 500 and was not in the experiment runs: a comparison copies EE RAM five times, about 20 ms, and it is called 850 times a frame.
- Seven of the nineteen functions are not called in this stretch. They are covered by the snapshot tests only.
- 38 calls were not compared because events ran inside them (112 in the experiment runs).
- A same-unit `j` to a function is not offered to the hooks.
- Hooks use no emulated time. A per-hook cycle count would keep a hooked run close to an unhooked one; nothing needs it yet.
- Hooks are not enabled in the browser page.
- `--snapshot-on-entry` (Part 5 of the spec) is still not wired to a command-line option.
- All of this was measured on the race as it was before gap 012.
