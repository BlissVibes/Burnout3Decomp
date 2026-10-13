# Gap 006: IOP heap AllocSysMemory is unhandled, so RWA clears IOP address 0 and erases the HLE return stubs

Date: 2026-10-11. Found in headless run 007 (`docs/re/boot-log-007.md`), the first run with the gap 005 patch. Analysis: Fable (ultracode workflow), from the runtime, repro, EE and reference readers' reports. Verification: workflow implementer.

## Symptom

With gap 005 applied the game leaves the DECI2 poll, loads all 27 IOP modules and, right after `[loadfile] cdrom0:\IOP\NETWORK\DRTYSCKF.IRX;1`, prints `[iopheap] method 4`. From then on the log is only `[iop] unknown instruction 00000030 at 0x0001E000 (ra 0x00001000)` / `[iop] fault: unknown instruction (pc 0x0001E004 thread 0)` (later also `00000001` at `0x0001E010` and `0x0001E02C`), with `[iop] guest call to 0x000E59A4 did not return` after 1,624 fault pairs, then the same for `0x00040F04` and `0x00020584` forever. No frame is written; explore.log reached 11.7 GB in 65 minutes.

With `--trace-iop --trace-iop-hle` (600-frame repro, `~/b3/out/repro-007-trace-iop.log`, lines 2111-2129) the lines before the first fault are, in order:

```
[sif] call server 0x80000003 fn 4 send 12 recv 4 mode 1 args 00002000 00000000 00000000 ...
[iopheap] method 4
[sys] SifSetDma(0x77) 01FFF900 00000001 01E817C0 00000030 from 0x0010C1E4 thread 1
[sif] command 0x00000000: 00000030 00000000 00000000 00000000 00000049 00000000 00000000 00000000 00000000 00000000 00002000 00000001 00000000 00000000
[iop-hle] sysclib:memcpy(00046E40, 0001E000, 00000030, 00000000) ra 0x00040F88 thread 0
[iop-hle] thsemap:iSignalSema(0000000B, 0001E000, 00000030, 00000000) ra 0x00040F98 thread 0
[iop-hle] sysclib:memset(00000000, 00000000, 00002000, 00002000) ra 0x00042A78 thread 18
[iop-hle] thsemap:PollSema(0000000B, ...) ra 0x000412A8 thread 18
[iop-hle] thsemap:WaitSema(0000000B, ...) ra 0x000412D8 thread 18
[iop-hle] thevent:iSetEventFlag(0000002E, 00000001, 00000000, 00000000) ra 0x000E59C8 thread 0
[iop] unknown instruction 00000030 at 0x0001E000 (ra 0x00001000)
[iop] fault: unknown instruction (pc 0x0001E004 thread 0)
```

## Cause

Two defects, one causing the other's visibility.

### 1. The IOP heap service has no method 4

SIF RPC server `0x80000003` is Sony's IOP heap service (hosted by the FILEIO module on a console). The EE side is libkernel's `sceSif*IopHeap` / `sceSif*SysMemory` client, bound once at `0x00113028` (client data `0x004E7240`, send buffer `0x004E72C0`, 4-byte reply at `0x004E7280`). Its method numbers, confirmed from the ELF wrappers, the decompiled Sony client in Tatsh/wavelength and Play!'s `Iop_Sysmem.cpp` (references listed in `docs/research/PROVENANCE.md`), are: 1 `AllocIopHeap(size)`, 2 `FreeIopHeap(addr)`, 3 `LoadIopHeap(path, addr)`, 4 `AllocSysMemory(type, size, addr)`, 5 `QueryMemSize`, 6 `QueryMaxFreeMemSize`, 7 `QueryTotalFreeMemSize`, 8 `QueryBlockTopAddress(addr)`, 9 `QueryBlockSize(addr)`. Method 4 is an allocation with the IOP kernel's `AllocSysMemory` flags (0 low, 1 high, 2 at the given address); it is not a disc load (that is method 3). Its 12-byte send buffer is in the IOP allocator's argument order `{size, type, addr}`: the wrapper at `0x00113120` stores its second argument (size) at offset 0, its first (type) at offset 4 and its third (addr) at offset 8, and the run 007 call carried `00002000 00000000 00000000` = size 0x2000, type 0, addr 0. The reply is the IOP address, 0 on failure; the EE wrapper also returns 0 when the RPC fails, and the game's seven call sites (`0x0028B69C` size 0x40000, `0x002A9E30`, `0x002AA8B4`, `0x002AA9BC`, `0x002AD0DC`, `0x002B4A58`, `0x00384524` size 0x4000) store the result without a zero check.

The HLE server in `runtime/src/iop/sif.cpp` (lines 643-656 before the fix) handled 1, 2, 5, 6, 7 only. Method 4 fell into the default case: `[iopheap] method 4`, reply 0. The call in run 007 came from `0x002AD0DC` (it forwards its caller's size, stores the result at `0x01E81004`, then calls `0x002AEFC0` with command code 0x49 and the result and size as arguments; the cid-0 packet above carries that 0x49 and the 0x2000). The 0x40000 stream-buffer request at `0x0028B69C` had not run yet. So the EE got IOP address 0 for a 0x2000-byte RWA buffer and told RWA.IRX about it with SIF command 0 (RWA registered `sceSifAddCmdHandler(0, 0x40F04, 0)` from thread 16). The handler copies the packet and signals semaphore 0xB; RWA worker thread 18 (entry `0x42B7C`) then clears the buffer: `sysclib:memset(0, 0, 0x2000)` from ra `0x42A78`. The HLE memset (`runtime/src/iop/libs.cpp`) writes straight into IOP RAM 0x0000-0x1FFF.

That range is the HLE kernel area. `kKernelBase = 0x1000` (`runtime/src/iop/kernel.cpp`); `boot()` resets `g_kernel_top` there and the first `hle_stub()` it allocates is `g_return_stub`, so the return stub is the single word at 0x1000; `g_exit_stub`, sifcmd's `g_loop_stub`/`g_finish_stub` (`sif.cpp`) and every other HLE stub follow. All of them became 0x00000000 = `nop`.

Every guest handler run by `call_guest()` gets `ra = g_return_stub` and `sp = kInterruptStack - (nesting-1) * 0x1000`. The first handler after the memset was PPPOE's 1 ms alarm handler `0xE59A4` (`thbase:SetAlarm(000E8800, 000E59A4, ...)` from thread 57; fired by `fire_alarm` in libs.cpp; the game's own `iSetEventFlag(0x2E, 1)` is the last HLE line before the fault). It returned to 0x1000, now a nop, and the interpreter slid through zeros up to 0x1E000: `kCmdBuffer` (`sif.cpp`), the HLE's SIF command receive buffer that `command()` fills with each EE packet before `call_guest(handler, kCmdBuffer, data)` and reports to the EE as SIF register 2. The packet's first word, its size 0x00000030, decodes as SPECIAL function 0x30, which the R3000 interpreter rejects (`runtime/src/iop/cpu.cpp`, `bad()` -> `kernel::fault`). The runtime reader's probe run proves the path: the `0xE59A4` probe (ra 0x1000, thread 0) is followed at the same timestamp by the `0x1E000` probe with `iSetEventFlag`'s arguments still in a0/a1. Nothing jumped to 0x1E000; "thread 0" means no IOP thread was current when the alarm fired. On a console the same memset would have overwritten the exception vectors and the kernel modules at the bottom of IOP RAM; the game only works there because `AllocSysMemory` never returns 0.

Why it loops instead of stopping: execution continues at 0x1E004 (the pc has already advanced; `bad()` does not touch it). Words 1-3 are zero; word 4 at 0x1E010 is 0x00000049, a jump-and-link-register through register zero, and the interpreter re-zeroes `r[0]` after every instruction, so the CPU jumps to address 0 and slides through the zeroed 0x0000-0x1DFFC back to 0x1E000. 0x1E000 / 4 = 30,720 instructions per lap, 50,000,000 / 30,720 = 1,628 laps, and run 007 shows 1,624 fault pairs before the first `did not return`. The second RWA packet (payload words 1 and 1 instead of 0x49 and 0x2000) puts invalid words at 0x1E010 and 0x1E02C, giving the three-address cycle seen later. With the exit and RPC stubs gone too, no IOP thread can finish and no RPC is ever answered; the EE waits on its semaphores and never reaches the next frame.

### 2. A fault inside a guest call does not end the call

`fault()` parked the current thread and set `g_switch`. `call_guest()` runs its loop while `!g_nested_done && guard > 0` and clears `g_switch` after every slice; nothing it checked was set by a fault. So a handler that faults kept executing whatever follows the bad word for 50 million cycles and logged two lines per fault with no limit. Every later handler (RWA's `0x40F04` on each EE command, SIO2MAN's interrupt-17 handler `0x20584` on every controller interrupt) paid the same price. On a console an exception inside an interrupt handler halts the IOP; the honest HLE equivalent is to abandon the handler at once, tell its caller it failed, and keep the interrupted thread intact. This did not cause the stall, but it turned one bad buffer into an unbounded log.

`LoadModuleBuffer` (sif.cpp) is not involved: it rejects a buffer that does not start with an ELF header. No IRX image was ever loaded from the heap in this run.

## Fix (generic, in the fork: `runtime/src/iop/sif.cpp`, `runtime/src/iop/kernel.cpp`, `runtime/src/iop/kernel.hpp`, `runtime/src/iop/cpu.cpp`, `runtime/src/iop/libs.cpp`)

### Part 1: implement the IOP heap service as Sony's client expects (`sif.cpp`)

The whole `register_hle_server(0x80000003u, ...)` block in `install_servers()` is replaced with:

```cpp
    register_hle_server(0x80000003u, [](std::uint32_t method, std::uint32_t args, std::uint32_t, std::uint32_t ret, std::uint32_t) {
        // Sony's IOP heap service (hosted by the FILEIO module on a console). The method numbers are
        // libkernel's sceSif*IopHeap / *SysMemory client order: 1 AllocIopHeap(size), 2 FreeIopHeap(addr),
        // 3 LoadIopHeap(addr, path), 4 AllocSysMemory(size, type, addr) in the IOP allocator's argument
        // order (type 0 low, 1 high, 2 at addr), 5 QueryMemSize, 6 QueryMaxFreeMemSize, 7 QueryTotalFreeMemSize,
        // 8 QueryBlockTopAddress(addr), 9 QueryBlockSize(addr). Every reply is one word; an allocation that
        // fails returns 0, which is what the EE wrappers map their own failures to as well.
        const auto word = [args](std::uint32_t i) { return ee_read<std::uint32_t>(args + 4u * i); };
        std::uint32_t result = 0;
        switch (method) {
        case 1: result = iop::kernel::alloc(word(0)); break;
        case 2: iop::kernel::free(word(0)); break;
        case 3: {
            // Load a whole disc file to an IOP address: {addr, path[252]}.
            const std::string path(reinterpret_cast<const char *>(g_ram + ((args + 4) & (kRamSize - 1))));
            std::uint32_t mode = 0, size = 0;
            const int fd = iop::io::stat(path, mode, size) ? iop::io::open(path, 1) : -1;
            if (fd < 0) {
                std::fprintf(stderr, "[iopheap] load %s failed\n", path.c_str());
                result = static_cast<std::uint32_t>(-1);
                break;
            }
            const std::uint32_t at = iop::phys(word(0)) & (iop::kRamSize - 1);
            std::vector<std::uint8_t> buffer(size);
            const int n = iop::io::read(fd, buffer.data(), size);
            iop::io::close(fd);
            if (n > 0) std::memcpy(iop::ram_ptr(at), buffer.data(), std::min<std::uint32_t>(static_cast<std::uint32_t>(n), iop::kRamSize - at));
            std::fprintf(stderr, "[iopheap] load %s to 0x%08X, %d bytes\n", path.c_str(), at, n);
            break;
        }
        case 4: result = iop::kernel::alloc(word(0), static_cast<int>(word(1)), word(2)); break;
        case 5: result = iop::kRamSize; break;
        case 6: result = iop::kernel::free_max(); break;
        case 7: result = iop::kernel::free_total(); break;
        case 8: result = word(0); break;
        case 9: result = 0; break;
        default:
            std::fprintf(stderr, "[iopheap] method %u\n", method);
            break;
        }
        store<std::uint32_t>(g_ram + ret, result);
        return true;
    });
```

Notes: `iop::kernel::alloc(size, mode, at)` already implements modes 0, 1 and 2 and is exactly what the IOP-side `sysmem:AllocSysMemory` HLE uses with the same argument mapping, so methods 1 and 4 are now the same allocator seen from the EE. Methods 8 and 9 mirror the IOP-side `QueryBlockTopAddress`/`QueryBlockSize` HLE. `iop::io::open(path, 1)` is the read-only open the real FILEIO uses for method 3 and `iop::io::stat` gives the size.

### Part 2: a fault ends the guest call it happened in (`kernel.cpp`, `kernel.hpp`, `cpu.cpp`)

`kernel.cpp`, next to `g_nested_done`:
```cpp
bool g_nested_faulted = false;
unsigned g_fault_count = 0;
constexpr unsigned kFaultLogLimit = 256;
unsigned g_kernel_writes_reported = 0;
```

`fault()` becomes:
```cpp
void fault(const char *reason) {
    ++g_fault_count;
    if (g_fault_count <= kFaultLogLimit)
        std::fprintf(stderr, "[iop] fault: %s (pc 0x%08X thread %d)\n", reason, g_cpu.pc, g_current);
    if (g_fault_count == kFaultLogLimit)
        std::fprintf(stderr, "[iop] %u faults logged; further faults are only counted\n", g_fault_count);
    if (g_nesting) {
        // The faulting code runs under call_guest: an interrupt, alarm, hard-timer, SIF command or SIF DMA
        // callback handler. On the console an exception inside such a handler halts the IOP; here the
        // handler is abandoned, the call ends now and reports failure to its caller, and the thread that
        // was interrupted resumes untouched when call_guest restores its context.
        g_nested_done = true;
        g_nested_faulted = true;
        return;
    }
    if (Thread *t = current()) {
        t->status = kDormant;
        g_current = 0;
    }
    g_switch = true;
}

bool fault_logging() { return g_fault_count < kFaultLogLimit; }
```

`call_guest()` becomes:
```cpp
std::uint32_t call_guest(std::uint32_t address, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3) {
    const Cpu saved = g_cpu;
    const bool saved_done = g_nested_done, saved_faulted = g_nested_faulted;
    ++g_nesting;
    g_nested_done = g_nested_faulted = false;
    g_cpu.r[4] = a0;
    g_cpu.r[5] = a1;
    g_cpu.r[6] = a2;
    g_cpu.r[7] = a3;
    g_cpu.r[29] = kInterruptStack - static_cast<std::uint32_t>(g_nesting - 1) * 0x1000u;
    g_cpu.r[31] = g_return_stub;
    g_cpu.pc = address;
    g_cpu.npc = address + 4;
    std::int64_t guard = 50'000'000;
    bool switch_wanted = g_switch;
    g_switch = false;
    while (!g_nested_done && guard > 0) {
        std::int64_t budget = kSlice;
        execute(g_cpu, budget);
        guard -= kSlice - budget;
        switch_wanted |= g_switch;
        g_switch = false;
    }
    g_switch = switch_wanted;
    if (guard <= 0) std::fprintf(stderr, "[iop] guest call to 0x%08X did not return\n", address);
    else if (g_nested_faulted && g_fault_count <= kFaultLogLimit) std::fprintf(stderr, "[iop] guest call to 0x%08X ended by a fault\n", address);
    // A handler that faulted or never returned reports 0: an interrupt handler's 0 masks its line, an
    // alarm or hard-timer handler's 0 is not re-armed, so a dead handler is not called again and again.
    const std::uint32_t result = g_nested_faulted || guard <= 0 ? 0u : g_cpu.r[2];
    g_cpu = saved;
    g_nested_done = saved_done;
    g_nested_faulted = saved_faulted;
    --g_nesting;
    return result;
}
```

`boot()`, after `g_nesting = 0;`:
```cpp
    g_nested_done = g_nested_faulted = false;
    g_fault_count = 0;
    g_kernel_writes_reported = 0;
```

`dump()` (`namespace ps2::iop`) prints as its first line:
```cpp
    std::fprintf(stderr, "[iop] %u faults\n", g_fault_count);
```

`kernel.hpp`, after `void fault(const char *reason);`:
```cpp
bool fault_logging();
void note_guest_write(const char *who, std::uint32_t at, std::uint32_t size, std::uint32_t ra);
```

`cpu.cpp`, `bad()` becomes:
```cpp
void bad(Cpu &c, std::uint32_t pc, std::uint32_t w) {
    if (kernel::fault_logging()) std::fprintf(stderr, "[iop] unknown instruction %08X at 0x%08X (ra 0x%08X)\n", w, pc, c.r[31]);
    kernel::fault("unknown instruction");
}
```

`execute()` already returns after `bad()` and after any HLE call when `switch_pending()` is true, and `switch_pending()` returns true when `g_nesting && g_nested_done`, so the loop in `call_guest` sees the fault on the next check without further changes. The new globals are not added to the save state: `state()` already refuses to save inside a guest call, so `g_nested_faulted` is always false at save time, and the counters are diagnostics.

### Part 3: name guest writes into the kernel area (`kernel.cpp`, `kernel.hpp`, `libs.cpp`)

Diagnostic only, so the next null-buffer bug is one line instead of a mystery. `kernel.cpp`, after `fault_logging()`:
```cpp
void note_guest_write(const char *who, std::uint32_t at, std::uint32_t size, std::uint32_t ra) {
    // Guest modules never own memory below kUserBase: 0x0000-0x0FFF is unused and 0x1000-0x1EFFF holds
    // the HLE stubs, the SIF command buffer and the interrupt stack. A write there is a guest bug, usually
    // a null buffer, that on the console would land on the exception vectors and the kernel modules.
    if (!size || phys(at) >= kUserBase || g_kernel_writes_reported >= 16) return;
    ++g_kernel_writes_reported;
    std::fprintf(stderr, "[iop] %s writes 0x%X bytes at 0x%08X from 0x%08X: below the user area\n", who, size, phys(at), ra);
}
```

`libs.cpp` (inside `namespace ps2::iop::kernel`, so the calls are unqualified), the sysclib block from `memcpy` to `bzero`:
```cpp
    def("sysclib", 12, "memcpy", [copy](Cpu &c) { note_guest_write("sysclib:memcpy", arg(c, 0), arg(c, 2), c.r[31]); copy(arg(c, 0), arg(c, 1), arg(c, 2)); c.r[2] = arg(c, 0); });
    def("sysclib", 13, "memmove", [copy](Cpu &c) { note_guest_write("sysclib:memmove", arg(c, 0), arg(c, 2), c.r[31]); copy(arg(c, 0), arg(c, 1), arg(c, 2)); c.r[2] = arg(c, 0); });
    def("sysclib", 14, "memset", [](Cpu &c) {
        note_guest_write("sysclib:memset", arg(c, 0), arg(c, 2), c.r[31]);
        if (phys(arg(c, 0)) + arg(c, 2) <= kRamSize) std::memset(ram_ptr(arg(c, 0)), static_cast<int>(arg(c, 1) & 0xFF), arg(c, 2));
        else for (std::uint32_t i = 0; i < arg(c, 2); ++i) write8(arg(c, 0) + i, static_cast<std::uint8_t>(arg(c, 1)));
        c.r[2] = arg(c, 0);
    });
    def("sysclib", 15, "bcmp", [](Cpu &c) {
        c.r[2] = std::memcmp(ram_ptr(arg(c, 0)), ram_ptr(arg(c, 1)), arg(c, 2)) ? 1u : 0u;
    });
    def("sysclib", 16, "bcopy", [copy](Cpu &c) { note_guest_write("sysclib:bcopy", arg(c, 1), arg(c, 2), c.r[31]); copy(arg(c, 1), arg(c, 0), arg(c, 2)); });
    def("sysclib", 17, "bzero", [](Cpu &c) { note_guest_write("sysclib:bzero", arg(c, 0), arg(c, 1), c.r[31]); std::memset(ram_ptr(arg(c, 0)), 0, arg(c, 1)); });
```

### Behaviour notes

- Part 1 is the fix. With it the EE receives a real IOP address (first fit from `kUserBase = 0x20000` upward for type 0, rounded to 0x100), RWA clears its own buffer, and the stubs at 0x1000 are never touched. The later 0x40000 request from `0x0028B69C` (audio stream buffer) and 0x4000 from `0x00384524` go through the same path; watch for `[iop] out of memory allocating` if the 2 MB IOP RAM gets tight behind the 27 modules (they end near 0x100650) and the thread stacks allocated at the top.
- Part 2 changes behaviour only when a handler faults or hits the 50M-cycle guard: the call ends, reports 0, and the interrupted thread's context is restored exactly as before. Returning 0 for a timed-out call is new (it returned whatever was in `v0`); a dead interrupt handler now masks its line and a dead alarm is not re-armed, which is what those result codes mean. Unknown-instruction logging stops after 256 faults and the total is printed by `dump()`; no log can grow unbounded from faults again.
- Part 3 prints at most 16 lines per boot and changes nothing else.
- Unknown heap methods still reply 0 (a failed allocation to the EE wrappers) with the `[iopheap] method N` line; all nine documented methods are handled, so that line should not appear again.
- Method 3 (`LoadIopHeap`) is implemented from the ps2sdk/Play! semantics without a caller in this game to test it; no call site was found in the ELF.

## Verification

1. Rebuild: `ninja -C /home/mark/b3/ps2-web-recomp/build/native-burnout3 ps2run`.
2. Short repro first (log capped, `--frames 600 --press 300:start --trace-iop --trace-iop-hle`, misses/memcard/shots under `/home/mark/b3/out/`). Expected:
   - `[sif] call server 0x80000003 fn 4 send 12 recv 4 mode 1 args 00002000 00000000 00000000 ...` with no `[iopheap] method 4` after it.
   - The following `[sif] command 0x00000000: 00000030 ... 00000049 ...` packet, then `sysclib:memset(00XXXXXX, 00000000, 00002000, ...) ra 0x00042A78 thread 18` with an address at or above `0x00020000` instead of `00000000`.
   - No `[iop] unknown instruction`, `[iop] fault:`, `did not return` or `below the user area` lines; `[iop-hle] thevent:iSetEventFlag(0000002E, ...) ra 0x000E59C8 thread 0` keeps recurring every millisecond (the PPPOE alarm now returns normally).
   - New traffic after it: `[sif] bind 0x00475453` (GTFSCDVD's "GTS" server) followed by `call server 0x00475453 fn 1`, and later `fn 4 ... args 00040000 00000000 00000000` from the audio player.
3. Full run: `tools/port.sh --frames 1800` (watch the log size). Expected: shots in `shots/`; the LOADING progress bar advances or the screen changes.
4. Optional check of Part 2 alone: run step 2 with Part 1 reverted; expected exactly one `unknown instruction 00000030 at 0x0001E000` / `fault:` pair per guest call, each followed by `[iop] guest call to 0x000E59A4 ended by a fault` (then `0x00040F04`, `0x00020584`), logging stopping after 256 faults, and the run finishing in minutes with a small log.
5. If the game stalls somewhere new, report the last `[sif]`/`[sys]` lines, the EE pc and the `[iop]` dump, not the fault lines.

## Result (workflow implementer, 2026-10-11)

Applied exactly as written (all three parts; `g_kernel_writes_reported` sits with the other new globals and is reset in `boot()`). `ninja` built `ps2run` clean with no warnings; the web build in `port.sh` step 6 also rebuilt and linked. Headless run 008 (`docs/re/boot-log-008.md`):

- 600-frame repro (`~/b3/out/repro-008.log`, 11 MB, 121,518 lines, exit 0 in under 20 s): `[iop] 0 faults`; zero `unknown instruction`, `fault:`, `did not return`, `below the user area` or `[iopheap] method` lines. The memset at ra `0x42A78` now clears `00100700` (above `kUserBase`) instead of `00000000`. With `--trace-iop` (`repro-008b.log`): the `fn 4` call at line 2111 has no `[iopheap] method 4` after it, the cid-0 packet carries `00100700 00002000`, the later `fn 4` calls for 0x4000 and 0x40000 are answered with no `[iop] out of memory`, `[sif] bind 0x00475453` follows and the GTS server reads six data files (19 calls). The PPPOE alarm handler keeps returning normally.
- Full run (`tools/port.sh --frames 1800`): 49 s wall, exit 0, explore.log 10 KB, 1800 frames, `[gs] 145509 primitives drawn`, `[iop] 0 faults`. Scan: GAPS for the two pre-existing findings only (`modload #18`, unknown tag `loadfile`).
- Six frames: the LOADING screen with the progress bar now about three quarters filled, identical from frame 300 to frame 1800 (run 006 had an empty bar).
- Part 2's "ended by a fault" path compiled but was not exercised: no fault occurred in any run, and the optional step 4 was not run.
- The run then stalls somewhere new: the loading state machine never completes because RWA's worker thread 18 waits on event flag 0x14 bit 2, which nothing sets. That is gap 007 (see `docs/re/boot-log-008.md`).

## Status

- Applied in the fork on branch `burnout3` by the workflow implementer and verified by run 008. Uncommitted (`git status`: `M runtime/src/iop/cpu.cpp`, `kernel.cpp`, `kernel.hpp`, `libs.cpp`, `sif.cpp`; 6 files with the gap 005 change, 91 insertions, 16 deletions). Proposed commit message `iop: implement the IOP heap service (AllocSysMemory, LoadIopHeap, block queries) and end a guest call on a fault`.
- Later: committed as `fca7bf4`.
- References used for the method table (knowledge only, no code copied): ps2sdk, wavelength, Play!, Tenkaichi3Decomp; see `docs/research/PROVENANCE.md`.
