# Gap 002: the vertical-blank poll never sees the interrupt bit

Date: 2026-10-10. Found in headless runs 002 and 003 (`docs/re/boot-log-002.md`, `boot-log-003.md`). Analysis: Fable. Verification: Opus 1.

## Symptom

With the constructor table seeded, the game runs to the frame limit without a halt but draws nothing: zero GS primitives, zero DMA starts, zero GPU submits in 1800 frames. The main thread (thread 1) spins at `0x0010C2F0` with `ra 0x0010C2E4`, making no system calls after the IOP set-up sequence; thread 2 waits on semaphore 3 from start-up.

## Cause

The routine at `0x0010C2B0` is Sony's `sceGsSyncV(0)`, the standard libgraph vertical-blank wait. In prose: it disables interrupts (the helper at `0x00114E60` is a plain `di` with the EE's status-register re-read loop, returning whether interrupts were enabled), writes 4 to the interrupt controller status register at `0x1000F000` to clear the vertical-blank-start bit, restores interrupts (`0x00114EB8` is a plain `ei`), then reads the status register in a tight loop at `0x0010C2F0` until bit 2 is set again, and finally clears it once more.

The game also registers an interrupt handler for line 2 (`AddIntcHandler(2, 0x002370A0)` then `EnableIntc(2)`). That handler signals up to four semaphores whose ids sit in a table at `0x01D80700` and never touches the status register.

On hardware both work together because the CPU takes the exception a few cycles after the status bit rises. A tight poll reads the register inside that window and sees the bit before the kernel's dispatcher acknowledges it. The runtime has no such window:

- `hw::intc_raise()` sets the status bit and `kernel::must_unwind()` becomes true at once.
- Pending interrupts are checked in `preempt()`, which the generated code calls at backward branches when the cycle budget is exhausted. The vertical-blank event fires inside that same `preempt()` call (`run_due_events()`), so the unwind, `kernel::run_intc()` and its `hw::intc_ack()` all happen before the loop's next load. The poll resumes and reads 0, forever.

Play!'s high-level kernel also acknowledges before dispatching, but its CPU core takes the exception at the next block boundary, which gives the same kind of window hardware does. Another emulator project documented the identical hang and fixed it by delivering interrupt-controller interrupts a few instructions late.

## Fix (generic, in the fork: `runtime/src/hw.cpp`, `runtime/src/hw.hpp`, `runtime/src/system.cpp`)

Model the delivery latency. A rising status bit becomes *deliverable* a fixed number of EE cycles later. The request is latched when the line is enabled at raise time, so a game that clears the status bit inside the window (as `sceGsSyncV` does on its way out) still gets its handler run once, which is also what happens on hardware in practice because the exception is taken before the clearing store executes.

### hw.hpp

Add two event ids before `kEventCount` so saved states can rebuild the arm events:

```cpp
enum Event {
    kEventVblank,
    kEventVblankEnd,
    kEventTimer0,
    kEventTimer1,
    kEventTimer2,
    kEventTimer3,
    kEventDma0,
    kEventIop = kEventDma0 + 10,
    kEventIntcArm0,                        // one per INTC line, 16 lines
    kEventIntcArmEnd = kEventIntcArm0 + 15,
    kEventCount
};
```

### hw.cpp

Replace the INTC state and the four INTC functions:

```cpp
// The CPU takes an INTC interrupt a little after the status bit rises. A tight poll of INTC_STAT
// (libgraph's sceGsSyncV) reads the bit inside that window before the kernel acknowledges it.
constexpr std::uint64_t kIntcLatency = 32;

std::uint32_t g_intc_stat = 0, g_intc_mask = 0;
std::uint32_t g_intc_requested = 0;    // lines that rose while enabled and will be delivered
std::uint32_t g_intc_deliverable = 0;  // requested lines whose latency has passed

void intc_arm(unsigned cause) {
    const std::uint32_t bit = 1u << cause;
    if (!(g_intc_requested & bit)) return;
    g_intc_deliverable |= bit;
    if (g_intc_mask & bit) kernel::wake_idle();
}

void intc_request(unsigned cause) {
    const std::uint32_t bit = 1u << cause;
    if (g_intc_requested & bit) return;
    g_intc_requested |= bit;
    schedule(kEventIntcArm0 + static_cast<int>(cause), now() + kIntcLatency, [cause] { intc_arm(cause); });
}

std::uint32_t intc_pending() { return g_intc_deliverable & g_intc_mask; }

void intc_raise(unsigned cause) {
    const std::uint32_t bit = 1u << cause;
    g_intc_stat |= bit;
    if (g_intc_mask & bit) intc_request(cause);
}

void intc_ack(unsigned cause) {
    const std::uint32_t bit = 1u << cause;
    g_intc_stat &= ~bit;
    g_intc_requested &= ~bit;
    g_intc_deliverable &= ~bit;
}

bool intc_set_mask(unsigned cause, bool enabled) {
    const std::uint32_t bit = 1u << cause;
    const bool was = g_intc_mask & bit;
    if (enabled) {
        g_intc_mask |= bit;
        if (g_intc_stat & bit) intc_request(cause);   // level-sensitive: enabling a line that is already up fires it
    } else {
        g_intc_mask &= ~bit;
    }
    return was != enabled;
}
```

`write32(kIntcStat)` stays `g_intc_stat &= ~v;` (a guest clear does not cancel a latched request). `write32(kIntcMask)` stays as is but must honour the level rule the same way: after `g_intc_mask ^= v;` add `for (unsigned cause = 0; cause < 16; ++cause) if ((g_intc_mask & g_intc_stat & (1u << cause))) intc_request(cause);`.

`reset()`: also zero `g_intc_requested` and `g_intc_deliverable`.

`event_action()`: add `if (id >= kEventIntcArm0 && id <= kEventIntcArmEnd) return [cause = static_cast<unsigned>(id - kEventIntcArm0)] { intc_arm(cause); };`

`state()`: add `s.pod(g_intc_requested); s.pod(g_intc_deliverable);` after `g_intc_mask`.

### system.cpp

`kStateVersion` 2 → 3 (the event id numbering and the INTC state changed; old state files are refused with the existing message).

`intc_arm` and `intc_request` are file-local; `kernel.cpp` keeps calling `intc_pending`, `intc_ack`, `intc_set_mask` unchanged.

### Why 32 cycles

The poll body is six instructions, so the loop observes the bit on its next pass once the budget slice ends at the arm event. The exit path (`di`, the clearing store, `ei`, return) is well under 32 budget units, so the request is still latched when the arm fires and the handler runs at the next backward branch. It is far below anything a game can time: a vertical-blank period is about 4.9 million EE cycles.

### Risk

This changes when every INTC handler runs, for every game, by about 32 cycles. GT4 and God of War II cannot be regression-tested on this machine (no discs). The change is small and hardware-shaped; record it in `external/FORK.md` as the first behaviour change to send upstream with a note asking snuri00 to run it against those two titles.

## Verification

1. Rebuild and re-run the port: `tools/port.sh --frames 1800`.
2. Expected: `shots/` is no longer empty, `[gs] N primitives drawn` with N > 0, DMA channel starts non-zero, and the scan shows the run reached the frame limit. Thread 1 no longer sits at `0x0010C2F0` in the end-of-run dump.
3. If the main thread still spins there, run the Task 6 repro with `--trace-threads` and look for the handler: after a vertical blank, thread 2 should wake from semaphore 3 at some point if the game has registered its id in the handler's table. Report the dump either way.
4. Second-order check: `[frame] N prims` lines with `--trace-threads` should show prims rising within the first 120 frames (EA and Criterion logos).

## Result (Opus 1, 2026-10-10)

Applied exactly as written above; the native runner and the web build compiled without errors. Headless run 004 (`docs/re/boot-log-004.md`), `tools/port.sh --frames 1800`:

- Thread 1 no longer sits at `0x0010C2F0`. The game goes on to `SetGsCrt interlace 1 mode 0x2 field 0`, creates threads 3–6, and the end-of-run dump shows `[gs] 1 primitives drawn`, `[dma] starts: ch1=1 ch2=2 ch8=2`, `[gpu] 7 submits`, IPU commands 0, 5, 6 and 9.
- Verification step 2 passes as worded: `shots/` holds six frames, primitives > 0, DMA starts non-zero, frame limit reached.
- Step 4 does not: `[frame] N prims` stays at 1 from frame 25 to the end, and all six frames are solid black. The game stalls again in a different place, a loop without syscalls at `0x001F19C0` (caller `0x001F2604`), entered right after an unhandled write of `0x10000` to `0x1000F590` and `ChangeThreadPriority(1, 10)`. That is the next gap, not a failure of this fix.
- GT4 and God of War II were not tested (no discs here), as the Risk section says.

## Status

- Applied in the fork on branch `burnout3` by Opus 1 and verified by run 004; commit message `hw: deliver INTC interrupts after a short latency so status polls can see the bit`.
