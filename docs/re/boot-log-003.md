# Headless run 003 — 2026-10-10

Fork: 17a5eee43eb013d43f26ace6dbff5139e5e8043c plus the `PS2_WEB_SHELL` CMake change
(committed since as 414c116) and the then-uncommitted gap-001 patch to
`recomp/analysis.cpp` (`docs/re/gap-001-static-constructor-table.md`). Command:
`tools/port.sh --frames 1800`.

Change since run 002: `games/burnout3/entries.txt` emptied (0 lines; the 158-line hand
seed is kept in `~/b3/out/run002/entries.txt`), and `generated/burnout3` cleared first.

The first attempt failed at `[4/6]`: 12 `unit_*.cpp` files left from run 002's 237-unit
build were still in `generated/burnout3` and referenced units the new 225-unit build no
longer declares (`unit_00ED.cpp:212: error: use of undeclared identifier 'unit_00F3'`).
That ninja log is saved as `~/b3/out/run003/ninja-stale-units.log`. Everything below is
the second attempt, after deleting the generated files.

## eerecomp
```
225 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```
Full summary line from the same binary and inputs:
`code 858300 instructions (3352 KiB), functions 6151, entries 66903, rejected seeds 0`.

## Exploring rounds
```
round 1: frame limit reached, 0 new entry points
```
`entries.txt` is still empty after the run. `[5/6]`: none recorded. `[6/6]`: web build
linked.

## Scan
```
frames reached: none of 1800
halted: frame limit reached
gaps: 0 distinct, 0 total
unknown tags: gif x3, sema x1, vif0 x1, vif1 x1
result: GAPS
```
The scanner notes in `boot-log-002.md` apply unchanged.

explore.log is byte-identical to run 002's (`cmp` reports no difference): same SIF
sequence, same end state `[thread] 1 status 1 prio 1 wait 0/11 pc 0x0010C2F1 ra 0x0010C2E4`,
`[ee] stopped after 1800 frames, 10616841860 cycles: frame limit reached`.

## Frames
None. `games/burnout3/shots` is empty; 0 GS primitives, 0 GPU submits.

## Verdict
Gap 001 is fixed generically: with no hand seed the run gets past the static
constructors and reaches the frame limit in round 1. The run is otherwise the same as
002 and still GAPS: the EE spins at `0x0010C2F0` and nothing is drawn. That hang is the
next gap, with Fable.
