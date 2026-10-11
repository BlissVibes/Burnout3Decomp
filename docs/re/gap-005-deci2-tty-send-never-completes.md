# Gap 005: DECI2 send never completes, so the debug-TTY write polls forever

Date: 2026-10-11. Found in headless run 006 (`docs/re/boot-log-006.md`). Analysis: Fable. Verification: Opus 1.

## Symptom

The game draws its LOADING screen (first visible frame of the port) with an empty progress bar, then the main thread sits at `0x0010D508`, calling the same small routine in a loop while a flag at `0x004E3B9C` stays set. No system calls other than that one, no SIF traffic. The IOP had just loaded the network modules.

## Cause

The routine the loop calls, `0x0010D010`, is a wrapper for syscall `0x7C`, `Deci2Call`, with sub-command 4 (`sceDeci2Poll`). The function around it, starting at `0x0010D3E8`, is the EE kernel-library TTY writer that sends text to a development host over DECI2: it marks a global "busy" flag, copies the text into a message buffer (converting newlines), issues sub-command 3 (`sceDeci2ReqSend`), and, if that returns a non-negative value, polls with sub-command 4 until the completion callback clears the flag. If `ReqSend` returns a negative value it clears the flag itself and returns an error. This is the printf path of games built with Sony's debug TTY left enabled; the text would appear on the development kit's host.

The runtime handles syscall `0x7C` as `ret(c, 0)` for every sub-command (`runtime/src/kernel.cpp`, around line 706). `ReqSend` therefore "succeeds" but no completion ever arrives, and the poll never ends. On a retail console there is no DECI2 host; the send fails immediately and the library takes the error path.

## Fix (generic, in the fork: `runtime/src/kernel.cpp`)

Replace the `0x7C` case:
```cpp
    case 0x7C: {
        // Deci2Call. No development host is attached, as on a retail console: a request to send
        // (3 ReqSend, 7 ExReqSend) fails at once so the kernel-library TTY writer takes its error
        // path instead of polling for a completion that never comes. Everything else stays a no-op.
        const std::uint32_t command = arg(c, 0);
        ret(c, command == 3 || command == 7 ? -1 : 0);
        break;
    }
```
`ret()` takes a signed 64-bit value, so `-1` is returned as the kernel's error code.

Optional, later: print the TTY text to stderr as `[ee-tty]` so the game's own debug output becomes visible in our logs. The message layout is library-specific, so leave it until it is worth the reverse engineering.

## Verification

1. Rebuild `ps2run`, `tools/port.sh --frames 1800`.
2. Expected: thread 1 leaves `0x0010D508`; the LOADING progress bar advances or the screen changes within the run; `[gs] N primitives drawn` rises well above 143; new syscalls and SIF traffic after the network modules load.
3. If the game stops on the loading screen elsewhere, send the Task 6 repro's last SIF lines and the new pc.

## Result (Opus 1, 2026-10-11)

Applied exactly as written (the `0x7C` case in `runtime/src/kernel.cpp`, 9 lines changed). Headless run 007 (`docs/re/boot-log-007.md`):

- Thread 1 leaves the poll at `0x0010D508`: for the first time there is SIF traffic after `[loadfile] cdrom0:\IOP\NETWORK\DRTYSCKF.IRX;1`, an IOP heap RPC (`sceSifAllocSysMemory`) for RWA.IRX.
- That RPC is unhandled by the runtime (`[iopheap] method 4`, reply 0) and its null buffer wedges the IOP interpreter in an unbounded fault loop: run 007 wrote no frame and 11.7 GB of log in 65 minutes. That is gap 006 (`docs/re/gap-006-iop-heap-alloc-sysmemory-unhandled.md`), fixed and verified in run 008, where the LOADING bar shows progress for the first time.
- The TTY text itself is still not printed (the optional `[ee-tty]` idea above remains open).

## Status

- Applied in the fork on branch `burnout3` by Opus 1 and verified by runs 007 and 008. Uncommitted; commit message `kernel: Deci2Call send requests fail like a retail console without a host`.
