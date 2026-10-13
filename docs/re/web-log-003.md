# Browser log 003: two runtime overheads removed from the emulation thread — 2026-10-13

Fork: `burnout3` at a854891 (before: f2f31d2). Page in `~/b3/out/web`, `ps2web.wasm` 24,883,793 bytes (before: 24,865,366, kept in `~/b3/out/web-before`). The two items are the ones `docs/re/web-log-002.md` listed last: the dispatcher at 8% and GIF packet copying at 12% of the emulation thread. Data under `~/b3/out/prof01/` (`idle/`, `web/ab/`).

**Both were runtime overhead and both are gone. In Chrome the emulation thread's idle time over the race goes from about 10% to about 30%: the thread does 23% less work a frame (15.0 ms to 11.6 ms of a 16.7 ms frame). The game's course is unchanged: same cycle count, same primitive count, same audio byte for byte.**

## 1. The game's wait loop (the dispatcher's 8%)

The dispatcher was not slow; it was being called 160,000 times a frame by one loop.

At `0x001F24E0` the game waits for its own DMA queue to catch up: it spins until two queue positions are equal and a busy byte is clear (an interrupt handler moves them). On every turn it calls the function that a global pointer names (the pointer is at `gp - 28312`), from the call that returns to `0x001F2518`, with `a0` = 1 while it still waits and 0 on its last turn. In a race that pointer holds `0x00228040`, a function that stores `a1` in the global at `0x006662E0` if it is lower than what is there; `a1` does not change inside the loop, so after the first turn the call changes nothing.

A turn costs 21 or 24 cycles. Over 1,800 frames of the race (12300 to 14100) the loop turned 294 million times, 163,000 turns a frame: 70 to 80 percent of the EE's emulated time is spent waiting here. On the console that is simply the EE idling; in the runtime every turn is a call through a pointer, so it goes through `ps2::dispatch`, and the loop body and the callback are two more pieces of generated code.

### The fix: an idle call

`ps2::add_idle_call(function, return address, waiting register)` (fork, `runtime/src/dispatch.cpp`) tells the dispatcher about such a call. For a registered call whose waiting register is non-zero it compares the cycle budget with the budget at the previous such call. Once two turns in a row have cost the same number of cycles, it takes whole turns off the time slice at once and leaves the last one or two to run. The slice therefore ends on exactly the cycle it would have ended on with the loop spinning, the due events run at the same time, and the loop resumes in the same phase. A turn of another length, a new slice, a call from elsewhere or a zero in the waiting register starts the measuring again. Nothing is skipped inside a harness call. `PS2_NO_IDLE=1` turns it off.

The registration is a promise that the function does the same thing on every turn of one wait. It is made per game, in `src/hooks/hooks_table.cpp` (`register_hooks()`), which the page now loads as the native program does: `host/web.cpp` calls `register_hooks()`, and `tools/build-web.sh` passes `PS2_HOOKS`. The decompiled functions in that table stay registered and disabled, as before.

A first form that simply ended the slice on a matching call gave the same totals but a different run (frames and audio differed), because the loop resumed in a different phase. It was dropped for the exact form.

### Is the run the same?

Native, the pre-race state (`~/b3/out/run011/pre-race.state`, frame 12300) to frame 14100, accelerate held, with `PS2_NO_IDLE=1` and without:

| | Cycles at frame 14100 | Primitives | Audio (1,800 frames) |
|---|---|---|---|
| loop spinning | 69,308,449,691 | 72,746,114 | |
| idle call | 69,308,449,691 | 72,746,114 | identical, byte for byte |

293,972,893 turns were skipped.

The frames dumped every 50 frames differed between the two runs in 32 of 36 files. That is not the change: two runs of the same build with the same options differ in the same way (6 of 8 dumped frames, identical cycles and primitives). The frame dump is taken from the render thread and is not repeatable between runs, so it cannot be used as an equality check. Cycles, primitives and audio are.

## 2. The PATH3 hold buffer (most of the GIF 12%)

The game sends its texture uploads on PATH3 with PATH3 masked, and lifts the mask to let one packet through at a time. In a race that is 608 transfers and 3.3 MB a frame, all of it held by the runtime first (`runtime/src/gs/gif.cpp`).

The held bytes were in a byte vector, and each release erased the released packet from the front, which moves everything behind it. When a release emptied the vector its storage was given away, so the next frame grew it again from nothing. Counted natively per frame: 10 reallocations and 20.6 MB of bytes moved, to pass on 3.3 MB.

Now the buffer has a read position. A release moves the position, not the data; when nothing is left the buffer is cleared and keeps its storage; the passed part is dropped only when the storage would otherwise grow, and before a state is written (a state holds only what waits, as before, so state files are unchanged). Per frame: 0 reallocations, 0 bytes moved; the time in the buffer's insert and release went from 1.21 ms to 0.35 ms natively. The run is identical (65,867,802,165 cycles and 47,145,247 primitives at frame 13400 before and after).

## Measured in Chrome

`speed.mjs` (the copy in `~/b3/out/prof01/web/`, which also records the size of the WebAssembly memory and the processor time of all of the test browser's processes), Chrome muted, a separate profile, faked pad, nothing else running on the machine. Three pages, two runs each, alternating: the old page; the new page with `?PS2_NO_IDLE=1` (the hold buffer fix alone); the new page. Every run: 60.0 fps over the race, no JavaScript error.

Emulation thread idle, both runs of each:

| Frames | Before | Hold buffer fix only | Both fixes |
|---|---|---|---|
| race, 12650 to 14100 | 9.1%, 10.5% | 16.1%, 15.7% | 30.5%, 30.4% |
| 12650 to 13050 | 3.7%, 5.0% | 12.8%, 11.3% | 27.0%, 26.2% |
| 13050 to 13400 | 3.0%, 4.2% | 10.8%, 10.6% | 24.8%, 27.3% |
| 13400 to 14100 | 16.0%, 16.9% | 20.8%, 20.9% | 35.4%, 34.1% |
| lowest one-second sample in the race | 0%, 0% | 8%, 7% | 19%, 23% |
| menus and the training video, 2000 to 11900 | 56.5%, 56.4% | 56.0%, 55.9% | 72.4%, 72.5% |

Processor time, over the race:

| | Before | Hold buffer fix only | Both fixes |
|---|---|---|---|
| Emulation thread busy, per frame | 15.0 ms (90.2%) | 14.0 ms (84.1%) | 11.6 ms (69.5%) |
| All of Chrome's processes, in cores | 2.54, 2.54 | 2.47, 2.47 | 2.34, 2.34 |

So the hold buffer fix saves 1.0 ms a frame (6.8% of the thread's work) and the idle call 2.4 ms (16.1%); together 3.4 ms, 23%. Chrome as a whole uses 0.20 of a core less, which is the same 3.4 ms a frame: the saving is all on the emulation thread. The VU thread (idle 19 to 22% in every run) and the main thread (idle 53 to 58%) are as they were.

Chrome's profiler on the new page (`prof.mjs`, frames 12650 to 13400), emulation thread, against the profile of `web-log-002.md`:

| | Before | After |
|---|---|---|
| `ps2::dispatch` | 8.1% | below 0.4% (not in the listing) |
| generated EE code | 19.6% | 13.7% |
| the two units with the loop and its callback (`unit_003C`, `unit_004A`) | 3.0% and 3.2% | 0.4% and below 0.4% |
| `ps2::gif` | 6.3% | 2.1% |
| byte-vector insert | 4.1% | 0.9% |
| waiting (idle) | none recorded | 20.4% |
| `ps2::vu::run` | 37.0% | 38.0% |

`vu::run` is now by far the largest item on the thread: the same 6 ms a frame, more than half of what the thread still does.

### Memory

- Size: no change. The WebAssembly memory is 307.2 MB at the start and at the end of the race on all three pages. The hold buffer was never large (it holds 3.5 MB at its fullest and now keeps 3.7 MB of storage); what it wasted was copying, not space.
- Traffic: 20.6 MB of bytes moved a frame and 10 reallocations a frame are gone (native count). What is left of the hold buffer's work is one copy in and one copy out to the render queue, 3.3 MB each. Roughly 30 MB a frame of memory writes before, under 7 MB now.
- The idle call does nothing for memory.

Audio played from an empty queue during the race: 0 to 672 samples (0 to 14 ms) in a run, on the old page and on the new one alike; no pattern between the pages.

## How others handle a wait like this

- PCSX2 has it as a speed hack that is on by default ("wait loop detection", with "INTC spin detection" beside it): when the EE sits in a loop that only tests something an event will change, the emulator jumps straight to the next event. Theirs is found by looking at the loop's code; this loop calls through a pointer on every turn, which such a check cannot see through, so here the call is named by hand and the runtime proves the turns are alike by measuring them. Behaviour only, nothing copied.
- reB3 (the Xbox reimplementation) has no such loop to handle: it is native code, and its frame ends by swapping buffers and, in its browser build, giving the time back to the browser. The spin is specific to the PS2 game's own packet queue.

## Tests

- `tests/runtime/test_dispatch_idle.cpp` (19 checks): the fork's dispatcher alone. Whole turns are skipped once two turns cost the same; the slice ends where spinning would have ended it; another turn length, a new slice, a call that does not match, a slice with less than two turns left and a harness call are all left alone.
- `tests/runtime/test_gif_path3.cpp` (26 checks): the fork's GIF module alone. A masked PATH3 holds whole packets, each unmask lets one through, a packet that arrives in pieces is released whole, an open PATH3 drains the rest, 60 packets come out in order, and a saved state carries what is held and no more.
- `tests/runtime/run.sh ~/b3/ps2-web-recomp`: 5 programs, 202 checks, all pass.

## Not done

- The remaining GIF cost (2 to 3% of the thread) is the copy into the render queue; a release could hand the queue the buffer's own bytes, but the saving is small.
- The idle call is registered for the race's callback only. The menus use the same loop (their idle rose from 56% to 72%), so the same pointer value serves there; another screen could install a different callback and would simply spin as before.
- VU0 (`vu::run`, 38%) is untouched and is now the one large item. See `web-log-002.md` for the options.
- Nobody has played the new page by hand. The checks above are a scripted race.
