# Headless run 008 — 2026-10-11

Fork: branch `burnout3` at 8aca713 plus the uncommitted gap-005 change (`runtime/src/kernel.cpp`)
and gap-006 change (`runtime/src/iop/sif.cpp`, `kernel.cpp`, `kernel.hpp`, `cpu.cpp`,
`libs.cpp`). Command: `tools/port.sh --frames 1800`, started 02:12 PDT, finished in 49 s,
exit 0. `entries.txt` held `469ab0`. Wrapper log `~/b3/out/port-008.log`; explore.log and
entries.txt copied to `~/b3/out/run008/`.

**The LOADING bar now shows progress, and the IOP runs clean: 0 faults.**

## eerecomp
```
225 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```

## Exploring rounds
```
round 1: frame limit reached, 0 new entry points
```
`[5/6]`: none recorded. `[6/6]`: the web build rebuilt the changed runtime objects and linked
`ps2web.html`; `ps2web.wasm` 24,689,691 bytes (only the pre-existing em++ pthread +
ALLOW_MEMORY_GROWTH warning).

## Scan
```
frames reached: 1800 of 1800
halted: no (frame limit reached)
primitives drawn: 145509
gaps: 1 distinct, 1 total
  iop_missing  modload #18 called from 0x0006C1B4  x1
unknown tags: loadfile x27
result: GAPS
```
Both findings pre-date this run (same `modload #18` line as runs 006 and 007; `loadfile`
is still not in the scanner's known tags). Also noted: the scanner's tag pattern does not
match `iop-tty` and `iop-io` because of the hyphen, so those are silently ignored rather
than listed as unknown.

## Frames
Six frames, 640×448, `frame_00300` … `frame_01800`. All six are byte-identical (`cmp` and a
pixel diff: 0 differing pixels).

| Frame | Visible |
|---|---|
| 300–1800 | Black background. The word LOADING in light-blue capitals, centred, between two thin horizontal blue lines fading towards the edges. Near the bottom a thin outlined progress bar whose left three quarters are filled light blue, the right quarter empty. |

Against run 006 (bar completely empty) the bar now shows real progress, but it does not
move between frame 300 and frame 1800. The 600-frame repros' frames (100 … 600) are the
same picture.

## What the run did
- explore.log is 10 KB (no trace flags): 1800 frames, 8,851,292,209 EE cycles, no fault,
  `did not return`, `below the user area` or `[iopheap] method` line anywhere.
- After `[loadfile] cdrom0:\IOP\NETWORK\DRTYSCKF.IRX;1` the log has five new `[iop-tty]`
  lines from B3ROUTE that no earlier run reached:
  ```
  [iop-tty] IOP:B3DIST_CmdHandler 1/1 got command ID of 32768
  [iop-tty] IOP:B3Route:Finished Initialisation and setup of cores with channel mask of [0]..
  [iop-tty] IOP:B3:Init Params Buffer:[1e7ac80]Channels:[0]
  [iop-tty] IOP:B3:Init set target EE buffer to [1e7ac80]. Local CleanA[4b880] CleanB[4b280] Fuzzy[4aa80]
  [iop-tty] IOP:B3:Meminterrupt A:[3800] B:[3900] C:[3a00] D:[3b00]
  ```
- GS: 145,509 primitives (143 in run 006), 1,797 texture uploads, 1,799 CLUT loads, 3,594
  raster passes. DMA starts: ch1=1794, ch2=3587, ch8=3591. GIF tags: path 2
  `packed 12576 reglist 14347`, path 3 `packed 12568 reglist 1792 image 3593`. VIF1 ran
  417,808 no-ops and 7,173 code-06 (MSKPATH3) commands. So the game redraws the loading
  screen every frame: one GIF submit per frame, one libpad RPC per frame.
- EE threads 7 and 8 exist now (run 006 had six). The IOP dump lists 27 modules and 64
  threads (62 in run 006).

From the traced 600-frame repro with `--trace-iop --trace-iop-hle --trace-syscalls`
(`~/b3/out/repro-008b.log`, 123,294 lines):
- IOP heap calls, all answered: `fn 4 ... args 00002000` at line 2111 (reply `00100700`,
  which the next cid-0 packet to RWA carries as `00100700 00002000`, and which RWA then
  clears with `sysclib:memset(00100700, 00000000, 00002000, ...) ra 0x00042A78 thread 18`),
  `fn 4 ... args 00004000` at line 35056 and `fn 4 ... args 00040000` (the audio stream
  buffer) at line 35130. No `[iop] out of memory`.
- `[sif] bind 0x00475453` (GTFSCDVD's "GTS" server) at line 35140, then
  `call server 0x00475453 fn 1` and 18 more GTS calls (`fn 3` open, `fn 5` read, `fn 4`
  close) for `Data\Global.txt`, `Data\GlobalUs.bin`, `Data\HeadUs.bin`,
  `pvehicle\vlist.bin`, `Tracks\tlist.bin` and `Data\PrgData.bin` (paths decoded from the
  argument words); the reads are 0x8000-byte double-buffered IOP→EE DMAs from IOP
  `0x146700`/`0x14E700`.
- PPPOE's 1 ms alarm handler returns normally throughout
  (`thevent:iSetEventFlag(0000002E, ...) ra 0x000E59C8 thread 0` recurs to the end).

End state (explore.log from the halt line on; the IOP thread list is cut to the threads
discussed below):
```
[ee] halted: frame limit reached
[thread] 1 status 4 prio 50 wait 1/0 pc 0x0010BD48 ra 0x00237188
[thread]   stack: 0012EC80 00100210 00115248 00115150 001001F0
[thread] 2 status 4 prio 0 wait 2/3 pc 0x0010BE68 ra 0x0010CB68
[thread] 3 status 4 prio 54 wait 1/0 pc 0x0010BD48 ra 0x00237188
[thread] 4 status 4 prio 54 wait 1/0 pc 0x0010BD48 ra 0x00237188
[thread] 5 status 4 prio 54 wait 1/0 pc 0x0010BD48 ra 0x00237188
[thread] 6 status 4 prio 33 wait 1/0 pc 0x0010BD48 ra 0x0022E248
[thread] 7 status 4 prio 22 wait 2/19 pc 0x0010BE68 ra 0x002AF330
[thread]   stack: 002A2124 002AF2A0 002AF340 001D2F50 001D2EE0 002FC530
[thread] 8 status 4 prio 1 wait 2/26 pc 0x0010BE68 ra 0x00242AA8
[sema] 3 count 0 waiters 1
[sema] 19 count 0 waiters 1
[sema] 26 count 0 waiters 1
[iop] 0 faults
[iop] thread 17 status 4 prio 115 wait 2/0 pc 0x00041F4C
[iop] thread 18 status 4 prio 114 wait 4/20 pc 0x0003FCF4
[iop] thread 19 status 4 prio 12 wait 3/15 pc 0x0003D850
[iop] thread 58 status 4 prio 68 wait 4/46 pc 0x000E5A14
[iop] thread 64 status 4 prio 123 wait 3/54 pc 0x00039B9C
[sif] 0 packets wait for the EE (processed 1)
[gpu] 1794 submits, dispatches: upload 1797 copy_read 0 copy_write 0 load_clut 1799 display 6 raster 3594 resolve 0
[gs] 145509 primitives drawn, register writes: 00:10768 01:104287 02:25108 03:71688 05:236940 06:1793 07:1802 08:1794 09:1796 14:3590 15:1796 18:5385 19:5387 1A:3 34:1793 35:4 36:1793 37:4 3B:1793 3D:1797 3F:1803 40:5385 41:3600 42:1795 43:1796 44:1793 45:7176 46:3 47:3594 48:3594 49:1 4C:7178 4D:3600 4E:3589 4F:1 50:3592 51:3593 52:3593 53:3593 7F:5383
[ipu] commands: 0:2 5:2 6:1 9:1 stalls 0 out 0 qwords, busy 0 decoding 0 cmd 0x00000000 in 0 bytes bit 0 out 0
[dma] starts: ch0=0 ch1=1794 ch2=3587 ch3=0 ch4=0 ch5=0 ch6=0 ch7=0 ch8=3591 ch9=0
[gif] path 1 tags: packed 0 reglist 0 image 0
[gif] path 2 tags: packed 12576 reglist 14347 image 0
[gif] path 3 tags: packed 12568 reglist 1792 image 3593
[vif0]
[vif1] 00:417808 01:1 02:1 03:1 04:1 05:2 06:7173 10:1 11:10758 13:8965 20:1 50:7179
[ee] stopped after 1800 frames, 8851292209 cycles: frame limit reached
```

## Where it stops
The EE no longer spins. Thread 1 runs its per-frame loop (`SleepThread` at `0x0010BD44`
from `0x00237188`, woken every vertical blank by `iWakeupThread` from thread 0): the loop at
`0x0012EC78`–`0x0012ECB4` waits for the blank, updates the loading-screen object
`0x00665EC0` and calls `0x00133190` on the loader object `0x004EE040`, repeating while it
returns 0. `0x00133190` dispatches on a state word at `0x0051BAA0` (cases 0 to 0xF); the
stuck value was not read (a `--save-state` dump would give it). Thread 7 (entry
`0x002AF2A0`) is the RWA reply pump, waiting on semaphore 19 at `0x002AF328`; thread 8
(priority 1, entry `0x004E8670`, created at the ninth RWA command) waits on semaphore 26
inside `0x00249680` called from `0x00242AA0`. Neither semaphore is signalled again.

The IOP side is where it stops (from `repro-008b.log`): the EE sent 13 SIF cid-0 command
packets to RWA.IRX (handler `0x40F04`) and RWA answered 10 of them with cid-1 packets. The
two unanswered ones are the cid-5 packet at line 42683,
```
[sif] command 0x00000000: 00000030 00000000 00000000 00000000 00000005 00000000 00102700 00005400 00004000 00000000 00000000 00000001 00000000 00000000
```
(the 0x4000 heap buffer at IOP `0x102700`, after RWA had GTFSCDVD read disc sector 543385
× 8 into it) and the fourth cid-0x32 packet at line 42856 (a 0x70-byte payload from EE
`0x0049FE60` to IOP `0x441A0`; the first three 0x32 packets got cid-0x33 replies). RWA
worker thread 18 (entry `0x42B7C`) handles the cid-5 command at `0x3FB30`–`0x3FBB0`
(`CpuSuspendIntr`, `PollEventFlag`, `ClearEventFlag(0x14, bit 2)`), does something under
`CpuSuspendIntr` at `0x3DED8`–`0x3DF4C` that touches no traced HLE, hardware or DMA, then
```
[iop-hle] thevent:WaitEventFlag(00000014, 00000002, 00000001, 001D8E50) ra 0x0003FCF4 thread 18
```
and stays there to the end (IOP dump: thread 18 `wait 4/20 pc 0x0003FCF4`). Event flag
0x14 bit 2 is never set by anyone in the whole run: 0 `SetEventFlag`/`iSetEventFlag` calls
on flag 0x14.

Related facts: LIBSD registered IOP interrupt handlers 0x24 and 0x28 (SPU2 DMA cores 0/1,
handler `0x35DB8`) and 9 (SPU IRQ, handler `0x374E0`), and RWA enabled the same three; the
`[iop] interrupt N -> handler` line appears once each for 9, 36 and 40, but no SPU2 DMA was
ever started (`[iop-dma] start ch 4/7` absent for the whole run; only SIO2 channels 11/12
run). With `PS2_TRACE_SPU2=1` (`repro-008c.log`) there are 163 SPU2 register writes, the
last at line 35087 (core 0/1 init), none around or after the cid-5 command, and no
`[spu2] core N irq`. Other RWA threads: 17 polls semaphore 0xA with `DelayThread(0x413C us)`
at `0x41F4C`, 19 (priority 12) waits semaphore 15 at `0x3D850`, 16 is dormant.

Guesses, not verified: cid 5 is RWA's "start stream" command on the 0x4000 buffer, and bit
2 of flag 0x14 is meant to be set by a LIBSD transfer/IRQ callback or by RWA thread 19 once
an SPU2 ADPCM stream DMA or SPU IRQ completes; B3ROUTE's `Meminterrupt A:[3800] B:[3900]
C:[3a00] D:[3b00]` line may name the SPU RAM IRQ addresses involved.

## Traced repros
`~/b3/out/repro-008.log` (600 frames, `--trace-iop-hle --trace-syscalls`, 11 MB, 121,518
lines), `repro-008b.log` (plus `--trace-iop`, 123,294 lines), `repro-008c.log` (plus
`PS2_TRACE_SPU2=1`). Each finished in under a minute. Shots, misses and memcard copies for
each are beside them under `~/b3/out/`.

## Verdict
Gap 006 fixed: the IOP heap service answers, RWA gets a real buffer, the IOP runs 1800 frames
with 0 faults and the loading bar fills to about three quarters. Still GAPS. The loading
state machine then waits on RWA, whose worker thread 18 waits on event flag 0x14 bit 2 that
nothing sets. That is the next gap (007). Starting points: RWA.IRX code at `0x3DED8`–`0x3DF4C`
and `0x3FB30`–`0x3FCF4` (what it expects to set flag 0x14 bit 2), LIBSD's handlers
`0x35DB8`/`0x374E0`, the HLE SPU2 ADMA/IRQ path in `runtime/src/iop/spu2.cpp`, and the EE
state word at `0x0051BAA0`. Also still open: IOP `modload #18` is missing in the runtime.
