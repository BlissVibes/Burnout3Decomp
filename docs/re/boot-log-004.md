# Headless run 004 — 2026-10-10

Fork: branch `burnout3` at 0797e31 plus two then-uncommitted changes: the gap-002 INTC
latency fix (`runtime/src/hw.cpp`, `hw.hpp`, `system.cpp`) and the stale-unit cleanup in
`recomp/codegen.cpp`. Command: `tools/port.sh --frames 1800`. `entries.txt` empty at the
start.

## eerecomp
```
225 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```
`code 858300 instructions (3352 KiB), functions 6151, entries 66903, rejected seeds 0`
on the first pass. The unit count stayed 225 after round 1 added its entry point.

## Exploring rounds
```
round 1: missing code at 0x00469AB0 (ra 0x00217824), 1 new entry points
round 2: frame limit reached, 0 new entry points
```
`entries.txt` after the run: one line, `469ab0`. `[5/6]`: none recorded. `[6/6]`: web
build linked.

## Scan
```
frames reached: 1800 of 1800
halted: no (frame limit reached)
primitives drawn: 1
gaps: 0 distinct, 0 total
result: CLEAN
```
CLEAN here means no known gap string and the frame limit reached. It does not mean the
game is showing anything: see Frames.

explore.log of round 2, complete (35 lines):
```
[gpu] llvmpipe (LLVM 20.1.2, 256 bits) (Mesa 25.2.8-0ubuntu0.24.04.4 (LLVM 20.1.2))
[disc] /home/mark/b3/ps2-web-recomp/games/burnout3/disc.iso: 834 files, 1400304 sectors, layer 1 at 0
[mc] card in slot 1: /home/mark/b3/ps2-web-recomp/games/burnout3/explore.ps2 (new, unformatted)
[sif] IOP reboot requested with "rom0:UDNL cdrom0:\IOP\DNAS280.IMG;1"
[sif] LOADFILE from cdrom0:\IOP\DNAS280.IMG;1 reports 2800
[vu0] cfc2 28
[vu0] cfc2 28
[vu0] cfc2 28
[gs] SetGsCrt interlace 1 mode 0x2 field 0
[io] write 0x1000F590 = 0x10000 (pc 0x00109C08)
[ee] halted: frame limit reached
[thread] 1 status 1 prio 10 wait 0/11 pc 0x001F19C1 ra 0x001F2604
[thread]   stack: 001F2604 001F4414 001F6130 001E200C 0019DE10 0012EC24 001FFAFC 00222260 001E7A18 001E2040 00100210 00115248 00115150 001001F0
[thread] 2 status 4 prio 0 wait 2/3 pc 0x0010BE68 ra 0x0010CB68
[thread]   stack:
[thread] 3 status 2 prio 54 wait 0/0 pc 0x0022F8B0 ra 0x00001010
[thread]   stack:
[thread] 4 status 2 prio 54 wait 0/0 pc 0x0022F8A0 ra 0x00001010
[thread]   stack:
[thread] 5 status 2 prio 54 wait 0/0 pc 0x0022F800 ra 0x00001010
[thread]   stack:
[thread] 6 status 2 prio 33 wait 0/0 pc 0x0022F890 ra 0x00001010
[thread]   stack:
[sema] 3 count 0 waiters 1
[sif] 0 packets wait for the EE (processed 1)
[gpu] 7 submits, dispatches: upload 0 copy_read 0 copy_write 0 load_clut 0 display 6 raster 1 resolve 0
[gs] 1 primitives drawn, register writes: 00:1 01:2 05:2 18:2 19:1 1A:3 40:2 41:1 42:1 45:2 46:2 47:2 48:1 4C:2 4D:1 4E:2 4F:1
[ipu] commands: 0:2 5:2 6:1 9:1 stalls 0 out 0 qwords, busy 0 decoding 0 cmd 0x00000000 in 0 bytes bit 0 out 0
[dma] starts: ch0=0 ch1=1 ch2=2 ch3=0 ch4=0 ch5=0 ch6=0 ch7=0 ch8=2 ch9=0
[gif] path 1 tags: packed 0 reglist 0 image 0
[gif] path 2 tags: packed 0 reglist 0 image 0
[gif] path 3 tags: packed 2 reglist 0 image 0
[vif0]
[vif1] 00:21 01:1 02:1 03:1 04:1 05:2 06:2 10:1 20:1
[ee] stopped after 1800 frames, 8851299370 cycles: frame limit reached
```

## Frames
Six frames were written, `frame_00300.ppm` … `frame_01800.ppm`, 640×448 each. All six
are identical and solid black (every pixel 0,0,0; the same SHA-1 of the pixel data).

| Frame | Visible |
|---|---|
| 300 | black |
| 600 | black |
| 900 | black |
| 1200 | black |
| 1500 | black |
| 1800 | black |

## What changed against run 003
- Thread 1 no longer spins at `0x0010C2F0`. Gap 002 is fixed.
- The game goes on to set the display mode (`SetGsCrt interlace 1 mode 0x2 field 0`),
  draw 1 GS primitive, start DMA on channels 1, 2 (×2) and 8 (×2), send IPU commands
  0, 5, 6 and 9, and create four more threads (3, 4, 5 at priority 54, 6 at priority 33).
- One new piece of missing code in round 1, `0x00469AB0` called from `0x00217824`,
  found and translated by the normal exploring loop.

## Traced repro (600 frames, `--trace-syscalls --trace-iop-hle --trace-iop --trace-threads`)
Log: `~/b3/out/repro-004.log` (492 lines). The last things the game does, in order:

1. Creates and starts threads 3 (entry `0x0022F8B0`), 4 (`0x0022F8A0`), 5 (`0x0022F800`),
   all priority 54, and 6 (`0x0022F890`), priority 33.
2. `[io] write 0x1000F590 = 0x10000 (pc 0x00109C08)`: a write the runtime reports as an
   unhandled I/O register.
3. `GetThreadId`, then `ChangeThreadPriority(1, 10)`: thread 1 sets its own priority
   to 10.
4. No syscall and no thread switch after that. `[frame] 25 prims 1` through
   `[frame] 600 prims 1`: the primitive count never rises above 1.

End state, the same at 600 and 1800 frames: thread 1 runnable (status 1, priority 10) at
`pc 0x001F19C0`, `ra 0x001F2604`; threads 3–6 are ready (status 2) but have never run,
each still at its entry address; thread 2 waits on semaphore 3.

So thread 1 is in another loop without syscalls, at `0x001F19C0`, and because it never
blocks, the four lower-priority threads it just started never get the CPU.

## Verdict
Gap 002 fixed; the run is further along but still shows nothing. Next gap, for Fable: the
busy loop at `0x001F19C0` (caller `0x001F2604`), right after the unhandled write of
`0x10000` to `0x1000F590`. The scanner prints CLEAN for this run, so a clean scan is not
enough for the milestone-1 exit: the frames have to be looked at too.
