# Decomp log 002 — eight functions with globals and guest objects, 2026-10-12

Second batch of game functions in `src/hooks/`. Where batch 1 (`docs/re/decomp-log-001.md`) kept
to leaves without globals, this batch takes functions that read fixed globals or walk guest objects
through pointers, in the score, vehicle, AI and ValueDB ranges, and writes them in guest terms
(`ee::guest_ptr`, `ee::f32`, `ee::ld`/`ee::st` from `src/ee/`) so that most need no shim at all.

**All eight are verified: 20,194 oracle cases over the menu01 and race01 corpora, 0 fail,
0 skip. Unit tests 21 of 21.**

How the batch was made, for the record, because it was not the full workflow of log 001. The
finder, reader and coder stages ran in the planning session and produced the eight headers, the
eight implementations and six unit tests; that session then stopped (out of credit) before the two
skeptic passes and the fixer ran. The batch was finished by hand in the coding session: each
function was read again against the fork's generated C++ for its address (all eight agree with it,
no implementation was changed), the two unit tests that did not pass were corrected (both were
mistakes in the tests, below), the two missing unit tests and the shims were written, and every
function went through the oracle. So the independent check each function has had is one re-read
and the oracle, not two skeptics.

## Summary

| Retail address | Our name | Subsystem (dir, namespace) | Size | Unit test | Oracle cases (menu01 + race01) | Status |
|---|---|---|---|---|---|---|
| `0x0014E7E0` | `boost_stats_as_ints` | `score` (`b3::score`) | 0x48 | passes (new) | 168 + 154 | verified |
| `0x001553A0` | `stamped_value_if_fresh` | `score` | 0x50 | passes | 2,052 + 1,881 | verified |
| `0x00156170` | `event_stamp_within_window` | `score` | 0x44 | passes (4 expectations corrected) | 1,272 + 1,166 | verified |
| `0x0015A750` | `recent_sample_ring_read` | `vehicle` (`b3::vehicle`) | 0x80 | passes | 1,980 + 1,815 | verified |
| `0x0021AEF0` | `decode_id_base40` | `valuedb` (`b3::valuedb`) | 0x100 | passes | 480 + 440 | verified |
| `0x00254970` | `slot_pool_register_entry` | `vehicle` | 0xDC | passes (one misspelled name) | 1,812 + 1,661 | verified |
| `0x0026CBE0` | `compare_ints_cyclic_from_pivot` | `ai` (`b3::ai`) | 0x8C | passes | 2,460 + 2,255 | verified |
| `0x002F9CA0` | `arena_alloc_aligned` | `util` (`b3::util`) | 0x50 | passes (new) | 312 + 286 | verified |

Sizes run from the entry to the end of the return's delay slot. Each function's header in
`src/hooks/<dir>/<name>.hpp` carries the full description, the evidence and the edge cases; this
log does not repeat them. `event_stamp_within_window`'s header says 16 instruction words; it is 17
(size 0x44), the return being at `0x001561AC`.

`decode_id_base40` is the base-40 id codec that log 001 listed as a finder miss ("no base-40
codec exists as a function"). It exists, at `0x0021AEF0` with 81 call sites; it multiplies by 40
with two shifts and an add, which is why a search for multiply or divide constants did not find it.

## What each function is

- **`boost_stats_as_ints`** reads three floats of the record that the object at `a0` points to
  (pointer at +1380; fields +936, +932, +928) and stores them, converted with the EE's
  float-to-int conversion, through three out pointers. Every known caller passes the object
  `0x006481A0`. The pointer is read again before each field.
- **`stamped_value_if_fresh`** returns the word at +1516 of an object while the float stamp at
  +1520 plus the window in `f12` is at or above the object's clock at +12; 0 when the stamp is
  exactly -1.0f or the window has passed.
- **`event_stamp_within_window`** follows the pointer at +200 to a state block and answers
  whether the stamp at +5512 is not negative and the clock at +4300 is at or below stamp + window.
  Three siblings (`0x00156080`, `0x001560D0`, `0x00156120`) are the same code with other stamp
  offsets; the C++ has the shared core `event_stamp_within_window_at`, but only this one is hooked.
- **`recent_sample_ring_read`** returns one float of an eight-slot ring by sequence number, or
  -1.0f when the number is not one of the seven most recent.
- **`decode_id_base40`** packs the first twelve characters of an id string into a 64-bit key.
- **`slot_pool_register_entry`** appends a vehicle render entry to the pool of 16-byte slots at
  `0x01D9D010`, using the count at `0x004E29D0`, the running total at `0x004E29D4` and the two flag
  bytes at `0x004E29E0` and `0x004E29E4`.
- **`compare_ints_cyclic_from_pivot`** is a `qsort` comparator over ints that sorts the value in
  the global at `0x004E19F8` first, then larger values, then smaller ones.
- **`arena_alloc_aligned`** is one bump-allocation step over a 28-byte arena descriptor.

## Unit tests

`tests/unit/run.sh <fork>`: 21 programs pass.

- `test_event_stamp_within_window.cpp` failed 4 of 1,448 checks as written. All four expected a
  float add that overflows to give `0x7F7FFFFF`. The EE add in the runtime (`ps2::fp::pack`)
  saturates to the maximal pattern `0x7FFFFFFF`, as the function's own header says ("saturates to
  the signed maximal pattern"), so a deadline that overflows is at or above every clock and the
  answer is 1. The expectations were corrected and three more checks added beside them; the oracle
  cases below include the same patterns and agree.
- `test_slot_pool_register_entry.cpp` did not compile: one use of a constant was misspelled
  (`kEntrySubobjectSecond()` for `kEntrySecondSubobject`). Corrected; it passes.
- `test_boost_stats_as_ints.cpp` and `test_arena_alloc_aligned.cpp` are new: field order,
  conversion edge cases (truncation, denormals, saturation, exponent-255 patterns) and the
  aliasing consequences of the re-read pointer for the first; rounding, the capacity boundary,
  alignment 0 and non-powers of two, 32-bit wrapping and the unsigned compare for the second.

## Shims (`src/hooks/hooks_table.cpp`)

Five of the eight are registered with `ee::abi::hook<&fn>` and have no shim: their signatures are
already guest addresses, `f32` and integers. For the two overloaded names
(`compare_ints_cyclic_from_pivot`, `arena_alloc_aligned`) the guest-pointer overload is selected
with a cast. Three have written shims:

- `decode_id_base40` takes a host `const char *`, so the shim translates `a0` with
  `ps2::host_ram()`; the 64-bit key is written to `v0` whole.
- `event_stamp_within_window` and `stamped_value_if_fresh` also reproduce the FPU condition bit
  the original leaves, so that `--strict` can compare `fcr31`. For the first the bit ends set when
  the stamp is negative and otherwise equals the answer; for the second it ends set when the slot
  is empty or stale and clear when the word is returned.

Extensions the oracle confirmed: `arena_alloc_aligned` and `stamped_value_if_fresh` return a
32-bit word sign-extended (a start or a stored word at or above `0x80000000` comes back with ones
above bit 31); `recent_sample_ring_read` returns in `f0`; `boost_stats_as_ints` and
`slot_pool_register_entry` return nothing, their results being the memory they write.

## Oracle

Fork `burnout3` at 5b782b3, `ps2run` built with `-DPS2_HOOKS=` on this repo's `src/hooks`. Two
corpora: `~/b3/out/snaps/menu01` (twelve states, menus) and `~/b3/out/snaps/race01` (eleven
states in a running race; see `docs/re/harness-log-002.md`). One run per function and corpus:
```
python3 -I <repo>/tools/difftest.py --ps2run build/native-burnout3/ps2run \
  --elf games/burnout3/disc/SLUS_210.50 --function <address> --hook <name> \
  --snapshots ~/b3/out/snaps/<corpus> --out ~/b3/out/diff/<corpus>_<name> \
  --args-file ~/b3/out/harness03/cases/<name>.txt <compare flags> -j 8
```
with `--returns v0` for `compare_ints_cyclic_from_pivot`, `arena_alloc_aligned` and
`decode_id_base40`; `--returns v0 --strict` for the two stamp functions; `--returns f0` for
`recent_sample_ring_read`; `--returns=` for `boost_stats_as_ints` and `slot_pool_register_entry`.
Every run exited 0 with a summary line of the form `slot_pool_register_entry: 1661 pass, 0 fail,
0 skip`; a sample case line is `s012600_c000 PASS cycles 17`.

Cases (generated by a script kept outside the repo; counts are per snapshot):

- **`arena_alloc_aligned`**, 26: the descriptor in a `--buf` and the size in `a1`. Aligned and
  unaligned cursors, alignments 1, 4, 16, 0x100, 0, 3, 6 and `0x80000000`, new cursor equal to and
  one past the limit, a failure followed by the advanced cursor, wrap past 2^32, limits above 2^31
  (the compare is unsigned), a size with bits above 31 set (only the low word counts).
- **`decode_id_base40`**, 40: the empty string, every class of character alone, mixed-case pairs
  that must agree, space padding, strings of exactly 11, 12, 13 and 16 characters (no terminator
  within twelve), bytes above 0x7F, and twelve-character strings of out-of-alphabet bytes whose
  digits reach 44 and make the 64-bit value wrap.
- **`boost_stats_as_ints`**, 14: eleven float triples (plain, truncating, denormal, the largest
  values below 2^31, saturating, exponent-255) with the object, its record and three out words in
  buffers at fixed addresses (`--sp 0x00080000 --stack-reserve 0x2000`); two aliasing cases (the
  first out pointer is the object's pointer field, or the third float); and one call on the real
  object `0x006481A0` as each snapshot has it.
- **`recent_sample_ring_read`**, 165: nine settings of the three counters (among them a negative
  segment index, a capacity of zero and products that wrap) with eight marked slot values, and for
  each the sequence numbers from ten below `next` to eight above it plus 0, 1, 7, 8, the largest
  positive and several negative numbers.
- **`event_stamp_within_window`**, 106: 53 (stamp, window, clock) bit-pattern triples, each run
  with `fcr31` preset to 0 and to `0x00800000`: the plain cases, the inclusive boundary and one
  step either side, negative stamps, -0.0 and negative denormals (not negative), denormal windows,
  cancellation to zero, truncation at 2^24, and the overflow and exponent-255 patterns of the
  corrected unit test. Results 0 and 1 both occur, and the condition bit is seen going both ways.
- **`stamped_value_if_fresh`**, 171: the same triples and four more around the -1.0f "empty"
  pattern (exactly -1.0f, one step either side), each with three stored words (`0x01A3C840`,
  `0x80123454`, 0) and both `fcr31` presets.
- **`slot_pool_register_entry`**, 151: the entry and its descriptor in buffers at fixed addresses
  with the four globals set by `--poke`: owner byte 0, 1 and 0x80; skip flag and second-sub-object
  flag 0 and 1; both active words zero and non-zero; slot count 0, 3 and 23 (144 cases); six
  cases with the globals as the snapshot has them; one with count 24 and a running total about to
  wrap. The writes into the pool and the globals are compared byte for byte.
- **`compare_ints_cyclic_from_pivot`**, 205: seven values (-5, -1, 0, 3, 7, the most negative and
  most positive ints) as both elements against four pivots poked into `0x004E19F8` (-1, 0, 3, the
  most negative int), and nine pairs with the pivot as the snapshot has it. Results -1, 0 and 1
  all occur.

The race corpus matters for the functions with globals: the recompiled side reads the pivot and
the pool flags through `gp`, which is `0x004E8670` in all 23 states.

## Not covered

- No natural call was replayed: every case builds its own objects, except the one
  `boost_stats_as_ints` call on the real object and the cases that leave the globals as the
  snapshot has them. Entry snapshots (Part 5 of the harness spec, needs Part 3b) would give real
  arguments.
- Hooks still take effect only for calls that go through `dispatch()`. All eight are called
  directly (`jal`) by their callers, or through `qsort`'s indirect call in the comparator's case,
  so in a live run only the comparator would actually be replaced until Part 3b is applied.
- Null pointers, a ring header or record outside RAM, and slot counts beyond the pool are
  preconditions and were not exercised beyond the one count-24 case.
- The three sibling stamp functions are not hooked or tested.

## Symbol rows

Eight rows added to `game/symbols/SLUS_210.50.tsv` with status `verified`, evidence `CX`;
`symdb.py validate` passes (222 symbols) and `game/hooks/burnout3.hooks` was exported again
(19 functions). `tools/progress.py`: verified 19 functions, 2,176 bytes of 2,819,928 (0.07%).

## Next

1. Part 3b of the harness spec (the entry check in generated code and one regeneration), so that
   hooks replace direct calls in live play and entry snapshots become possible. Until then a
   verified function is proven but not actually running in the game.
2. The runner-ups of log 001 and the siblings named in both logs (`0x00156080`, `0x001560D0`,
   `0x00156120`, `0x0014E830`, `0x00251920`, `0x002519B0`, `0x00251EF0`, `0x0015A7D0`) are cheap
   next targets: their layouts are already known.
