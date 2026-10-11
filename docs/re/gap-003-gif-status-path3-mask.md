# Gap 003: GIF status never reports the PATH3 mask

Date: 2026-10-10. Found in headless run 004 (`docs/re/boot-log-004.md`). Analysis: Fable. Verification: Opus 1.

## Symptom

After gap 002, the game sets the display mode, draws one primitive, creates its four worker threads (priorities 54, 54, 54, 33) and lowers the main thread to priority 10. Then thread 1 spins at `0x001F19C0` with no further system calls, so the worker threads never run. All frames are black.

## Cause

The loop at `0x001F19C0` reads the GIF status register (`GIF_STAT`, `0x10003020`) and waits for bit 1 to become 1. Bit 1 is `M3P`: "PATH3 is masked by VIF1". Just before, the code sent VIF1 a `MSKPATH3` command (VIF code `0x06`, mask bit 15 set); the end-of-run dump shows one VIF1 code 06 executed. After the wait the routine also checks the FIFO count field (bits 24–28) of the same register for zero. This is the RenderWare PS2 driver's "block PATH3 while PATH1 renders" synchronisation.

In the runtime (`runtime/src/vif.cpp`, `execute()`), `case 0x06` is an empty statement: the mask is discarded. `runtime/src/gs/gif.cpp` keeps `g_stat` as a plain value that nothing sets, and `read()` returns it unchanged, so `M3P` can never read as 1. `GIF_MODE` writes (`0x10003010`, bits `M3R` and `IMT`) are also dropped, and those are mirrored into `GIF_STAT` bits 0 and 2 on hardware.

## Fix (generic, in the fork)

Track the two mask sources and reflect them in `GIF_STAT`.

### `runtime/src/gs/gif.hpp`

Add:
```cpp
void set_path3_mask(bool masked);   // VIF1 MSKPATH3
```

### `runtime/src/gs/gif.cpp`

Next to `g_stat`:
```cpp
bool g_mask_vif1 = false;       // PATH3 masked by VIF1 MSKPATH3 (GIF_STAT.M3P)
std::uint32_t g_mode = 0;       // GIF_MODE: bit 0 M3R, bit 2 IMT (mirrored into GIF_STAT bits 0 and 2)
```
In `reset()`: also `g_mask_vif1 = false; g_mode = 0;`.

Replace `read()` and `write()`:
```cpp
void set_path3_mask(bool masked) { g_mask_vif1 = masked; }

std::uint32_t read(std::uint32_t a) {
    if ((a & 0xF0u) == 0x20u) return g_stat | (g_mode & 5u) | (g_mask_vif1 ? 2u : 0u);
    return 0;
}

void write(std::uint32_t a, std::uint32_t v) {
    if ((a & 0xF0u) == 0x00u && (v & 1u)) reset();
    if ((a & 0xF0u) == 0x10u) g_mode = v & 5u;
}
```
In `state()`: add `s.pod(g_mask_vif1); s.pod(g_mode);` after `g_stat` (state version 3 was introduced this session; no saved states predate it, so no bump).

### `runtime/src/vif.cpp`

```cpp
    case 0x06: if (unit == 1) gif::set_path3_mask(imm & 0x8000u); break;
```
(`MSKPATH3` exists only on VIF1; bit 15 of the immediate is the mask flag.)

### Not done on purpose

Hardware also stalls PATH3 DMA transfers while the mask is set and resumes them when it clears. The runtime executes GIF DMA synchronously in program order, so ignoring the stall changes nothing visible unless a game depends on PATH1 and PATH3 interleaving. Leave it until a symptom points at it; note it in the fork's issues list.

## Verification

1. `ninja -C build/native-burnout3 ps2run` then the Task 6 repro, or the full `tools/port.sh --frames 1800`.
2. Expected: thread 1 leaves `0x001F19C0`; the dump shows threads 3–6 having run (status or pc changed from their entry addresses); `[gs] N primitives drawn` with N well above 1; `shots/` no longer all black.
3. If the main thread stops elsewhere, report the new pc, the `[io]` lines, and the last 40 lines of the traced repro.

## Result (Opus 1, 2026-10-10)

Applied exactly as written; the native runner and the web build compiled without errors. Headless run 005 (`docs/re/boot-log-005.md`):

- Thread 1 leaves `0x001F19C0`, as expected.
- The rest of Verification step 2 does not hold. Thread 1 stops again at `0x001F1A28`, same caller `0x001F2604` and same call stack, so it is a later wait in the same routine. Threads 3–6 still have not run. `[gs] 1 primitives drawn`. All six frames are solid black.
- What the routine did between the two waits: one more DMA start on channel 2 (3 in total), GIF PATH3 tags `packed 7 reglist 0 image 2` (was `packed 2`), GS register writes `50`–`53` twice each and `7F` once, one GPU texture upload. VIF1 code counts are unchanged (`06:2`).
- Step 3: the new pc is `0x001F1A28`; the only `[io]` line is still `[io] write 0x1000F590 = 0x10000 (pc 0x00109C08)`; the traced repro (`~/b3/out/repro-005.log`) is line-for-line the same as run 004's except for those summary lines, ending with the four thread creations, that `[io]` line and `ChangeThreadPriority(1, 10)`.

## Status

- Applied in the fork on branch `burnout3` by Opus 1; commit message `gif: reflect VIF1 MSKPATH3 and GIF_MODE in GIF_STAT`. The first wait is fixed; the second wait at `0x001F1A28` is the next gap.
