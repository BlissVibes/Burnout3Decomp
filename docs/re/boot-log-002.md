# Headless run 002 — 2026-10-10

Fork commit: 17a5eee43eb013d43f26ace6dbff5139e5e8043c. Command: `tools/port.sh --frames 1800`.

Change since run 001: `games/burnout3/entries.txt` was seeded by hand with all 158
pointers of the static-constructor table at `0x004DD820..0x004DDA94` (8 lines → 158).
The list is kept at `~/b3/out/ctor-table.txt`. No fork change.

## eerecomp
```
237 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```
`generated/burnout3` is 54 MB, 238 `.cpp` files (237 `unit_*.cpp` + `table.cpp`).

## Exploring rounds
```
round 1: frame limit reached, 0 new entry points
```
`[5/6]`: `none recorded, the page compiles them on the fly`. `[6/6]` built
`ps2web.wasm` (24,731,032 bytes; 5,627,599 gzipped).

## Scan
```
frames reached: none of 1800
halted: frame limit reached
gaps: 0 distinct, 0 total
unknown tags: gif x3, sema x1, vif0 x1, vif1 x1
result: GAPS
```
Two scanner notes. The run did reach frame 1800: the runtime's last line is
`[ee] stopped after 1800 frames, 10616841860 cycles: frame limit reached`, but it never
prints the `[ee] frame N` line the scanner looks for, so `frames reached` says none. And
the runtime reports an ordinary end of run as `[ee] halted: frame limit reached`, which
the scanner counts as a halt. As written the scanner cannot print CLEAN for any run of
this runtime.

explore.log, complete (25 lines):
```
[gpu] llvmpipe (LLVM 20.1.2, 256 bits) (Mesa 25.2.8-0ubuntu0.24.04.4 (LLVM 20.1.2))
[disc] /home/mark/b3/ps2-web-recomp/games/burnout3/disc.iso: 834 files, 1400304 sectors, layer 1 at 0
[mc] card in slot 1: /home/mark/b3/ps2-web-recomp/games/burnout3/explore.ps2 (new, unformatted)
[sif] IOP reboot requested with "rom0:UDNL cdrom0:\IOP\DNAS280.IMG;1"
[sif] LOADFILE from cdrom0:\IOP\DNAS280.IMG;1 reports 2800
[vu0] cfc2 28
[vu0] cfc2 28
[vu0] cfc2 28
[ee] halted: frame limit reached
[thread] 1 status 1 prio 1 wait 0/11 pc 0x0010C2F1 ra 0x0010C2E4
[thread]   stack: 00102E34 001FD788 001FE3A0 001E2D40 001E2AC0 00227FC0 002281D4 0012DC08 0022818C 00133DF4 0012EB6C 00100210 00115248 00115150 001001F0
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
[vif1] 01:1 02:1 03:1 04:1 05:1 06:1 20:1
[ee] stopped after 1800 frames, 10616841860 cycles: frame limit reached
```

## Frames
None. `games/burnout3/shots` is empty. The run lasted all 1800 frames, but the GS drew
0 primitives and the GPU received 0 submits, so there was nothing to capture.

## Traced repro (Task 6 Step 1 form)
600 frames with `--trace-syscalls --trace-iop-hle --trace-iop --trace-threads`; log at
`~/b3/out/repro-002.log` (345 lines). What it shows:

- All 158 constructors run; no `no translated code` line anywhere.
- No `unhandled syscall`, no `[iop] missing`, no `[io] ... unsupported device`.
- SIF traffic, in order: bind + call server `0x80000592` fn 0; IOP reboot with
  `rom0:UDNL cdrom0:\IOP\DNAS280.IMG;1` (`LOADFILE ... reports 2800`); after the reboot
  bind + call `0x80000592` fn 0, bind + call `0x80000593` fn 34 (arg 2), bind + call
  `0x8000059C` fn 0, bind + call `0x80000001` fn 255 (send 8, recv 8). Every call got
  its `to EE: command 0x80000008` reply. That is the last SIF activity of the run.
- After it, thread 1 makes a long series of `GetThreadId` / `WaitSema(1)` /
  `SignalSema(1)` calls (a lock taken and released around small operations), three
  `[vu0] cfc2 28` lines, and then no syscall at all for the rest of the run:
  `[frame] 25 prims 0` through `[frame] 600 prims 0`.
- At the end thread 1 is still runnable (status 1) at `pc 0x0010C2F0`,
  `ra 0x0010C2E4`, the same place as in the 1800-frame run, with the call stack shown
  above. Thread 2 waits on semaphore 3 as it has since start-up.

So the EE is not blocked on a kernel object; it is spinning inside the function around
`0x0010C2E4` without making syscalls, most likely polling memory or a hardware register
for a change that never comes.

## Verdict
GAPS, handed to Fable. The constructor problem from run 001 is gone and the exploration
finds no more missing code, but the game hangs in a busy loop at `0x0010C2F0` shortly
after the post-reboot SIF calls and never draws. The scanner sees no known gap string, so
this is not one of the logged runtime gaps; it needs the loop at `0x0010C2E4` read to see
what it waits for.
