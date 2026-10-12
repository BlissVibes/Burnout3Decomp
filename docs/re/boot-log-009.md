# Headless run 009 — 2026-10-11

Fork: branch `burnout3` at bbb10d4 (gaps 001-006 committed) plus the uncommitted gap-007
change (`runtime/src/iop/spu2.cpp`, `runtime/src/system.cpp`). Command:
`tools/port.sh --frames 1800`, run in the foreground from WSL, frames written 22:34-22:35,
port rc 0 without hitting the log-size or run-time limit. `entries.txt` held `469ab0`.
Wrapper log `~/b3/out/port-009.log`; explore.log, entries.txt and the six frames copied to
`~/b3/out/run009/`. (A first attempt started detached with `nohup ... &` died with its
wsl.exe session and left a 0-byte log.)

**The game boots to the title screen: LOADING completes, the EA and Criterion logos and the
intro FMV play, and frame 1800 shows "Press START button to continue". 0 IOP faults.**

## eerecomp
```
225 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```

## Exploring rounds
```
round 1: frame limit reached, 0 new entry points
```
`[5/6]`: none recorded. `[6/6]`: the web build rebuilt `system.cpp.o` and `spu2.cpp.o` and
linked `ps2web.html`; `ps2web.wasm` 24,691,349 bytes (only the pre-existing em++ pthread +
ALLOW_MEMORY_GROWTH warning).

## Scan
```
frames reached: 1800 of 1800
halted: no (frame limit reached)
primitives drawn: 206507
gaps: 1 distinct, 1 total
  iop_missing  modload #18 called from 0x0006C1B4  x1
result: GAPS
```
The one finding pre-dates this run (same `modload #18` line as runs 006-008). `loadfile`
no longer shows as an unknown tag: the scanner learned it in repo commit a7df7da.

## Frames
Six frames, 640×448, `frame_00300` … `frame_01800`, all different.

| Frame | Visible |
|---|---|
| 300 | The LOADING screen: LOADING in light-blue capitals between two thin blue lines; the progress bar near the bottom is filled almost to its right end (run 008: three quarters, frozen). |
| 600 | EA GAMES logo on a blue background: "Challenge Everything", "EA GAMES is an ELECTRONIC ARTS brand". |
| 900 | Criterion Games logo (bull emblem, "Criterion GAMES") on black. |
| 1200 | Fully black: a transition between intro pieces. |
| 1500 | Intro FMV: a blue holographic globe (Europe and Africa) with a HUD and columns of numbers on the right. |
| 1800 | Burnout 3 Takedown title screen: "A Criterion Games Production in association with Electronic Arts", the logo over a city street, "Press START button to continue", "© 2004 Criterion Software Limited. All rights reserved." |

The 600-frame repros' frames agree: frame 400 is black, frame 600 is the EA logo.

## What the run did
- explore.log is 10 KB (10,311 bytes, 199 lines, no trace flags): 1800 frames,
  8,851,316,108 EE cycles, `[iop] 0 faults`, no `did not return`, `below the user area` or
  `[iopheap] method` line. It created a new memory card
  (`[mc] card in slot 1: .../explore.ps2 (new, unformatted)`). The IOP side prints the same
  B3ROUTE TTY lines as run 008 (`B3Route:Replaced interrupt handler at 0x0003ee20 with data
  0x00046684`, `B3:Meminterrupt A:[3800] B:[3900] C:[3a00] D:[3b00]`, ...); the same 27
  modules load.
- GS: 206,507 primitives (145,509 in run 008), 1,069 GPU submits, 48,827 texture uploads,
  493 CLUT loads, 49,114 raster passes. DMA starts: ch1=1064 ch2=1545 ch3=377347
  ch4=1564 ch8=49567 ch9=594815 (run 008: ch3, ch4 and ch9 were 0). GIF tags: path 2
  `packed 391194 reglist 103335 image 190400`, path 3 `packed 8048 reglist 1062 image 2428`.
  VIF1 ran 2,507 code-06 (MSKPATH3) commands.
- The IPU is busy for the first time:
  `[ipu] commands: 0:1418 2:376232 3:1649377 4:596863 5:88 6:1 7:1115 9:1 stalls 148 out
  63186816 qwords`, the logos and the intro FMV.

From the traced 600-frame repros (`~/b3/out/repro-009.log`, `PS2_TRACE_SPU2=1
--trace-iop-hle --trace-syscalls`, 18.4 MB, 212,372 lines; `~/b3/out/repro-009b.log`, the
same plus `--trace-iop`, 19.0 MB, 217,859 lines; both exit 0, fresh memory cards under
`~/b3/out/`):
- The SPU2 IRQ clock ticks: 1,862 `[spu2] core 0 irq` lines, alternating halfword 0x400
  and 0x500, 196,608 cycles apart (first at `time 3063658`, `repro-009b.log` line 1249),
  each delivered as `[iop] spu2 interrupt -> 0x000374E0` and followed by
  `thbase:iWakeupThread(00000017, ...) ra 0x0003EE68`. No core 1 IRQ.
- RWA's stream engine runs: 662 `[iop-dma] start ch 7` (channel 4: 0). The first cid-5
  packet (line 46823) is uploaded in three channel-7 pieces from IOP `0x102700`,
  `0x103F40` and `0x105780`, after which
  `thevent:SetEventFlag(00000014, 00000002, 00000001, 00046A70) ra 0x0003F91C thread 22`
  (line 47230) releases thread 18 and RWA replies with code 0xD.
- RWA answers: 717 cid-1 replies, among them 109 × 0x33 for 109 × 0x32 (the 4th
  included), 71 × 0x47 for 71 × 0x46, 373 × 0x14 for 373 × 0x14, and 157 × 0xD, the same
  count as `SetEventFlag(00000014, ...)` (86 cid-0 commands with code 5 were sent; which
  commands the other 0xD replies answer was not checked).
- EE: semaphore 19 (thread 7's reply pump) is signalled 717 times, once per reply, and
  waited on 718 times; semaphore 26 (thread 8) is signalled 4 times and waited on 5 times.
- IOP dump at frame 600: RWA thread 18 `wait 3/11 pc 0x000412D8` (its command
  semaphore), threads 19/20 `wait 3/15`/`3/17` at `0x3D850`, threads 21-23 `wait 1/0` at
  `0x3F698`.

End state (explore.log from the halt line on; the IOP thread list is cut to the RWA
threads and a few others):
```
[ee] halted: frame limit reached
[thread] 1 status 1 prio 50 wait 0/0 pc 0x001F24E1 ra 0x001F2518
[thread]   stack: 001F4480 00132F40 0012ECE8 00100210 00115248 00115150 001001F0
[thread] 2 status 4 prio 0 wait 2/3 pc 0x0010BE68 ra 0x0010CB68
[thread] 3 status 2 prio 54 wait 0/0 pc 0x0010BD48 ra 0x00237188
[thread] 4 status 2 prio 54 wait 0/0 pc 0x0010BD48 ra 0x00237188
[thread] 5 status 2 prio 54 wait 0/0 pc 0x0010BD48 ra 0x00237188
[thread] 6 status 4 prio 33 wait 1/0 pc 0x0010BD48 ra 0x0022E248
[thread] 7 status 4 prio 22 wait 2/19 pc 0x0010BE68 ra 0x002AF330
[thread] 8 status 4 prio 1 wait 2/26 pc 0x0010BE68 ra 0x00242AA8
[sema] 3 count 0 waiters 1
[sema] 19 count 0 waiters 1
[sema] 26 count 0 waiters 1
[iop] 0 faults
[iop] thread 17 status 4 prio 115 wait 2/0 pc 0x00041F4C
[iop] thread 18 status 4 prio 114 wait 3/11 pc 0x000412D8
[iop] thread 19 status 4 prio 12 wait 3/15 pc 0x0003D850
[iop] thread 20 status 4 prio 12 wait 3/17 pc 0x0003D850
[iop] thread 21 status 4 prio 9 wait 1/0 pc 0x0003F698
[iop] thread 22 status 4 prio 9 wait 1/0 pc 0x0003F698
[iop] thread 23 status 4 prio 9 wait 1/0 pc 0x0003F698
[iop] thread 37 status 4 prio 43 wait 3/35 pc 0x00079A0C
[iop] thread 58 status 4 prio 68 wait 4/46 pc 0x000E5A14
[iop] thread 63 status 4 prio 10 wait 3/52 pc 0x0003A74C
[iop] thread 64 status 4 prio 123 wait 3/54 pc 0x00039B9C
[sif] 0 packets wait for the EE (processed 1)
[gs] 206507 primitives drawn
[dma] starts: ch0=0 ch1=1064 ch2=1545 ch3=377347 ch4=1564 ch5=0 ch6=0 ch7=0 ch8=49567 ch9=594815
[ee] stopped after 1800 frames, 8851316108 cycles: frame limit reached
```

## Where it stops
It does not stop. At frame 1800 the game is on the title screen waiting for START. EE
thread 1 is running (status 1; `pc 0x001F24E1` is the value the dump prints for the running
thread, not a wait), threads 3-5 are ready. Threads 7 and 8 sit on semaphores 19 and 26
again, but that is their idle state here: both semaphores are signalled throughout the run.
RWA's IOP threads are all in idle waits, and thread 18 no longer sits at `0x3FCF4`.

All five presses (300 start, 600/900/1200/1500 cross) landed during loading, the logos and
the FMV, so none of them reached a menu. The next stall, if any, lies past the title.

## Traced repros
`~/b3/out/repro-009.log` and `repro-009b.log` as above. Each finished in under a minute.
Shots, misses and memcards for each are beside them under `~/b3/out/`.

## Verdict
Gap 007 fixed (`docs/re/gap-007-spu2-output-areas-irq-clock.md`): the SPU2 mixer now writes
its output areas, RWA's IRQ clock ticks, its stream engine uploads to SPU2 RAM and answers
every command, the loader finishes and the game reaches the title screen in 1800 frames
with 0 IOP faults. Still GAPS in the scanner's terms, for the old IOP `modload #18` only.
Next: a run with a press schedule that gets past the title, for example `--frames 3000`
with `--press 1900:start` and later cross presses; if it stalls, report the last `[sif]`
and `[sys]` lines, the EE pc and the `[iop]` dump.
