# Gap 008: lifting the PATH3 mask released every queued packet at once, so race textures were overwritten before they were drawn

Date: 2026-10-12. Found in headless run 011 (`docs/re/boot-log-011.md`). Analysis, fix and verification: Opus 2, in a separate worktree of the fork (`~/b3/ps2-gs008`, branch `gap008` from `burnout3` at 5b782b3).

## Symptom

In a Single Event race the road, pavements, walls, fences, shop fronts and signs were drawn with the wrong texture content (coloured noise, or another surface's texture), while the sky, hills, trees, cones and the HUD were right. The geometry was in the right place. The picture was the same with the WebGPU renderer and the CPU rasteriser, and with every VU1 setting. No log line complained.

## Cause

The game does not keep its world textures in GS memory. Every frame it uploads them again in batches, all batches into the same GS memory area, and draws the geometry of one batch before the next batch replaces it. It orders the two streams with the PATH3 mask:

- GIF channel 2 (PATH3) is started once per frame, while PATH3 is masked, with about 219,000 qwords. They form 13 GS packets: 1,693 GIF tags of which 13 carry EOP. One packet is one batch, 16,000 to 21,000 qwords of BITBLTBUF/TRXPOS/TRXREG/TRXDIR settings and image data (the first packet is small, 265 qwords).
- The VIF1 stream then contains 14 groups of `FLUSHA, FLUSHA, MSKPATH3 off, MSKPATH3 on`, each followed by the geometry of one batch. The mask is lifted and set again by two adjacent VIF codes.

On hardware the mask acts on packets, not on bytes. Lifting it lets the next packet start at once; setting it again does not stop a packet that is already on its way, only the one after it; and the next `FLUSHA` waits until PATH3 is idle. So each off/on pair releases exactly one packet, and the geometry that follows finds that batch's textures in GS memory.

The fork's `gif.cpp` (the code of gaps 003 and 004) held all PATH3 data while the mask was set and forwarded the whole held buffer at the first unmask. All 13 batches were written in a row, each over the previous one, and the frame was then drawn with only the last batch's textures in memory: every surface whose batch was not the last sampled some other batch's texels.

Evidence, from a temporary trace of three race frames loaded from `~/b3/out/snaps/race01/s012750.state`:

```
[gif] mskpath3 0, held 219354 qwords        first unmask of the frame: everything went
[gif] mskpath3 1, held 0 qwords
[vif1] flusha
[vif1] flusha
[gif] mskpath3 0, held 0 qwords             13 more pairs with nothing left to release
[gif] mskpath3 1, held 0 qwords
...
[gif] held 219354 qwords: 1693 tags, 13 packets (EOP), 0 qwords after the last EOP
```

Hypotheses that were raised in boot log 011 and are not the cause: the unread MIPTBP registers and the ignored TEX1 mipmap fields (level 0 is at TBP0 and is what should be sampled; with the fix the textures are right without mipmapping); CLUT addressing for PSMT8/PSMT4 with a CT32 CLUT (the dumped textures of a fixed frame, 194 of them, are all clean); the texture cache of the WebGPU renderer (the CPU rasteriser showed the same picture).

## Fix (generic, in the fork: `runtime/src/gs/gif.cpp`, `runtime/src/gs/gif.hpp`, `runtime/src/vif.cpp`)

PATH3 masking now works on GS packets.

- `gif.cpp` follows the PATH3 stream as it is let through (`packet_span`, `follow`) so that it knows where the current packet ends: a packet ends with the data of the tag that has EOP set.
- When the mask is lifted (`MSKPATH3` off, or `GIF_MODE` M3R cleared, whichever makes PATH3 unmasked), exactly one held packet is forwarded (`start_path3`). The rest stays held.
- When the mask is set while a packet is partly through (its start was forwarded and its end has not arrived on the channel yet), the rest of that packet is still forwarded when it arrives; whole packets after it are held.
- The rest of the queue drains (`release_path3`) only when PATH3 has demonstrably stayed open: new channel-2 data arrives while unmasked, the EE reads `GIF_STAT` while unmasked, or VIF1 executes a code that does work while unmasked. `vif.cpp` calls `gif::path3_advance()` for every VIF1 code except NOP, MSKPATH3, FLUSHE, FLUSH and FLUSHA; the flushes only wait, so `MSKPATH3 off, FLUSHA, MSKPATH3 on` also releases one packet.
- New switch `PS2_TRACE_PATH3=1`: one line per mask change and per release.

The runtime has no notion of how long a GIF transfer takes, so "the mask came back before the second packet could start" cannot be measured; it is decided by what the VIF1 stream does between the two mask codes, as above. That is exact for the off/on pair this game uses and for a pair with flushes in between, and it lets PATH3 run freely as soon as VIF1 does anything else, which is what the old code did in all cases.

### State format

`kStateVersion` stays 4. The packet tracker is kept in slot 0 of the existing `g_scan` array, which no GIF path uses and which is already saved (`gif::state`), so old states load unchanged: a zeroed slot means "between packets". If the state format is bumped for another reason, the tracker can move to a field of its own.

### Thread ownership

Nothing changes: `set_path3_mask`, `transfer`, `read`, `write` and the new `path3_advance` all run on the EE side (`vif.cpp` calls the latter under the same `!on_vu_thread()` test as `set_path3_mask`).

## Verification

1. Build `ps2run` in the worktree: no warnings.
2. Race state: `PS2_GPU_FALLBACK=1 PS2_TRACE_PATH3=1 ps2run SLUS_210.50 --disc disc.iso --memcard <new> --load-state ~/b3/out/snaps/race01/s012750.state --frames 12760 --frames-dir <dir> --frame-every 5`. Expected: per frame, 13 lines `path3 unmasked: one packet of N qwords passes, M stay held` with M falling to 0, and frames whose road, walls and signs are right.
3. Menu state: load `~/b3/out/snaps/menu01/s003300.state`, run to 3400; frames must match the unfixed build.
4. Cold boot over the menu route (the schedule of `docs/re/harness-log-001.md` step 2, 3600 frames, no state): the boot path of gaps 003 and 004 (the `GIF_STAT` polls at `0x001F19C0` and `0x001F1A28`), the videos and the title must behave as before.

## Result (Opus 2, 2026-10-12)

All outputs are under `~/b3/out/gap008/`.

- Trace with the fix, first race frame after the load:
  ```
  [gif] mskpath3 0, 219354 qwords held
  [gif] path3 unmasked: one packet of 265 qwords passes, 219089 stay held
  [gif] mskpath3 1, 219089 qwords held
  [gif] mskpath3 0, 219089 qwords held
  [gif] path3 unmasked: one packet of 16862 qwords passes, 202227 stay held
  ...
  [gif] path3 unmasked: one packet of 19166 qwords passes, 16109 stay held
  ```
- Frames 12760 and 13660 (10 frames after the states at 12750 and 13650), before and after: `before-12750/frame_12760.png` against `after-12750/frame_12760.png`, `before-13650/frame_13660.png` against `after-13650/frame_13660.png`. Before: road and kerbs are noise, a billboard is a black box, fences are smeared colour. After: asphalt with lane markings, pedestrian crossing, shop fronts, fence, billboard and road signs are all correct. `PS2_GS=off` gives the same corrected picture (`after-soft-13650/`).
- All 194 texture setups drawn in one fixed race frame were dumped at draw time with the CPU rasteriser: every one is a clean image (no noise), which rules out CLUT and upload addressing.
- Menu state: frames 3325, 3350, 3375 and 3400 are byte-identical between the unfixed and the fixed build.
- Cold boot, 3600 frames, fixed build (`cold-menu/`): reaches the main menu as before; 1,212,031 primitives against 1,211,906 in `~/b3/out/snaps/menu01-control/run.log` (run-to-run noise is about 1,250); GIF tag counts (`path 2: packed 876578 reglist 284057 image 422400`, `path 3: packed 42022 reglist 1962 image 15934`), texture uploads (114,165), CLUT loads (1,193) and raster passes (108,014) are identical to the control. The control run kept no frames, so frames 300, 2100, 3300 and 3600 were compared between this run and the run with the gap 009 fix added (no pixel differs) and looked at (frame 3600 is the main menu).
- The cars were still missing after this fix: a separate fault, `docs/re/gap-009-vu-max-mini-flushed-denormals.md`.

## Status

Fixed in the worktree `~/b3/ps2-gs008` (branch `gap008`), uncommitted; the three files of this gap are also saved as `~/b3/out/gap008/gap008-path3.patch` so that it can be committed apart from gap 009.
