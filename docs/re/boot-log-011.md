# Headless run 011 — 2026-10-12

Fork: branch `burnout3` at 3e42c71, tree clean, no rebuild. Not a `tools/port.sh` run: a
set of `build/native-burnout3/ps2run` probes from the fork root with
`PS2_GPU_FALLBACK=1`, each with its own new memory card, everything under
`~/b3/out/run011/`. Presses are written `frame:button:frames held`.

**First race of the port. From a cold boot, thirteen presses go through Single Event to a
running race whose first frame is 12575. The game logic and the HUD work; the 3D world is
drawn with garbage textures and the player's car is missing. A save-state at frame 12300
replays the race start in 24 seconds.**

## Route
The schedule and every page are in `docs/re/menu-route.md`. In short:
```
1900:start:10 2300:start:10 2500:down:6 2600:cross:10 2800:cross:10 3000:cross:10
3150:up:6 3250:cross:10 3600:down:6 3750:cross:10 3950:cross:10 4150:cross:10
4350:cross:10
```
Title, New Profile (not saved), main menu, Single Event, Race, options left on Region
Random and 5 rivals, the first car, then the unskippable Race Training video (frames 4425
to about 11900), a loading screen (11925-12350), the race intro (12375-12550) and the
race (from 12575).

## Probes

| Probe | From | Frames | Extra | Result |
|---|---|---|---|---|
| `a` | boot | 8000 | cross every 200 after the main menu | menu pages up to the training video |
| `b` | boot | 14500 | the schedule above | the race; 9 minutes of wall time |
| `state` | boot | 12310 | `--save-state 12300:~/b3/out/run011/pre-race.state` | `[state] saved frame 12300 … (45204 KB)`, 3.5 minutes |
| `plain` | state | to 12900 | none | race frames 12575-12900, 56 s |
| `gssoft` | state | to 12900 | `PS2_GS=off` (software renderer, no GPU) | same faults, 87 s |
| `vuint` | state | to 12900 | `PS2_VU_INTERPRET=1` | same faults |
| `vuexact` | state | to 12900 | `PS2_VU_INTERPRET=1 PS2_VU_EXACT=1` | same faults |
| `traceio` | state | to 12700 | `--trace-io` | no extra line at all |
| `tracedma` | state | to 12700 | `PS2_TRACE_DMA=1 PS2_TRACE_DMA1=1 PS2_VU_COUNT=1` | 18 MB log |
| `gsseq` | state | to 12700 | `PS2_TRACE_GSSEQ=5000000` (100,000 GS writes from the 5,000,000th) | 2.7 MB log |

All rc 0, all end with `[ee] halted: frame limit reached`, no misses file (no missing
code), `[iop] 0 faults`.

### The save-state
`--save-state` takes `frame:file`; `--load-state file` restores it, including the frame
counter, so `--frames` is the absolute end frame (12700 for 400 frames).
`~/b3/out/run011/pre-race.state` (46,289,152 bytes) is the repro starting point:
```
build/native-burnout3/ps2run games/burnout3/disc/SLUS_210.50 --disc games/burnout3/disc.iso
  --memcard <new file> --load-state ~/b3/out/run011/pre-race.state --frames 12700
  --frames-dir <dir> --frame-every 25
```
prints `[state] loaded frame 12300` and reproduces the full run: frame 12325 is the
"HAVE YOU TRIED… CRASH AFTERTOUCH?" loading screen, 12425 the race intro ("Fight Through
Traffic To Win The Race!"), 12575 the first race frame (POS 4/6, LAP 1/3, 49 mph). The EE
cycle count at frame 12700 is 62,427,155,031 in every run from the state, with any of the
switches above.

Frames are not bit-identical from run to run even with identical settings (`plain` against
`b`, and `plain` against `vuint`, differ at frame 12700), although they show the same
picture and the same faults.

## The race, frame by frame (`plain`, every 25 frames; the listed ones were looked at)

| Frame | Right | Wrong |
|---|---|---|
| 12600 | HUD (POS 3/6, LAP 1/3, 49 mph, boost bar), sky and clouds | the camera shows only sky |
| 12650 | HUD, sky, horizon haze | a white object with scrambled black print tumbles across the sky; no road in view |
| 12700 | HUD, "CRASHED!" banner, EA Trax logo, lamp post, power pole, distant trees | road is plain white; the car is missing, only a soft shadow; a black box where a building or sign should be |
| 12750 | HUD, banner, traffic light, sky, trees, track pop-up ("The Matches, Audio Blood, E. Von Dahl Killed The Locals") | road pink-white noise; car missing, shadow only; buildings as dark blocks |
| 12800 | HUD, banner, grass verge, trees, lamp post | road black with a magenta strip beside it; building walls pale blocks with colour noise |
| 12850 | HUD, grass, sky, lamp posts | road white with cyan speckle; the banner text is broken up ("CRA H !"); a billboard shows scrambled glyphs; car missing, shadow only |
| 12875 | HUD, sky, hills, trees | road is a red and white circuit-like pattern with a yellow strip; walls flat grey and green noise |
| 12900 | HUD, sky, traffic cones on the right, lamp posts | road white and orange noise; walls dark blue and static |

Across all frames: the HUD, 2D banners and pop-ups, sky, hills, trees, grass, cones and
poles look right. Road, pavement and most building and sign surfaces have the wrong
texture content. The player's car is never drawn; its shadow is. No rival or traffic car
is identifiable in any frame, though the position counter moves (4th, 3rd, later 6th).
With the software renderer (`gssoft`) the 3D picture is the same and the HUD digits are
slightly broken in addition.

## Trace findings

- **No complaint in any log.** No line contains "unimplemented", "unknown", "unsupported",
  "not handled", "not implemented" or "TODO" in any of the seven logs. `--trace-io` adds
  nothing: the runtime met no I/O address it does not handle.
- **Not the GPU backend, not VU1 recompilation.** `PS2_GS=off` gives the same garbage with
  the CPU rasteriser. VU1 is fully interpreted already (`[vu1] compiled 0 interpreted N`),
  and `PS2_VU_EXACT` changes nothing visible.
- **VU1 programs.** `PS2_VU_COUNT` at 400 frames:
  ```
  [vu1] compiled 0 interpreted 55000: 0E48:28 0DE8:101 0D40:1 1398:744 0000:8702 1318:3485 2650:1374 06A0:5436 1070:198 0020:4872 16E0:64 14E0:12561 0DA0:618 0550:182 1048:128 1548:7263 0098:912 1910:6607 0150:30 0EE8:1694
  ```
  Twenty entry addresses. During the loading screen only four are used
  (`[vu1] compiled 0 interpreted N: …:68 0DA0:… …:68 06A0:…`).
- **VIF histograms, 600 frames from the state (`plain`):**
  ```
  [vif0] 00:23204 4A:8666
  [vif1] 00:6066390 01:1077534 02:37857 03:37857 04:285935 05:817244 06:7940 10:163024 11:70716 13:8389 14:37018 15:5807 17:281283 30:45123 4A:22278 50:127850 64:22339 65:152651 6A:139053 6C:518726 6D:85806 6E:158574
  ```
  VIF1: 22,278 microprogram uploads (code 4A), 37,018 MSCAL (14), 5,807 MSCALF (15),
  281,283 MSCNT (17), 45,123 STROW (30), 127,850 DIRECT (50), unpacks 64, 65, 6A, 6C, 6D,
  6E. VIF0: 8,666 microprogram uploads and no-ops only, no MSCAL, so VU0 microcode is
  loaded through VIF0 and can only be started from the EE.
- **DMA.** `[dma] starts: ch0=4975 ch1=449 ch2=898 ch8=19045 ch9=2322` in 600 frames.
  Channel 0 always starts as `ch0 chcr 0x00000145 … qwc 0` (source-chain mode), in pairs,
  from thread 1. Channel 1 starts 249 times in 400 frames, `chcr 0x00000145`; its chain
  tags are id 1 ×140,924, id 5 ×34,617, id 6 ×34,617 (call and return), id 3 ×10,645,
  id 7 ×249. Channel 2 starts as `chcr 0x00000104` (normal mode).
- **GIF.** `[gif] path 1 tags: packed 557775`, path 2
  `packed 3063715 reglist 977311 image 1383120`, path 3
  `packed 445214 reglist 6946 image 236915`.
- **GS, 600 frames:** 11,526,567 primitives, 33,337 texture uploads, 15,698 CLUT loads,
  186,397 raster passes.
  ```
  [gs] 11526567 primitives drawn, register writes: 00:13266893 01:194092 02:974274 03:189717 04:5398633 05:1591349 06:88941 07:1604 08:54017 09:1604 0C:4637045 0D:227714 14:39357 15:1604 18:56079 19:2823 34:30903 36:30903 3B:834 3D:1219 3F:31726 40:58086 41:2823 42:108926 43:45957 44:449 45:2502 47:59141 48:2823 4C:69770 4D:2823 4E:62202 4F:770 50:59298 51:158202 52:158202 53:158202 7F:228768
  ```
  Registers 34 and 36 (MIPTBP1_1, MIPTBP2_1) are written 30,903 times each in these 600
  frames. They are not new to the race: run 010 wrote each 5,407 times in 3600 frames up
  to the title screen. Whether those earlier writes were non-zero was not checked.
- **Texture setup in the race** (the 100,000-write sample):
  - TEX0 formats: 8-bit indexed (T8) and 4-bit indexed (T4) with a 32-bit CLUT in CSM1
    are the bulk (T8 256×256 ×74, T4 128×128 ×32, T4 256×256 ×18, T8 128×128 ×16, and
    smaller counts of other sizes); T8H ×7; CT32 ×39; one CT24 and one Z16 used as a
    texture. Texture function is always modulate.
  - TEX1: most world textures ask for mipmapping. Typical values: `mxl 4, mmin 4`
    (linear, nearest mip level), `lcm 0` (level computed per pixel), with K between about
    −6 and −2 (raw 3990-4064); `mxl` 3, 4 and 5 all occur. 2D draws use `lcm 1 mxl 0`.
  - All 240 MIPTBP writes in the sample are non-zero.
  - Uploads: BITBLTBUF destination format CT32 ×146 and CT16 ×92.
- **What the runtime does with those.** In the fork's GS, `gs.cpp` stores MIPTBP1 and
  MIPTBP2 and nothing reads them; of TEX1 only the magnification-filter bit is used
  (`raster.cpp`, `gpu/gpu.cpp`). So both renderers always sample level 0 at TBP0 and
  ignore MXL, MMIN, LCM and K. This is a fact about the code; whether it explains the
  garbage is not shown yet. On its own, sampling level 0 of a complete texture would look
  right, so it would only explain the picture if level 0 is not where the renderer
  expects it at draw time.

## End state (`plain`, frame 12900)
```
[ee] halted: frame limit reached
[thread] 1 status 1 prio 50 wait 0/0 pc 0x001F24E1 ra 0x001F2518
[sif] 0 packets wait for the EE (processed 1)
[gpu] 455 submits, dispatches: upload 33337 copy_read 0 copy_write 0 load_clut 15698 display 24 raster 186397 resolve 0
[ipu] commands: stalls 0 out 0 qwords, busy 0 decoding 0 cmd 0x00000030 in 0 bytes bit 0 out 0
[dma] starts: ch0=4975 ch1=449 ch2=898 ch3=0 ch4=0 ch5=0 ch6=0 ch7=0 ch8=19045 ch9=2322
[ee] stopped after 12900 frames, 63410197288 cycles: frame limit reached
```
The counters restart at the load, so they cover frames 12300-12900 only. In probe `b`
(from boot) threads 3, 4 and 5 are waiting (status 4) at the end, where they were ready in
run 010; the other threads are as in run 010. New IOP text at the race start:
```
[iop-tty] IOP:B3DIST_CmdHandler 2/2 got command ID of 32848
[iop-tty] IOP:B3Route:Finished Initialisation and setup of cores with channel mask of [ff]..
[iop-tty] IOP:B3DIST_CmdHandler 1/2 got command ID of 32800
[iop-tty] IOP:B3Route:started distortion..
```

## Where it stops
It does not stop. The race runs until the frame limit; with no steering the car crashes
and ends 6th of 6, standing against a wall at frame 14500.

## Verdict
Gap 008 candidate: the in-race 3D picture. Two visible faults, possibly one cause: world
textures hold the wrong content, and the player's car (and apparently the other cars) is
not drawn while its shadow is. The fault is upstream of both renderers and of the VU1
interpreter choice. The one concrete lead is that most world textures in the race ask
for mipmapping and the fork's GS ignores the mipmap registers. Not ruled out: texture
or CLUT upload addressing in the race's formats, and the VU0 microcode the race loads
through VIF0.

Repro for a fix: load `~/b3/out/run011/pre-race.state`, run to frame 12700 (24 seconds),
look at frames 12700 and 12875.
