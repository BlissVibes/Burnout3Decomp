# Headless run 007 — 2026-10-11

Fork: branch `burnout3` at 8aca713 plus the then-uncommitted gap-005 change
(`runtime/src/kernel.cpp`, EE `Deci2Call`). Command: `tools/port.sh --frames 1800`, started
00:10 PDT. `entries.txt` held `469ab0`.

**Stalled run: killed at 01:16 after 65 minutes with an 11.7 GB explore.log and no frames.**
The wrapper never got past `[4/6] exploring`, so there is no scan, no web build and no
end-of-run dump. The log was deleted after its first 3 MB were saved as
`~/b3/out/explore-007-head.log` (48,800 lines); the lines around the first fault are in
`~/b3/out/explore-007-firstfault.txt`.

## eerecomp
```
225 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```

## Exploring rounds
None completed. `[5/6]` and `[6/6]` did not run.

## Scan
Not run (the wrapper was killed during the exploring run).

## Frames
None. `shots/` stayed empty: the run never reached frame 300.

## What the log showed
The first 71 lines are the same as run 006: reboot with `DNAS280.IMG`, the display mode, the
27 `[loadfile]` lines with their `[iop-tty]` banners, the one `modload #18` gap and the same
unhandled I/O lines. Then, for the first time, traffic after the last network module:
```
[loadfile] cdrom0:\IOP\NETWORK\DRTYSCKF.IRX;1
[iopheap] method 4
[iop] unknown instruction 00000030 at 0x0001E000 (ra 0x00001000)
[iop] fault: unknown instruction (pc 0x0001E004 thread 0)
```
and from there on nothing but fault pairs. In the saved 3 MB head: 24,362 fault pairs,
of them 9,204 at `0x0001E000` (word `00000030`) and 7,579 each at `0x0001E010` and
`0x0001E02C` (word `00000001`). Every few thousand lines a guest call hits the runtime's
50M-cycle guard:
```
3321:  [iop] guest call to 0x000E59A4 did not return
12748: [iop] guest call to 0x00040F04 did not return
22175: [iop] guest call to 0x00020584 did not return
31602: [iop] guest call to 0x00020584 did not return
41029: [iop] guest call to 0x00020584 did not return
```
The first call (`0xE59A4`, PPPOE's 1 ms alarm handler) logged exactly 1,624 fault pairs
before giving up; each later call (`0x40F04`, RWA's SIF command handler; `0x20584`,
SIO2MAN's interrupt handler) logged about 4,700 pairs, two log lines per fault, with no
limit. At that rate the log grew by roughly 180 MB a minute.

`[iopheap] method 4` is the HLE SIF RPC server `0x80000003` (`runtime/src/iop/sif.cpp`)
reporting a method it did not handle and answering 0. The EE's call was
`sceSifAllocSysMemory(type 0, size 0x2000, addr 0)`; the 0 reply became a null IOP buffer
that RWA.IRX cleared with `memset`, erasing the HLE return stubs at IOP `0x1000`. See
`docs/re/gap-006-iop-heap-alloc-sysmemory-unhandled.md`.

## Traced repros
Made afterwards by the workflow's readers, 600 frames each, all under `~/b3/out/`:
`repro-007-trace.log` (303 MB, `--trace-iop-hle --trace-syscalls`),
`repro-007-trace-iop.log` (311 MB, plus `--trace-iop`; the fault sequence is at lines
2111-2129, 5,046,800 `unknown instruction` lines, 536 `did not return`) and
`rtreader-probe.log` (150 MB, a probe build). All can be deleted. The relevant lines are
quoted in the gap 006 document.

## Verdict
Gap 005 fixed: the main thread leaves the DECI2 poll at `0x0010D508` and the game goes on
to its next step, an IOP heap allocation for RWA. That allocation is unhandled by the
runtime, and the second-order effect (a guest `memset` over IOP address 0) wedges the IOP
interpreter in an unbounded fault loop. Gap 006. Lesson for the runner: a fault inside a
guest call must end the call and fault logging must be capped, or one bad buffer fills the
disk.
