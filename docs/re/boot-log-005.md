# Headless run 005 — 2026-10-10

Fork: branch `burnout3` at 15b4c42 plus the then-uncommitted gap-003 change
(`runtime/src/gs/gif.cpp`, `gif.hpp`, `runtime/src/vif.cpp`). Command:
`tools/port.sh --frames 1800`. `entries.txt` held the one entry from run 004 (`469ab0`).

## eerecomp
```
225 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```

## Exploring rounds
```
round 1: frame limit reached, 0 new entry points
```
`[5/6]`: none recorded (`games/burnout3/vu1` is empty). `[6/6]`: `ps2web.wasm`
24,686,260 bytes (5,616,057 gzipped).

## Scan
```
frames reached: 1800 of 1800
halted: no (frame limit reached)
primitives drawn: 1
gaps: 0 distinct, 0 total
result: CLEAN
```

End-of-run dump (explore.log from the halt line on; the ten lines before it are the same
as in run 004):
```
[ee] halted: frame limit reached
[thread] 1 status 1 prio 10 wait 0/11 pc 0x001F1A29 ra 0x001F2604
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
[gpu] 7 submits, dispatches: upload 1 copy_read 0 copy_write 0 load_clut 0 display 6 raster 1 resolve 0
[gs] 1 primitives drawn, register writes: 00:1 01:2 05:2 18:2 19:1 1A:3 40:2 41:1 42:1 45:2 46:2 47:2 48:1 4C:2 4D:1 4E:2 4F:1 50:2 51:2 52:2 53:2 7F:1
[ipu] commands: 0:2 5:2 6:1 9:1 stalls 0 out 0 qwords, busy 0 decoding 0 cmd 0x00000000 in 0 bytes bit 0 out 0
[dma] starts: ch0=0 ch1=1 ch2=3 ch3=0 ch4=0 ch5=0 ch6=0 ch7=0 ch8=2 ch9=0
[gif] path 1 tags: packed 0 reglist 0 image 0
[gif] path 2 tags: packed 0 reglist 0 image 0
[gif] path 3 tags: packed 7 reglist 0 image 2
[vif0]
[vif1] 00:21 01:1 02:1 03:1 04:1 05:2 06:2 10:1 20:1
[ee] stopped after 1800 frames, 8851299366 cycles: frame limit reached
```

## Frames
Six frames, 640×448, `frame_00300` … `frame_01800`. All six are solid black (every pixel
0,0,0; identical pixel hash, the same hash as run 004's frames). There is no first
non-black frame.

## What changed against run 004
- Thread 1 left `0x001F19C0`. It now loops at `0x001F1A28`, 0x68 bytes further on, with
  the same caller (`ra 0x001F2604`) and the same call stack: a later wait in the same
  routine.
- One more DMA start on channel 2 (2 → 3). GIF PATH3 went from 2 packed tags to 7 packed
  and 2 image tags; the GS saw register writes `50`, `51`, `52`, `53` (two each) and `7F`
  (one); the GPU did one texture upload. So the routine got as far as sending two image
  transfers down PATH3.
- Still 1 primitive, VIF1 code counts unchanged (`06:2`), threads 3–6 still at their entry
  addresses, never run.

## Traced repro
`~/b3/out/repro-005.log`, 600 frames, all four trace flags, 492 lines. Apart from the
`[gs]`, `[dma]`, `[gif]` and `[gpu]` summary lines above it is line-for-line the same as
`repro-004.log`: the last events are the four `CreateThread`/`StartThread` pairs, the
`[io] write 0x1000F590 = 0x10000 (pc 0x00109C08)` line (the only `[io]` line), and
`ChangeThreadPriority(1, 10)`; then no syscall and no thread switch, and
`[frame] 25 prims 1` through `[frame] 600 prims 1`.

## Verdict
Gap 003's fix does what it was meant to (the first wait is passed) but the run is still
black: the same routine waits again at `0x001F1A28` after the two PATH3 image transfers.
Next gap, for Fable. The scan prints CLEAN again for a black run.
