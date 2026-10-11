# Headless run 001 — 2026-10-10

Fork commit: 17a5eee43eb013d43f26ace6dbff5139e5e8043c. Command: `tools/port.sh --frames 1800`.

WSL: Ubuntu-24.04, 16 processors, 16 GB RAM + 16 GB swap. GPU adapter in the log:
`llvmpipe (LLVM 20.1.2, 256 bits) (Mesa 25.2.8)`, the software fallback.

## eerecomp
```
219 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```
(first pass, empty entries.txt). After round 1 added an entry point every later pass wrote
`235 units`. The fork's port.sh keeps only the last line of eerecomp's output, so no
function or byte count was printed. `generated/burnout3` is 53 MB: 236 `.cpp` files
(235 `unit_*.cpp` + `table.cpp`) and `units.hpp`.

## Exploring rounds
```
round 1: missing code at 0x004D3E00 (ra 0x00100844), 1 new entry points
round 2: missing code at 0x004D3E60 (ra 0x00100844), 1 new entry points
round 3: missing code at 0x004D42F0 (ra 0x00100844), 1 new entry points
round 4: missing code at 0x004D4410 (ra 0x00100844), 1 new entry points
round 5: missing code at 0x004D44C0 (ra 0x00100844), 1 new entry points
round 6: missing code at 0x004D45E0 (ra 0x00100844), 1 new entry points
round 7: missing code at 0x004D5320 (ra 0x00100844), 1 new entry points
round 8: missing code at 0x004D5330 (ra 0x00100844), 1 new entry points
```
All 8 rounds (the fork's default cap) were used. `[4/6]` compiled without errors every
round. `[5/6]`: `none recorded, the page compiles them on the fly`. `[6/6]` built
`build/web-burnout3/ps2web.wasm` (24,575,378 bytes; 5,594,996 gzipped).

`games/burnout3/entries.txt` after round 8 (all 8 lines; it was empty before the run):
```
4d3e00
4d3e60
4d42f0
4d4410
4d44c0
4d45e0
4d5320
4d5330
```

## Scan
```
frames reached: none of 1800
halted: missing code at 0x004D5330 (ra 0x00100844)
gaps: 0 distinct, 0 total
unknown tags: gif x3, sema x1, vif0 x1, vif1 x1
result: GAPS
```

Last round's explore.log, in full after the three start-up lines:
```
[ee] no translated code at 0x004D5330 (ra 0x00100844)
[ee]   r1  0002C668 004D5330 00000001 00665E50
[ee]   r5  004DDDF0 00665330 004DDE50 00665330
[ee]   r9  00652350 00621790 00000000 3E2AAAAB
[ee]   r13 BE2AAAAB 004DE2D0 004E01F0 004DD83C
[ee]   r17 004DDA98 00000000 00000000 00000000
[ee]   r21 00000000 00000000 00000000 004DE2F0
[ee]   r25 004E0400 00000000 00000000 004E8670
[ee]   r29 01FFFC90 00000000 00100844 00000000
[ee] halted: missing code at 0x004D5330 (ra 0x00100844)
[thread] 1 status 1 prio 1 wait 0/0 pc 0x004D5331 ra 0x00100844
[thread]   stack: 0012EB44 00100210 00115248 00115150 001001F0
[thread] 2 status 4 prio 0 wait 2/3 pc 0x0010BE68 ra 0x0010CB68
[thread]   stack:
[sema] 3 count 0 waiters 1
[sif] 0 packets wait for the EE (processed 1)
[gpu] 0 submits, dispatches: upload 0 copy_read 0 copy_write 0 load_clut 0 display 0 raster 0 resolve 0
[gs] 0 primitives drawn, register writes:
[ipu] commands: stalls 0 out 0 qwords, busy 0 decoding 0 cmd 0x00000000 in 0 bytes bit 0 out 0
[dma] starts: ch0=0 ch1=0 ch2=0 ch3=0 ch4=0 ch5=0 ch6=0 ch7=0 ch8=0 ch9=0
[gif] path 1 tags: packed 0 reglist 0 image 0
[gif] path 2 tags: packed 0 reglist 0 image 0
[gif] path 3 tags: packed 0 reglist 0 image 0
[vif0]
[vif1]
[ee] stopped after 2 frames, 13707699 cycles: missing code at 0x004D5330 (ra 0x00100844)
```
The unknown tags (`gif`, `sema`, `vif0`, `vif1`) all come from the state dump the runtime
prints when the EE halts; none is an error line.

## Frames
None. `games/burnout3/shots` is empty: every round stopped after 2 frames, before the
first screenshot at frame 300.

## Cause of the eight halts
The eight missing addresses are the first eight entries of a table of function pointers
stored in the executable at `0x004DD820..0x004DDA94`: 158 pointers, all distinct, all
inside `0x004D3E00..0x004DD7E0`, with a zero word before and after. The caller at
`ra 0x00100844` walks it one entry per call; in the dump above `r16 = 0x004DD83C` is the
slot of the entry that just failed and `r17 = 0x004DDA98` is one past the last slot. This
is the static-constructor list run from start-up code. eerecomp reaches these functions
only through the pointer table, so it does not translate them, and each exploring round
discovers exactly one. At that rate the table alone needs 158 rounds.

## Verdict
GAPS, not a runtime gap: the recompiler misses the 158 static constructors, so the game
halts in start-up after 2 frames and the 8-round cap ends the exploration. No compile
failure. Follow-up in `boot-log-002.md`: the remaining 150 table entries were added to
`games/burnout3/entries.txt` by hand and the run repeated. A generic fix belongs in the
fork (eerecomp should take entry points from pointer tables like this one, or port.sh
should keep exploring while each round still finds something).
