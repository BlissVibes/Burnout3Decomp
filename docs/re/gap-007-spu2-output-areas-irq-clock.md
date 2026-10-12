# Gap 007: the SPU2 mixer never writes its output areas, so RWA's IRQ clock never ticks

Date: 2026-10-11. Found in headless run 008 (`docs/re/boot-log-008.md`). Analysis: Fable (ultracode workflow), from the runtime, repro, IOP, EE and reference readers' reports, plus an out-of-tree confirmation run. Verification: workflow implementer.

## Symptom

From frame ~300 to 1800 every frame shows the LOADING screen with the bar about three quarters full. 0 IOP faults. The EE loader (thread 1, state word `0x0051BAA0` = 9) polls the `sound\generic.awd` stream every frame. The stream's EE table entry `0x0049FE60` keeps its busy bit (0x10 in word +0x1C) because the 4th cid-0 packet with code 0x32 never gets its cid-1 code-0x33 reply.

On the IOP (`repro-008b.log`), RWA's command worker, thread 18, takes the cid-5 packet before that one:
```
42683 [sif] command 0x00000000: 00000030 00000000 00000000 00000000 00000005 00000000 00102700 00005400 00004000 00000000 00000000 00000001 00000000 00000000
42691 [iop-hle] thevent:PollEventFlag(00000014, 00000002, 00000001, 001D8E50) ra 0x0003FB54 thread 18
42692 [iop-hle] thevent:ClearEventFlag(00000014, FFFFFFFD, 00000001, 001D8E50) ra 0x0003FB78 thread 18
42696 [iop-hle] thevent:WaitEventFlag(00000014, 00000002, 00000001, 001D8E50) ra 0x0003FCF4 thread 18
```
It never returns from that wait. Nothing in the run sets flag 0x14. There is no `[iop-dma] start ch 4/7`, no `[spu2] core N irq` line, and no `[iop] spu2 interrupt` line (0 in `repro-008b.log`). The `[iop] interrupt 9/36/40 -> ...` lines are RegisterIntrHandler registration prints (kernel.cpp `register_interrupt`), not deliveries.

## Cause

RWA.IRX (base 0x39000) runs its own stream engine on top of LIBSD (base 0x33600), and that engine is clocked by the SPU2 address IRQ (IOP line 9).

- **Flag 0x14.** RWA creates it at `0x3F964` with initial bits 3 and keeps the id at `0x469DC`. Bit `1<<n` means "transfer channel n idle", so bit 2 is channel 1.
- **cid 5.** This is "upload IOP 0x102700 to SPU2 RAM 0x5400, 0x4000 bytes, on channel 1". Thread 18 (body `0x41280`, started through RWA's thread trampoline `0x42B7C`, which is not itself a thread body) does the following:
  - installs channel 1's done-callback (`0x3FA7C`, callback `0x40FA8`)
  - claims the channel in `0x3FAB8` (CpuSuspendIntr, Poll, Clear(~2))
  - links request node `0x46A48` into RWA's priority queue with `0x3DEB8` (list `0x46248`, priority 3)
  - waits for bit 2 again at `0x3FCF4`

  The enqueue touches memory only, which is why no hardware or LIBSD line appears in the trace.
- **Who gives bit 2 back.** Only `0x3F8D0` does, with SetEventFlag at `0x3F914` (ra `0x3F91C`). It runs when the last piece of the request has been transferred: per-piece slot callback `0x3DDB0`, then RWA's notify helper `0x3CC44`. RWA imports no iSetEventFlag. (The other SetEventFlag sites, `0x3FF7C` and `0x3FFD8`, belong to an abort path that is not reached here.)
- **Who moves the request.** The queue is drained only by stream threads 19 and 20 (body `0x3D804`), which call `0x3DA94` after WaitSema on semaphores 0xF and 0x11. Those are signalled once per SPU2 IRQ:
  - LIBSD's line-9 handler `0x374E0` reads IRQINFO `0x1F9007C2` bits 2/3, clears ATTR bit 6 and calls the registered Spu2IntrHandler. That handler is B3ROUTE's replacement `0x49D70` (B3ROUTE base 0x49C00; its TTY line `B3Route:Replaced interrupt handler at 0x0003ee20 with data 0x00046684`), which chains core-0 IRQs to RWA's `0x3EE20`. So `0x3EE20` is RWA's handler, not B3ROUTE's.
  - `0x3EE20` calls iWakeupThread for thread 23.
  - Thread 23 (`0x3F120`/`0x3F274`) steps IRQA to the next table entry, re-arms ATTR bit 6, and signals the stream semaphores (`0x3D2CC`, ra `0x3D50C`).
- **The transfer.** Each piece goes out as LIBSD sceSdVoiceTrans in DMA mode on core 1, which starts IOP DMA channel 7. Its completion (line 40) runs, in order:
  - LIBSD `0x35DB8`
  - RWA's TransIntrHandler `0x3EBC4`
  - iWakeupThread for thread 22
  - `0x3CAC0`, then the slot callbacks, then `0x3F8D0`

  RWA's own init uploads use sceSdVoiceTrans in PIO mode (writes through the data port 0x1AC, no DMA), which is why run 008 shows SPU2 register writes but no SPU2 DMA.
- **The clock.** RWA arms core 0 with IRQA = byte 0x800 then 0xA00, that is halfword 0x400 then 0x500 (`repro-008c.log` lines 1233-1236: `[spu2] core 0 reg 0x19E = 0x0400`, `0x19A = 0xC040`). On a console the SPU2 mixer writes, every output sample, a set of write-back areas in SPU2 RAM at area + OutPos, where OutPos counts 0..0x1FF and wraps. Halfword addresses:

  | Area | Core 0 | Core 1 |
  |---|---|---|
  | Voice 1 (after ADSR) | 0x400 | 0xC00 |
  | Voice 3 | 0x600 | 0xE00 |
  | Dry L, Dry R | 0x1000, 0x1200 | 0x1800, 0x1A00 |
  | Wet L, Wet R | 0x1400, 0x1600 | 0x1C00, 0x1E00 |
  | Core output L, R | 0x800, 0xA00 | (core 0 only) |

  Every one of these writes is tested against both cores' IRQA. IRQA 0x400 is therefore hit when OutPos is 0 and 0x500 when OutPos is 0x100. Alternating the two gives one IRQ every 256 samples (5.33 ms, 196,608 IOP cycles at the runtime's 768 cycles per sample). That is RWA's engine clock. The game itself shows the ring is 0x200 long: with a 256-entry ring, 0x500 would never be hit. (PCSX2 and BurnoutDominatorReversed use 0x200; Play! wraps its core-0 write-back at 256 samples, which RWA's schedule rules out.)
- **The defect.** `runtime/src/iop/spu2.cpp` mixes voices and ADMA input to the host but never performs those write-backs. `check_irq()` is called only for voice ADPCM block fetches (`key_on`, `render_voice`), ADMA input slots (`render_input`), data-port writes (0x1AC) and DMA to TSA (`dma`). So IRQA = 0x400 is never touched, line 9 never fires, threads 19-23 never run, no SPU2 DMA ever starts, and flag 0x14 bit 2 is never returned. Thread 18 blocks with it. Because thread 18 handles commands in order, the queued 4th cid-0x32 is never served, and the EE loader waits forever (no EE timeout exists on that path).
- **Not the cause.** SIF delivery, the EE side, IOP DMA completion, interrupt delivery and LIBSD's register paths are all correct. They are simply never exercised. Burnout Dominator, which uses the same RWA engine, stalled on the same LOADING screen for the same reason (BurnoutDominatorReversed `docs/BOOT.md`, sections 6 and 8, which also give RWA's reply map with cid 5 answered by 0xD).

## Fix (generic, in the fork: `runtime/src/iop/spu2.cpp`, `runtime/src/system.cpp`)

### Part 1: write the mixer's output areas every sample and test IRQA against them (`spu2.cpp`)

1. After `constexpr unsigned kSpuLine = 9;`:
```cpp
// The mixer's write-back areas in SPU2 RAM (halfword addresses). Every output sample each core
// stores voice 1 and voice 3 (after ADSR) and its dry and wet L/R mixes, and core 0 stores its final
// L/R output, each at area + output position (a 0x200-halfword ring). Software uses these as a clock
// by pointing IRQA into one of them; the address IRQ fires when the mixer writes there.
constexpr std::uint32_t kOutputRing = 0x200;
constexpr std::uint32_t kVoice1Area[2] = {0x0400u, 0x0C00u};
constexpr std::uint32_t kVoice3Area[2] = {0x0600u, 0x0E00u};
constexpr std::uint32_t kDryArea[2][2] = {{0x1000u, 0x1200u}, {0x1800u, 0x1A00u}};
constexpr std::uint32_t kWetArea[2][2] = {{0x1400u, 0x1600u}, {0x1C00u, 0x1E00u}};
constexpr std::uint32_t kCore0OutArea[2] = {0x0800u, 0x0A00u};
static_assert(kOutputRing % kBatch == 0, "a batch must not straddle the output ring wrap");
```

2. After `bool g_tick_pending = false;`:
```cpp
std::uint32_t g_out_pos = 0;
```

3. `render_voice` gets a capture pointer. Its first line becomes:
```cpp
void render_voice(Voice &v, std::int32_t (*sum)[2], std::int16_t *capture) {
```
and directly after `const std::int32_t out = sample * v.env.level >> 15;`:
```cpp
        if (capture) capture[n] = static_cast<std::int16_t>(std::clamp(out, -32768, 32767));
```

4. After the one-line `master()` function:
```cpp
std::int16_t clamp16(std::int32_t value) { return static_cast<std::int16_t>(std::clamp(value, -32768, 32767)); }

void write_area(std::uint32_t area, const std::int16_t *values) {
    const std::uint32_t at = area + g_out_pos;
    check_irq(at, kBatch);
    for (unsigned n = 0; n < kBatch; ++n) g_ram[at + n] = static_cast<std::uint16_t>(values[n]);
}
```

5. The whole `tick()` becomes:
```cpp
void tick() {
    std::int16_t out[kBatch * 2];
    std::int32_t sum[2][kBatch][2] = {};
    std::int16_t raw[2][2][kBatch] = {};   // [core][voice 1, voice 3][sample], 0 while the voice is off
    std::int16_t dry[2][2][kBatch] = {};   // [core][left, right][sample], the voice mix before the input
    std::int16_t wet[2][kBatch] = {};      // reverb is not modelled, so the wet mixes are silence
    std::int16_t core0[2][kBatch] = {};    // core 0 output after its master volume
    static const bool count = std::getenv("PS2_SPU2_COUNT") != nullptr;
    static std::uint64_t ticks = 0, active = 0, phases[5] = {};
    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < 24; ++i) {
            Voice &v = g_cores[c].voices[i];
            if (!v.key_on && v.phase == kOff) continue;
            if (count) {
                ++active;
                ++phases[v.phase];
            }
            render_voice(v, sum[c], i == 1 ? raw[c][0] : i == 3 ? raw[c][1] : nullptr);
        }
        for (unsigned n = 0; n < kBatch; ++n) {
            dry[c][0][n] = clamp16(sum[c][n][0]);
            dry[c][1][n] = clamp16(sum[c][n][1]);
        }
        for (unsigned n = 0; n < kBatch; ++n) render_input(c, sum[c][n][0], sum[c][n][1]);
    }
    for (unsigned n = 0; n < kBatch; ++n) {
        std::int32_t mix[2] = {};
        for (int c = 0; c < 2; ++c) {
            const std::int32_t left = sum[c][n][0] * master(g_cores[c].mvol[0]) >> 15;
            const std::int32_t right = sum[c][n][1] * master(g_cores[c].mvol[1]) >> 15;
            if (c == 0) {
                core0[0][n] = clamp16(left);
                core0[1][n] = clamp16(right);
            }
            mix[0] += left;
            mix[1] += right;
        }
        out[n * 2] = static_cast<std::int16_t>(std::clamp(mix[0], -32768, 32767));
        out[n * 2 + 1] = static_cast<std::int16_t>(std::clamp(mix[1], -32768, 32767));
    }
    if (count && ++ticks % 15000 == 0)
        std::fprintf(stderr, "[spu2] %.1f voices per batch (attack %.1f decay %.1f sustain %.1f release %.1f)\n", static_cast<double>(active) / ticks,
                     static_cast<double>(phases[1]) / ticks, static_cast<double>(phases[2]) / ticks, static_cast<double>(phases[3]) / ticks, static_cast<double>(phases[4]) / ticks);
    if (g_record) g_audio.insert(g_audio.end(), out, out + kBatch * 2);
    if (g_sink) g_sink(out, kBatch);
    for (int c = 0; c < 2; ++c) {
        write_area(kVoice1Area[c], raw[c][0]);
        write_area(kVoice3Area[c], raw[c][1]);
        write_area(kDryArea[c][0], dry[c][0]);
        write_area(kDryArea[c][1], dry[c][1]);
        write_area(kWetArea[c][0], wet[0]);
        write_area(kWetArea[c][1], wet[1]);
        if (c == 0) {
            write_area(kCore0OutArea[0], core0[0]);
            write_area(kCore0OutArea[1], core0[1]);
        }
    }
    g_out_pos = (g_out_pos + kBatch) % kOutputRing;
    feed_input(0);
    feed_input(1);
    g_next_tick += kBatch * kCyclesPerSample;
    kernel::schedule(g_next_tick, kernel::Job{kernel::kJobSpuTick});
}
```

6. In `reset()`, after `g_tick_pending = false;`:
```cpp
    g_out_pos = 0;
```

7. In `state()`, after `s.pod(g_tick_pending);`:
```cpp
    s.pod(g_out_pos);
```

`tick()` is the only caller of `render_voice`.

### Part 2: mark the state format change (`system.cpp`)

```cpp
constexpr std::uint32_t kStateVersion = 4;
```
The previous value was 3. A version-3 state would already fail at load with `state section mismatch at iopio`, because `io::state` follows `spu2::state`. The bump turns that into the clear "state file from another version" error. Existing states such as `~/b3/out/ee007/s590.state` become unloadable either way.

### Behaviour notes

- **Audio.** The host output is numerically unchanged. The per-core terms are the same expressions as before, only named; the write-back reads values the mixer already computes.
- **Values written.** Voice 1/3 get the post-ADSR sample, and 0 while the voice is off, as on hardware, where the mixer keeps writing for an idle voice. Dry gets the clamped voice mix before the ADMA input is added. Wet gets 0, because reverb is not modelled. The core 0 output gets the core 0 mix after its master volume.
- **Timing granularity.** The write-back is done per 32-sample batch, so an IRQ is raised at batch time: up to 31 samples (0.65 ms) earlier than on hardware. Batches always start at a multiple of 32, so a batch never crosses the ring wrap (the static_assert enforces it). RWA tolerated this in run 009.
- **Latch.** The existing per-core latch (`k.irq`, cleared only by an ATTR write with bit 6 clear) still gives one IRQ per arm. LIBSD's handler clears ATTR bit 6 and RWA re-arms it after moving IRQA. This matches the IRQINFO behaviour documented by PCSX2 and ps2sdk FREESD.
- **Power-on IRQA.** IRQA's power-on value 0x800 lies in the core 0 output area. A game that enables the IRQ without setting IRQA now gets periodic IRQs, as on hardware (PCSX2 notes that F1 2005 relies on this).
- **Both cores.** Both cores' IRQA are tested against every write. Core 1 here keeps ATTR bit 6 clear (0xC001), so it raises nothing.
- **Cost.** 14 areas × 32 halfwords per batch: negligible.
- **References.** The area table, the 0x200 ring and "test both IRQAs on every write-back" are hardware behaviour learned from PCSX2 `pcsx2/SPU2/Mixer.cpp` and `spu2sys.cpp` (GPL-3.0+), BurnoutDominatorReversed `docs/BOOT.md` (GPL-3.0), Play! `Source/iop/Iop_SpuBase.cpp` (BSD-2-Clause) and ps2sdk FREESD `iop/sound/libsd` (AFL-2.0). The code above was written fresh; nothing was copied. All four are credited in `docs/research/PROVENANCE.md`.
- **Out of scope.** Seen but not needed for this gap, and left alone:
  - The ADMA input-area read IRQ at OutPos without queued data. The fork tests only its own ring slot while ADMA data is queued (PCSX2 tests the input area at OutPos on every sample).
  - One ADMA completion per transfer instead of per half-buffer.
  - STATX bit 0x400 (transfer busy) is never set.
  - SIF cid-0 handlers run synchronously from the EE side.
  - An IRQA placed in a reverb work area still never fires, because reverb is not modelled.
  - B3ROUTE's core-1 IRQA points (0x3800-0x3B00, its `Meminterrupt A:[3800] ... D:[3b00]` line) lie outside the write-back areas and depend on voice or DMA accesses once its distortion path runs; its `Warning, unknown address on B3dist core!` path was not seen in run 009.

## Verification

1. Rebuild with `ninja -C /home/mark/b3/ps2-web-recomp/build/native-burnout3 ps2run`.
2. A 600-frame repro with `PS2_GPU_FALLBACK=1 PS2_TRACE_SPU2=1`, `--trace-iop --trace-iop-hle --trace-syscalls`, presses as in run 008, misses/memcard/shots under `/home/mark/b3/out/`, under `timeout 600` with a 300 MB watcher. Expected:
   - periodic `[spu2] core 0 irq at 0x00400` / `0x00500` lines, 196,608 cycles apart, each followed by `[iop] spu2 interrupt -> 0x000374E0` and `thbase:iWakeupThread(00000017, ...) ra 0x0003EE68`;
   - after the first cid-5 packet, `[iop-dma] start ch 7` with `madr 0x00102700`, then `thevent:SetEventFlag(00000014, 00000002, ...) ra 0x0003F91C` and a cid-1 reply with code 0xD;
   - the 4th cid-0x32 answered with 0x33;
   - `[iop] 0 faults`, and thread 18 idle on its command semaphore in the IOP dump instead of at `0x3FCF4`.
3. Full run with `tools/port.sh --frames 1800`. Expected: the LOADING screen gives way to later screens; the scan shows only the existing `modload #18` finding.
4. `git -C /home/mark/b3/ps2-web-recomp status --short` shows ` M runtime/src/iop/spu2.cpp` and ` M runtime/src/system.cpp` only.
5. If the game stalls somewhere new, report the last `[sif]` and `[sys]` lines, the EE pc and the `[iop]` dump.

Before the fix was applied, an out-of-tree build of exactly this code (fork untouched, binary `/home/mark/b3/out/g7/ps2run`) gave the same sequence in `~/b3/out/g7/repro-g7a.log` (14.6 MB, 166,182 lines, exit 0) and reached the title screen in an 1800-frame run (`~/b3/out/g7/repro-g7full.log`, 206,507 primitives).

## Result (workflow implementer, 2026-10-11)

Applied exactly as written, with no change to the spec code; the git diff was checked against all seven Part 1 items and Part 2. `ninja` built `ps2run`; the web build in `port.sh` step 6 rebuilt `spu2.cpp.o` and `system.cpp.o` and linked `ps2web.html` (only the pre-existing em++ pthread + ALLOW_MEMORY_GROWTH warning). Headless run 009 (`docs/re/boot-log-009.md`):

- 600-frame repros, both exit 0 under `timeout 600` with the 300 MB watcher, `[iop] 0 faults`: `~/b3/out/repro-009.log` (18.4 MB, 212,372 lines, `PS2_TRACE_SPU2=1 --trace-iop-hle --trace-syscalls`) and `~/b3/out/repro-009b.log` (19.0 MB, 217,859 lines, the same plus `--trace-iop`, needed for the `[sif]`, `[iop-dma]` and interrupt-delivery lines). `games/burnout3/explore.ps2` did not exist when the repros ran, so each created a fresh memory card under `~/b3/out/`.
- The IRQ clock ticks: 1,862 `[spu2] core 0 irq` lines in 600 frames, alternating 0x400 and 0x500 exactly 196,608 cycles apart, the first two at `repro-009b.log` lines 1249 and 1568:
  ```
  [spu2] core 0 irq at 0x00400 (access 0x00400+32) time 3063658
  [spu2] core 0 irq at 0x00500 (access 0x00500+32) time 3260266
  ```
  Each is delivered (1,862 `[iop] spu2 interrupt -> 0x000374E0` lines) and followed by `thbase:iWakeupThread(00000017, 00046684, ...) ra 0x0003EE68` and thread 23's `SignalSema(0000000F/00000011, ...) ra 0x0003D50C`. No core 1 IRQ.
- The first cid-5 packet (`repro-009b.log` line 46823, the same `... 00000005 00000000 00102700 00005400 00004000 ...` as in run 008) is followed by thread 18's PollEventFlag / ClearEventFlag / `WaitEventFlag(00000014, 00000002, ...) ra 0x0003FCF4` as before. Two IRQs later the upload goes out in three channel-7 pieces of 0x1840, 0x1840 and 0xF80 bytes (0x4000 in all), one per IRQ, each completion waking thread 22 through `0x3EBF8`; then the flag is set and RWA replies:
  ```
  [iop-dma] start ch 7 madr 0x00102700 bcr 0x00610010 chcr 0x01000201
  [iop-hle] thevent:iSetEventFlag(0000000E, 00000001, 00000200, 00000000) ra 0x00035F2C thread 23
  [iop-hle] thbase:iWakeupThread(00000016, 00000001, 00000200, 00000000) ra 0x0003EBF8 thread 23
  [iop-dma] start ch 7 madr 0x00103F40 bcr 0x00610010 chcr 0x01000201
  [iop-dma] start ch 7 madr 0x00105780 bcr 0x003E0010 chcr 0x01000201
  [iop-hle] thevent:SetEventFlag(00000014, 00000002, 00000001, 00046A70) ra 0x0003F91C thread 22
  [iop-hle] sifcmd:sceSifSendCmd(00000001, 00046D20, 00000030, 00000000) ra 0x00041678 thread 22
  [sif] to EE: command 0x00000001: 00000030 00000000 00000001 00000000 0000000D 00000000 ...
  ```
  (The SetEventFlag is at line 47230.) The 0xD reply to cid 5 matches the Dominator reply map. Well before the first cid-5 packet, the eighth IRQ (`repro-009b.log` line 35469) already moves three channel-7 pieces from IOP `0x44C00` (inside RWA's image), and later IRQs keep moving pieces from that region; not analysed further.
- Totals over 600 frames: 662 channel-7 starts (channel 4: 0); 157 `SetEventFlag(00000014, ...)`; cid-0 commands by code 86 × 0x05, 109 × 0x32, 71 × 0x46, 373 × 0x14 plus single ones; 717 cid-1 replies, among them 157 × 0xD (as many as the SetEventFlag calls; which commands besides cid 5 they answer was not checked), 109 × 0x33 (every 0x32 answered, the 4th included), 71 × 0x47 and 373 × 0x14.
- EE side: thread 7's reply pump runs (`iSignalSema` on semaphore 19 717 times, one per reply; `WaitSema` 19 718 times); semaphore 26 is signalled 4 times (3 × iSignalSema, 1 × SignalSema). The IPU starts decoding (264,986 IPU command-3 lines in 600 frames): frame 400 of the repro is black, frame 600 is the EA GAMES logo.
- IOP dump at frame 600: `thread 18 status 4 prio 114 wait 3/11 pc 0x000412D8` (idle on its command semaphore 0xB), threads 19/20 `wait 3/15` and `3/17` at `0x3D850`, threads 21-23 `wait 1/0` at `0x3F698`: all idle waits.
- Full run (`tools/port.sh --frames 1800`, `~/b3/out/port-009.log`): port rc 0, no limit hit, 1800/1800 frames, `[iop] 0 faults`, 206,507 primitives (run 008: 145,509). The game finishes loading, shows the EA GAMES and Criterion Games logos and the intro FMV, and at frame 1800 shows the Burnout 3 Takedown title screen with "Press START button to continue". Scan: GAPS for the pre-existing `modload #18` finding only.
- No new stall within 1800 frames. The port.sh presses (300 start, 600/900/1200/1500 cross) all land during loading, the logos and the FMV, so none reaches a menu; the next run needs a later START press, for example `--frames 3000 --press 1900:start`.

## Status

- Applied in the fork on branch `burnout3` (on top of `bbb10d4`) by the workflow implementer and verified by run 009. Commit `3e42c71` (`git status --short`: ` M runtime/src/iop/spu2.cpp`, ` M runtime/src/system.cpp`; 57 insertions, 6 deletions). Proposed commit message `spu2: write the mixer output areas every sample and test IRQA against them (RWA's IRQ clock)`.
- State version 3 → 4: every existing save state becomes unloadable.
- References used (knowledge only, no code copied): PCSX2, BurnoutDominatorReversed, Play!, ps2sdk; see `docs/research/PROVENANCE.md`.
