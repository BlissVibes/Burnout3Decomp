# Gap 004: PATH3 is not stalled while masked, so the GIF FIFO never fills

Date: 2026-10-10. Found in headless run 005 (`docs/re/boot-log-005.md`). Analysis: Fable. Verification: Opus 1.

## Symptom

After gap 003 the main thread passes the mask wait at `0x001F19C0` and stops a few instructions later at `0x001F1A28`, same routine, same caller. Still one primitive, still black frames, worker threads never scheduled.

## Cause

Between the two waits the routine programs and starts a GIF DMA transfer (channel 2; the start count rose from 2 to 3) while PATH3 is still masked. It then reads `GIF_STAT` and, if the FIFO count field (`FQC`, bits 24–28) is already non-zero, skips ahead; otherwise it loops at `0x001F1A28` until `FQC` becomes non-zero. After that it starts a second channel with `CHCR = 0x145` and enters a loop that watches `D1_CHCR`, `D2_CHCR`, `VIF1_STAT` and the `FQC`/mask bits of `GIF_STAT` together.

This is the RenderWare PS2 driver priming the GIF FIFO: with PATH3 masked, the channel-2 transfer pushes its first qwords into the 16-qword FIFO and stalls there; the game confirms the prime by seeing `FQC > 0`, then runs the VIF1 chain whose embedded `MSKPATH3` commands (the second code-06 in the dump is the unmask) let PATH3 drain in slices between PATH1 packets.

The runtime delivers channel-2 data to the GIF synchronously and ignores the mask (noted as "not done on purpose" in gap 003), so the FIFO count is always zero and the poll never ends.

## Fix (generic, in the fork: `runtime/src/gs/gif.cpp`, `runtime/src/gs/gif.hpp`, `runtime/src/vif.cpp`)

Hold PATH3 data while PATH3 is masked, report the held amount as the FIFO count (capped at 16) and the `P3Q` bit, and forward the held data in order when the mask clears.

### Thread ownership

All of this state lives on the EE thread. PATH3 transfers arrive from the DMAC on the EE thread. The VIF1 stream is executed twice when the VU worker is active: a shadow pass on the EE thread and the real pass on the VU thread (`vif.cpp`: `shadow()`, `on_vu_thread()`). Only the EE pass may touch the mask, so the `MSKPATH3` hook from gap 003 must be guarded with `!on_vu_thread()`.

### `gif.hpp`

No new declarations beyond gap 003's `set_path3_mask(bool)`.

### `gif.cpp`

State, next to `g_mask_vif1` and `g_mode`:
```cpp
std::vector<std::uint8_t> g_path3_held;   // PATH3 data received while PATH3 was masked, in arrival order
constexpr std::uint32_t kGifFifoQwords = 16;

bool path3_masked() { return g_mask_vif1 || (g_mode & 1u); }
```

Rename the current `transfer()` body to `forward()` and gate it:
```cpp
void forward(int path, const std::uint8_t *data, std::uint32_t qwc) {
    if (!gs::queue::active()) {
        render(path, data, qwc);
        return;
    }
    if (gs::queue::role() != gs::queue::Role::Vu) scan(g_scan[path], data, qwc);
    gs::queue::post(gs::queue::kGif, static_cast<std::uint32_t>(path), qwc, data, qwc * 16);
}

void release_path3() {
    if (g_path3_held.empty()) return;
    std::vector<std::uint8_t> held = std::move(g_path3_held);
    g_path3_held.clear();
    forward(3, held.data(), static_cast<std::uint32_t>(held.size() / 16));
}

void transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {
    if (path == 3 && path3_masked()) {
        g_path3_held.insert(g_path3_held.end(), data, data + static_cast<std::size_t>(qwc) * 16);
        return;
    }
    forward(path, data, qwc);
}

void set_path3_mask(bool masked) {
    g_mask_vif1 = masked;
    if (!path3_masked()) release_path3();
}

std::uint32_t read(std::uint32_t a) {
    if ((a & 0xF0u) != 0x20u) return 0;
    const std::uint32_t held = static_cast<std::uint32_t>(g_path3_held.size() / 16);
    const std::uint32_t fqc = std::min(held, kGifFifoQwords);
    return g_stat | (g_mode & 5u) | (g_mask_vif1 ? 2u : 0u) | (held ? (1u << 6) : 0u) | (fqc << 24);
}

void write(std::uint32_t a, std::uint32_t v) {
    if ((a & 0xF0u) == 0x00u && (v & 1u)) reset();
    if ((a & 0xF0u) == 0x10u) {
        g_mode = v & 5u;
        if (!path3_masked()) release_path3();
    }
}
```
`reset()`: also `g_path3_held.clear();` (with the gap 003 resets). `state()`: add `s.vec(g_path3_held);` after the gap 003 pods (the state stream already has `vec` for vectors; if `vec` only accepts trivially-copyable element vectors, `std::vector<std::uint8_t>` qualifies). Include `<algorithm>` for `std::min` and `<vector>`.

### `vif.cpp`

Change gap 003's line to:
```cpp
    case 0x06: if (unit == 1 && !on_vu_thread()) gif::set_path3_mask(imm & 0x8000u); break;
```

### Behaviour notes

- Channel 2 still completes instantly from the DMAC's point of view (`D2_CHCR.STR` clears). On hardware it would stay busy until the mask lifts. Games cannot depend on it staying busy without hanging on hardware when the mask lifts early, so the simplification is safe; the gap 003 note's deferral is now implemented for the FIFO half only.
- Order on the render ring: held PATH3 data is posted at the unmask point of the EE shadow pass, which runs ahead of the VU thread's PATH1 output. The previous runtime posted PATH3 even earlier (at DMA time), so this is strictly closer to hardware order.
- The 16-qword cap applies to the reported count only; all held data is kept and forwarded.

## Verification

1. Rebuild `ps2run`, then the Task 6 repro or `tools/port.sh --frames 1800`.
2. Expected: thread 1 leaves `0x001F1A28`; `[gif] path 3 tags` and `[gs] N primitives drawn` rise well beyond 1; threads 3–6 run (their pcs move off their entry addresses); first non-black frame appears in `shots/`.
3. If the main thread stops in the loop after `0x001F1AA0` (it polls `D1_CHCR`, `D2_CHCR`, `VIF1_STAT` and `GIF_STAT` together), report which register it last read and the `[vif1]`/`[dma]` summary lines; that would point at VIF1 stall state rather than the GIF.

## Result (Opus 1, 2026-10-11)

Applied exactly as written (`forward()` and `release_path3()` sit in an anonymous namespace just above `transfer()`; the `!on_vu_thread()` guard is in). The native runner and the web build compiled without errors. Headless run 006 (`docs/re/boot-log-006.md`):

- Thread 1 leaves `0x001F1A28` and does not stop in the loop after `0x001F1AA0`.
- `[gs] 143 primitives drawn`; `[gif] path 3 tags: packed 14 reglist 1 image 4`, path 2 `packed 17 reglist 16`; `[vif1]` shows 9 code-06 commands; DMA starts ch1=3, ch2=5, ch8=8.
- Threads 3–6 ran and now wait inside the game.
- First non-black frame of the port: the LOADING screen with an empty progress bar, in all six frames.
- The run then stalls somewhere new, at `0x0010D508`, after the IOP network modules load: gap 005.

## Status

- Applied in the fork on branch `burnout3` by Opus 1 and verified by run 006; commit message `gif: hold PATH3 while masked and report the FIFO count`.
