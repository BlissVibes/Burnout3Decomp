# Headless run 006 — 2026-10-11

Fork: branch `burnout3` at 6862f7b plus the then-uncommitted gap-004 change
(`runtime/src/gs/gif.cpp`, `runtime/src/vif.cpp`). Command: `tools/port.sh --frames 1800`.
`entries.txt` held `469ab0`.

**First visible picture of the port: the game's LOADING screen.**

## eerecomp
```
225 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```

## Exploring rounds
```
round 1: frame limit reached, 0 new entry points
```
`[5/6]`: none recorded. `[6/6]`: `ps2web.wasm` 24,686,837 bytes (5,615,720 gzipped).

## Scan
```
frames reached: 1800 of 1800
halted: no (frame limit reached)
primitives drawn: 143
gaps: 1 distinct, 1 total
  iop_missing  modload #18 called from 0x0006C1B4  x1
unknown tags: loadfile x27
result: GAPS
```
The scanner does not know the `loadfile` tag (one line per IOP module the game loads).
The log also has `iop-tty` and `iop-io` lines, which the scanner did not list as unknown.

## Frames
Six frames, 640×448, `frame_00300` … `frame_01800`. All six are identical (same pixel
hash; 5,844 of 286,720 pixels are not black).

| Frame | Visible |
|---|---|
| 300–1800 | Black background. The word LOADING in light blue capitals, centred, between two thin horizontal blue lines that fade out towards the screen edges. Below it, near the bottom, an empty progress bar: a thin light outline, nothing filled. |

The bar never advances during the run.

## What the run did
- Threads 3–6 ran: each now waits (status 4) inside the game instead of sitting at its
  entry address.
- GS: 143 primitives, 2 texture uploads, 2 CLUT loads. DMA starts: ch1=3, ch2=5, ch8=8.
  VIF1 ran 484 no-ops, 9 code-06 (MSKPATH3), 12 code-11, 10 code-13 and 8 code-50
  commands. GIF tags: path 2 `packed 17 reglist 16`, path 3 `packed 14 reglist 1 image 4`.
- The game loaded 27 IOP modules from the disc, in this order: SIO2MAN, SIO2D, DBCMAN,
  DS2O (twice), MC2_S1, LIBSD, RWA, B3ROUTE, GTFSCDVD, USBD, LGAUD, LGDEVW, then from
  `IOP\NETWORK`: INET, NETCNF, INETCTL, MSIFRPC, LIBNET, NETCNFIF, IFCONFIG, EZNETCNF,
  EZNETCTL, DEV9, SMAP, PPP, PPPOE, DRTYSCKF. Their start-up messages appear as
  `[iop-tty]` lines; the IOP dump at the end lists 27 modules and 62 threads.
- One logged runtime gap: `[iop] missing modload #18 called from 0x0006C1B4`, during
  LGAUD's start-up. LGAUD still printed its banner and went on.
- Unhandled I/O the runtime reported (one line each): EE `write 0x1000F590 = 0x10000`;
  IOP reads of `0x1F808280`, `0x1F8010F0`, `0x1F801570`, `0x1F80146E` (the last from
  DEV9, which then prints `dev9: unknown dev9 hardware.`) and IOP writes to
  `0x1F801404`, `0x1F80140C`, `0x1F801014`, `0x1F801414`, `0x1F801680`.

End state of the EE threads:
```
[ee] halted: frame limit reached
[thread] 1 status 1 prio 10 wait 0/13 pc 0x0010D509 ra 0x0010D514
[thread]   stack: 0010D250 0010C440 0012BE60 00126D48 00123018 0012165C 00122C84 00129488 0012BF50 0012ABEC 00129488 0012ABEC 001F7E44 001F3A6C 0010E638 0012532C 00443920 00126BBC 004428A4 00214588 001D4570 001F2650 00133294 001F2A84 00313B58 002222B0 001331AC
[thread] 2 status 4 prio 0 wait 2/3 pc 0x0010BE68 ra 0x0010CB68
[thread] 3 status 4 prio 54 wait 1/0 pc 0x0010BD48 ra 0x00237188
[thread] 4 status 4 prio 54 wait 1/0 pc 0x0010BD48 ra 0x00237188
[thread] 5 status 4 prio 54 wait 1/0 pc 0x0010BD48 ra 0x00237188
[thread] 6 status 4 prio 33 wait 1/0 pc 0x0010BD48 ra 0x0022E248
[sema] 3 count 0 waiters 1
[sif] 0 packets wait for the EE (processed 1)
[gpu] 8 submits, dispatches: upload 2 copy_read 0 copy_write 0 load_clut 2 display 6 raster 4 resolve 0
[gs] 143 primitives drawn, register writes: 00:12 01:117 02:28 03:40 05:226 06:2 07:1 08:3 09:1 14:4 15:1 18:8 19:6 1A:3 34:2 36:2 3B:2 3D:2 3F:2 40:8 41:4 42:4 43:1 44:2 45:8 46:3 47:8 48:4 49:1 4C:10 4D:4 4E:7 4F:1 50:4 51:4 52:4 53:4 7F:6
[ipu] commands: 0:2 5:2 6:1 9:1 stalls 0 out 0 qwords, busy 0 decoding 0 cmd 0x00000000 in 0 bytes bit 0 out 0
[dma] starts: ch0=0 ch1=3 ch2=5 ch3=0 ch4=0 ch5=0 ch6=0 ch7=0 ch8=8 ch9=0
[gif] path 1 tags: packed 0 reglist 0 image 0
[gif] path 2 tags: packed 17 reglist 16 image 0
[gif] path 3 tags: packed 14 reglist 1 image 4
[vif0]
[vif1] 00:484 01:1 02:1 03:1 04:1 05:2 06:9 10:1 11:12 13:10 20:1 50:8
[ee] stopped after 1800 frames, 8851301181 cycles: frame limit reached
```

## Traced repro
Not kept. A 900-frame run with `--trace-syscalls` wrote 19 GB before I stopped it: the
loop thread 1 is in makes a system call on every pass, so the syscall trace grows without
bound. The file was deleted. For this state, trace without `--trace-syscalls`, or cap the
log (`| head -c`), or use far fewer frames.

## Verdict
Gap 004 fixed: the wait at `0x001F1A28` is passed and the game renders its loading
screen. Still GAPS. The main thread now loops at `0x0010D508` (`ra 0x0010D514`) right
after the last network module (DRTYSCKF) loads, and the loading bar does not move. That
is gap 005 (`docs/re/gap-005-deci2-tty-send-never-completes.md`). Also open: IOP
`modload #18` is missing in the runtime.
