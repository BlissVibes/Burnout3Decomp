# Runtime function harness: the recompiled build as the oracle for decompiled functions

Date: 2026-10-12. Status: **Parts 1, 2, 3a and 4 applied in the fork (see `docs/re/harness-log-001.md`); Part 3b and Part 5 entry snapshots not applied**. Design: Fable (milestone 4, harness design task), from reading the fork at `3e42c71` (`host/main.cpp`, `runtime/src/system.cpp`, `kernel.cpp`, `dispatch.cpp`, `memory.cpp`, `runtime/include/ps2/{cpu,memory,generated,fpu,state,system}.hpp`, `recomp/codegen.cpp`, `recomp/analysis.cpp`, `generated/burnout3/{table.cpp,units.hpp,unit_*.cpp}`), plus `docs/research/reb3-crosswalk.md`, the design spec and gaps 001-007. Verification of the fork facts: workflow verifier, same day; its corrections are applied in the text below and listed at the end. Nothing in the fork or the repo was modified. All code below is proposed; nothing was built or run. File references are `runtime/src/` or `runtime/include/ps2/` unless a directory is given; `gs.cpp`, `queue.cpp` and `queue.hpp` are under `runtime/src/gs/`, `gpu.cpp` under `runtime/src/gs/gpu/`, `spu2.cpp` under `runtime/src/iop/`.

## What the runtime gives us (verified)

- **Register file.** `ps2::Cpu` (`runtime/include/ps2/cpu.hpp:53-68`): `Reg128 r[32]` (128-bit GPRs; the 32-bit view is `w[0]`, the sign-extended 64-bit write is `set32()` from `generated.hpp:63`), `hi`, `lo`, `sa`, `pc`, `budget` (host-only cycle counter), `base` (relocatable-module base, unused by Burnout), `f[32]` (FPU registers as raw `uint32_t` bit patterns), `facc`, `fcr31`, `Vu0Regs vu0` (COP2 macro-mode state), `cop0[32]`. One global instance `ps2::g_cpu` (`system.cpp:37`).
- **Memory.** `g_ram[kRamSize + 64]` (32 MB, `memory.cpp:11`), `g_spr[16 KB]` (`memory.cpp:12`), `kScratchpadBase = 0x70000000` (`memory.hpp:11`). `fast<T>(a)` (`memory.hpp:28-33`) maps `a < 32 MB` straight to `g_ram + a` and the scratchpad window to `g_spr`; anything else goes through `slow_*` → `resolve()` (`memory.cpp:38-48`), which applies `physical()` (`memory.cpp:91-96`, declared in the private header `runtime/src/hw.hpp:19`: 0x8/0x9/0xA/0xB kseg → `& 0x1FFFFFFF`, 0x2/0x3 → `& 0x0FFFFFFF`), then RAM, VU memories, I/O (`hw::write32` etc.) or ROM. The ELF is loaded at its link addresses by `load_elf` (`system.cpp:309-327`) into `g_ram`.
- **Generated code.** One C++ function per 16 KB of guest address space: `void unit_XXXX(Cpu &c, std::uint32_t entry, std::uint32_t depth)` (`generated.hpp:13`, `codegen.cpp:191`), 225 units for Burnout (`table.cpp`, `register_generated_code()` → `register_units(gen::kUnits, 225u)`). Each unit starts with `switch (entry) { case id: goto L_addr; ... }` (`codegen.cpp:193-195`) and a dense `entries_XXXX[4096]` table mapping `(pc - base) >> 2` to the entry id (`codegen.cpp:186-190`). `dispatch(c, 0, depth)` (`dispatch.cpp:116-144`) looks the pc up through `unit_for()` and the table and calls the unit; `call(c, target, depth)` (`dispatch.cpp:146-150`) is the indirect-call path. **Direct `jal` calls do not go through the runtime**: `call_to()` emits `if (depth < kMaxDepth) unit_XXXX(c, id, depth + 1u); else { c.pc = target; return; } if (c.pc != ret) [[unlikely]] return;` (`codegen.cpp:274-284`; example in `unit_0004.cpp:595`: `unit_0003(c, 342u, depth + 1u)` for `0x0010F1E8`). Only `jalr`/dynamic sites use `call()` (`codegen.cpp:360-364`, `321-327`). What reaches `dispatch()`: `jalr` (via `call()`), dynamic `jal`, `jal` to non-code targets, trampolines past `kMaxDepth`, `jr rs` whose target is outside the unit or not an entry, and the outer loops. What does **not**: direct `jal` (a unit call), `j` and branches (`goto` in-unit, `[[clang::musttail]]` unit call cross-unit, `codegen.cpp:268-270`), and a same-unit `jr rs` (rs != ra) whose target is an entry, which does `entry = entries_XXXX[...]; goto dispatch_entry;` (`codegen.cpp:356`, e.g. `unit_00A2.cpp:2274`). `jr ra` is `{ const std::uint32_t t = c.r[31].w[0]; <delay slot> c.pc = t; return; }` (`codegen.cpp:354`). Syscalls are `c.pc = pc+4; syscall(c, pc); if (c.pc != pc+4) return;` (`codegen.cpp:421`). Backward jumps carry the only scheduler re-entry: `if (c.budget < 0 && preempt(c)) { c.pc = target | kUnwind; return; }` (`codegen.cpp:267`, emitted by `jump()` only when the target is at or before the source; used for all branch forms at 344, 347, 350). `kMaxDepth = 192` (`generated.hpp:15`); past it the callee is trampolined: `c.pc = target; return;` and the outer loop re-dispatches at depth 0 (every call return address is an entry: `analysis.cpp:87`). `Program::functions` (`analysis.hpp:16`, filled at `analysis.cpp:23`) is currently unused by `codegen.cpp`, so the Part 3b check is new code.
- **The existing single-call loop.** `kernel::run_guest()` (`kernel.cpp:172-201`) is already a function-call harness for interrupt handlers and alarms: it saves the context, sets `a0-a3`, `gp`, `sp = kKernelStackTop (0x80000)` (minus 0x4000 when nested), `ra = kHandlerReturn (0x1000, system.hpp:13)`, `pc = address`, pushes `g_guest_calls`, then `while (!halted()) { c.pc &= ~kUnwind; if (c.pc == kHandlerReturn) break; if (c.budget <= 0) set_budget(c, 20000); dispatch(c, 0, 0); }` (lines 189-194). `dispatch()` returns at once for `kUnwind` (`dispatch.cpp:118`) and for `pc == kHandlerReturn || pc == kIdle` (`dispatch.cpp:119`; `kIdle = 0x1010`, `system.hpp:14`). The harness below is this loop with isolation guards, argument setup, memory diffing and output.
- **Scheduler coupling.** `preempt()` (`system.cpp:196-200`) runs due events (vblank → frame counter, presses, IOP sync via `run_due_events()` → `sync_iop()` → `iop::run()`), refills the budget and returns `g_halted || g_state_request || kernel::must_unwind()`. `run_due_events()` (`system.cpp:66-76`) is called only from `idle()` (154), `preempt()` (197) and `run()` (375). `now()` (`system.cpp:181`) is `g_base + g_given - g_cpu.budget`, read by COP0 Count (`kernel.cpp:816`) and the timers (`hw.cpp:65,71,106`). `request_iop_sync()` (`system.cpp:338-340`, called from `SifSetDma` at `kernel.cpp:699`) cuts the budget to 64. Interrupt handlers run only from `kernel::service()` in `run()` (`system.cpp:376`), never from inside `dispatch()`. Thread switches happen inside syscalls via `reschedule()` → `switch_to()`, which loads another context and sets `c.pc |= kUnwind` (`kernel.cpp:127-145`; with no runnable thread it sets `kIdle | kUnwind`, `kernel.cpp:140-141`). A game-installed custom syscall handler (`SetSyscall`) runs through `run_guest()` from `handle_syscall` (`kernel.cpp:462-468`, before the switch).
- **Threads in ps2run.** None. `host/main.cpp` (`main()` at lines 33-97) never calls `init_graphics()`, `start_render_thread()` or `start_vu_thread()` (only `host/web.cpp:363,378,383` do; definitions `system.cpp:421-423`), so `gs::queue::active()` and `vu_active()` are false (`queue.cpp:59-60`, `Ring::active()` is `capacity_ != 0`, set only by `Ring::start()`): GIF/GS, VIF1/VU1 and VU0 micro (`vu_micro.cpp:921 run()`) all execute inline on the EE thread, `gs::state()` serialises inline (`gs.cpp:1024-1029`), `sync_vu_thread()` is a no-op (`system.cpp:225`). `gs::init()` (`gs.cpp:371-381`) is reached lazily through `render_reset()` (`gs.cpp:383`, the `init()` call at 384); `PS2_GS=off` (read at `gs.cpp:377-379`) selects the software renderer and skips WebGPU; `PS2_GPU_FALLBACK` only picks the fallback adapter (`gpu.cpp:859`). Without `PS2_GS=off` the WebGPU adapter is still created lazily on the first `render_reset()`; that is a gpu-library matter, not a runtime thread.
- **Save state.** `--save-state frame:file` (`main.cpp:64-70`) sets `g_state_request` in `vblank()` (`system.cpp:140`; `vblank()` is lines 128-142); `run()` honours it at the top of its loop (`system.cpp:371-373`), before `c.pc &= ~kUnwind` (line 375), so the saved `pc` may carry bit 0. `machine_state()` (`system.cpp:231-266`): magic `PS2STATE`, `kStateVersion = 4` (line 213), tag `ee`, `g_cpu` as a POD (budget and base included), `g_ram` (32 MB), `g_spr`, `g_base`, `g_given`, `g_frames`, `g_iop_synced`, `pal`, events, then `kernel`, `hw`, `dmac`, `vif`, `vu`, `ipu`, `gs`, `gif`, `iop`, `sif`, tag `end`. `kernel::state()` refuses to save inside a handler or a guest call (`kernel.cpp:851`: `g_in_handler || !g_guest_calls.empty()`). `load_state` (`system.cpp:284-305`) restores everything and sets `g_resumed`. A state is roughly 40 MB (EE RAM 32 MB, IOP RAM 2 MB, SPU2 RAM 2 MB, GS memory 4 MB, VU memories).
- **Tracing.** `--trace-syscalls` prints each syscall in `handle_syscall` (`kernel.cpp:470-472`); `--trace-threads` prints switches and creations (`kernel.cpp:138,142,286`). There is no per-call hook and no write-tracking bitmap: `st32` etc. write `g_ram` directly through `fast<T>()`. The recompiler's `--probes FILE` (`recomp/main.cpp:109,148-149`, `codegen.cpp:215`) emits `probe(c, pc)` prints at listed addresses but needs a regeneration.
- **FPU helpers.** `ps2::fp::add/sub/mul` (`fpu.hpp:24-69`, integer mantissa arithmetic: flush denormals to signed zero, truncate toward zero), `fp::div` and `fp::rsqrt` returning `fp::Result{value, invalid}` (`fpu.hpp:92-99, 107-113`; double division then `from_double` truncation), `fp::sqrt` (`101-105`), `fp::cvt_w_s` / `cvt_s_w` (`139-140`, via `to_fixed`/`from_fixed`), `fp::max/min/key` (`115-122`; `key` is the signed-magnitude compare key), `fp::flush`, constants `kSign/kAbs/kOne`; wrappers `fpu_compare`, `fpu_div`, `fpu_rsqrt` (`generated.hpp:66-80`, the last two set `fcr31 |= 0x30000` on invalid). COP2 macro ops are `ps2::vu::*` on `c.vu0` (`vu.hpp`, `vu_ops.hpp`). MADD is `fp::add(c.facc, fp::mul(a, b))`: two roundings, never fused (`codegen.cpp:641`).

## Design summary

1. **Snapshots** reuse the state file: `--snapshot-every N --snapshot-dir DIR` writes `DIR/sNNNNNN.state` at every Nth vblank with the existing `machine_state()`. No format change, version stays 4.
2. **One call** (`--call ADDR --snapshot FILE [--a0 X ...] [--f12 Y ...] [--buf a0:SIZE[:HEX]] [--poke ADDR:HEX] [--out FILE.json]`) boots, loads the state, writes the arguments into `g_cpu`, carves a stack frame below the snapshot thread's `sp`, sets `ra = kHandlerReturn` and runs `dispatch()` in the `run_guest` loop with four guards: `preempt()` aborts instead of running events (so no vblank, no IOP, no interrupts, no reschedule); `now()` is frozen; blocking, thread-changing and custom-handler syscalls abort; a thread switch, idle or halt is detected through `kUnwind` and `kIdle`. Output: registers before and after, cycles, 16-byte-granular RAM and scratchpad ranges that changed (stack frame reported separately), I/O register stores, syscall numbers, optional entry trace; JSON plus a `.mem` sidecar with the bytes.
3. **Hooks**: a runtime registry `ps2::register_hooks()` + `enter(c, address)`; checked in `dispatch()` (covers indirect calls, trampolines and the harness entry at once, so `--call ADDR --hook NAME` runs the decompiled function through the same harness with the same output) and, for direct calls and same-unit register jumps in live play, callee tracing and entry snapshots, a one-line check emitted at every function label by the recompiler (needs one regeneration). Decompiled code is plain C++ compiled into `ps2run`/`ps2web` from a directory given as `-DPS2_HOOKS=`; it reads EE memory through `g_ram`/`ld*`/`st*` and does float arithmetic through `fp::`.
4. **Driver** `tools/difftest.py`: for F and a snapshot set, run F recompiled and F hooked on each snapshot, compare the return registers, callee-saved registers, changed ranges and bytes, I/O and syscalls; text report, JSON summary, exit code.
5. **Gathering**: `--snapshot-every` during the scripted race, plus `--snapshot-on-entry ADDR[:every[:max]]` which saves a state from inside `enter()` at the chosen function (real arguments in `g_cpu`).

Smallest useful first step = Parts 1, 2 and 3a (runtime registry + dispatch check): no recompiler change, ~450 lines, enough to validate every leaf function with the driver. Part 3b (codegen check) is a separate, later step.

## Fix (generic, in the fork: `host/main.cpp`, `runtime/include/ps2/system.hpp`, `runtime/src/system.cpp`, `runtime/src/kernel.cpp`, `runtime/src/memory.cpp`, `CMakeLists.txt`; new `runtime/include/ps2/harness.hpp`, `runtime/src/harness.cpp`, `runtime/include/ps2/hook.hpp`, `runtime/src/hooks.cpp`)

### Part 1: periodic snapshots (`system.hpp`, `system.cpp`, `main.cpp`)

1. `system.hpp`, in `struct Options` after `std::uint64_t save_state_frame = 0;`:
```cpp
    std::uint64_t snapshot_every = 0;   // save a state every N frames into snapshot_dir
    std::string snapshot_dir;
```
and after `std::uint64_t frame_count();`:
```cpp
std::uint32_t state_version();
```

2. `system.cpp`, in `vblank()` after the `save_state_frame` line (line 140):
```cpp
    if (g_options.snapshot_every && g_frames % g_options.snapshot_every == 0) g_state_request = true;
```

3. `system.cpp`, `run()`: replace the `if (g_state_request) {...}` block (the `if` at line 370, its body and closing brace at 371-373) with:
```cpp
        if (g_state_request) {
            g_state_request = false;
            if (g_options.save_state_frame && g_frames == g_options.save_state_frame) save_state(g_options.save_state_path);
            if (g_options.snapshot_every && g_frames % g_options.snapshot_every == 0) {
                char name[64];
                std::snprintf(name, sizeof name, "/s%06llu.state", static_cast<unsigned long long>(g_frames));
                save_state(g_options.snapshot_dir + name);
            }
        }
```

4. `system.cpp`, after `std::uint64_t frame_count() { return g_frames; }`:
```cpp
std::uint32_t state_version() { return kStateVersion; }
```
(`kStateVersion` is declared in the anonymous namespace above; both are in `namespace ps2`, so this compiles as is.)

5. `main.cpp`, two new flags in the option loop:
```cpp
        else if (a == "--snapshot-every" && i + 1 < argc) ps2::g_options.snapshot_every = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--snapshot-dir" && i + 1 < argc) ps2::g_options.snapshot_dir = argv[++i];
```

### Part 2: the single-call harness (`harness.hpp`, `harness.cpp`, `system.cpp`, `kernel.cpp`, `memory.cpp`, `main.cpp`, `CMakeLists.txt`)

6. New `runtime/include/ps2/harness.hpp`:
```cpp
#pragma once

#include "cpu.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ps2::harness {

// A buffer the harness places in the reserved stack area and points an argument register at.
struct Buffer {
    int reg;                                  // 0-7: a0-a3, t0-t3
    std::uint32_t size;
    std::vector<std::uint8_t> bytes;          // initial contents, zero-padded to size
};

// One call of a recompiled (or hooked) function on a loaded snapshot.
struct Request {
    std::uint32_t address = 0;                // entry address as the game calls it
    std::optional<std::uint32_t> gpr[8];      // a0-a3, t0-t3 (r4-r11); unset keeps the snapshot value
    std::optional<std::uint32_t> fpr[8];      // f12-f19
    std::optional<std::uint32_t> gp, sp;      // r28; r29 is the top below which the frame is carved
    std::uint32_t stack_reserve = 0x1000;     // bytes carved below sp: the frame, and the buffers above it
    std::int32_t budget = 500'000'000;        // EE cycles before the call counts as hung (1.7 s of EE time)
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> pokes;   // bytes stored before the call
    std::vector<Buffer> buffers;
    bool trace = false;                       // record function entries (hooks.cpp enter())
};

enum class Status { ok, no_code, bad_request, hung, thread_switch, forbidden_syscall, halted };

struct Range {
    std::uint32_t address, length;
    std::uint64_t hash;                       // FNV-1a 64 of the bytes after the call
};

struct IoWrite {
    std::uint32_t address;
    std::uint64_t value;
    std::uint8_t size;
};

struct Result {
    Status status = Status::ok;
    std::string detail;
    Cpu before{}, after{};                    // register files at entry and at the jr ra
    std::uint32_t sp = 0;                     // the sp the function was entered with
    std::uint32_t scratch = 0;                // first byte of the buffer area
    std::int64_t cycles = 0;                  // budget consumed by recompiled code (0 for a hook)
    std::vector<Range> ram, spr, stack;       // changed 16-byte-granular ranges; stack = RAM below sp
    std::vector<IoWrite> io;                  // stores that reached hardware registers
    std::vector<std::uint32_t> syscalls;      // syscall numbers, in order
    std::vector<std::uint32_t> entries;       // function entries seen when trace is set
    bool hook_ran = false;                    // the entry itself was served by a hook
};

Result call(const Request &request);
bool write(const Result &result, const std::string &json_path);   // also writes json_path + ".mem"
bool parse_option(const std::string &flag, const char *value, Request &request);
const char *status_name(Status status);

// Switches the rest of the runtime consults while a call runs.
extern bool g_active;
extern std::uint64_t g_time;                  // what now() reports during a call
bool preempt(Cpu &c);                         // what ps2::preempt() does during a call
// true: forbidden, c.pc already unwound. custom_handler: the game installed a SetSyscall handler for
// this number, which would run through kernel::run_guest() (its loop strips kUnwind and refills the
// budget, so an abort inside it could never reach call()); such calls are forbidden too.
bool syscall(Cpu &c, std::uint32_t number, std::uint32_t pc, bool custom_handler);
void io_write(std::uint32_t address, std::uint64_t value, unsigned size);
void entered(std::uint32_t address, bool by_hook);               // from hooks.cpp enter()

}
```

7. New `runtime/src/harness.cpp`:
```cpp
#include "ps2/harness.hpp"

#include "kernel.hpp"
#include "ps2/generated.hpp"
#include "ps2/memory.hpp"
#include "ps2/system.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace ps2::harness {

bool g_active = false;
std::uint64_t g_time = 0;

namespace {

constexpr std::uint32_t kFallbackStackTop = 0x00080000u;   // the kernel stack run_guest uses
constexpr std::uint32_t kStackWindow = 0x10000u;            // writes this far below sp count as the frame

Status g_abort = Status::ok;
std::string g_abort_detail;
Result *g_result = nullptr;

// Syscalls that end, create, start or block threads, or change the kernel. Everything else runs and is
// recorded. A SignalSema that wakes a higher-priority thread still switches threads through
// reschedule(); call() catches that afterwards through kUnwind and the current-thread check.
bool forbidden(std::uint32_t n) {
    switch (n) {
    case 0x04: case 0x06: case 0x07:                                              // Exit, LoadExecPS2, ExecPS2
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:  // thread create .. iTerminateThread
    case 0x32: case 0x44:                                                         // SleepThread, WaitSema
    case 0x3C: case 0x3D: case 0x74:                                              // SetupThread, SetupHeap, SetSyscall
        return true;
    default:
        return false;
    }
}

std::uint64_t fnv1a(const std::uint8_t *p, std::size_t n) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

void diff(const std::uint8_t *before, const std::uint8_t *after, std::uint32_t size, std::uint32_t base, std::vector<Range> &out) {
    constexpr std::uint32_t kBlock = 16;
    std::uint32_t start = UINT32_MAX;
    for (std::uint32_t at = 0; at <= size; at += kBlock) {
        const bool differs = at < size && std::memcmp(before + at, after + at, kBlock) != 0;
        if (differs && start == UINT32_MAX) start = at;
        if (!differs && start != UINT32_MAX) {
            out.push_back({base + start, at - start, fnv1a(after + start, at - start)});
            start = UINT32_MAX;
        }
    }
}

std::uint32_t parse_u32(const char *text) {
    // 0x.. hex, decimal, or f:1.5 for the bit pattern of a float
    if (text[0] == 'f' && text[1] == ':') {
        const float f = std::strtof(text + 2, nullptr);
        std::uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return bits;
    }
    return static_cast<std::uint32_t>(std::strtoul(text, nullptr, 0));
}

std::vector<std::uint8_t> parse_hex(const std::string &text) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::strtoul(text.substr(i, 2).c_str(), nullptr, 16)));
    return out;
}

int reg_index(const std::string &name) {
    static const char *const kNames[8] = {"a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3"};
    for (int i = 0; i < 8; ++i)
        if (name == kNames[i]) return i;
    return -1;
}

void hex128(std::FILE *f, const Reg128 &r) {
    std::fprintf(f, "\"%016llx%016llx\"", static_cast<unsigned long long>(r.d[1]), static_cast<unsigned long long>(r.d[0]));
}

void cpu_json(std::FILE *f, const Cpu &c) {
    std::fprintf(f, "{\"r\":[");
    for (int i = 0; i < 32; ++i) { if (i) std::fputc(',', f); hex128(f, c.r[i]); }
    std::fprintf(f, "],\"hi\":"); hex128(f, c.hi);
    std::fprintf(f, ",\"lo\":"); hex128(f, c.lo);
    std::fprintf(f, ",\"sa\":%u,\"pc\":\"0x%08X\",\"f\":[", c.sa, c.pc);
    for (int i = 0; i < 32; ++i) std::fprintf(f, "%s\"0x%08X\"", i ? "," : "", c.f[i]);
    std::fprintf(f, "],\"facc\":\"0x%08X\",\"fcr31\":\"0x%08X\",\"vf\":[", c.facc, c.fcr31);
    for (int i = 0; i < 32; ++i) { if (i) std::fputc(',', f); hex128(f, c.vu0.vf[i]); }
    std::fprintf(f, "],\"acc\":"); hex128(f, c.vu0.acc);
    std::fprintf(f, ",\"vi\":[");
    for (int i = 0; i < 16; ++i) std::fprintf(f, "%s%u", i ? "," : "", c.vu0.vi[i]);
    std::fprintf(f, "],\"q\":\"0x%08X\",\"p\":\"0x%08X\",\"i\":\"0x%08X\",\"vr\":\"0x%08X\",\"mac\":\"0x%08X\",\"status\":\"0x%08X\",\"clip\":\"0x%08X\"}",
                 c.vu0.q, c.vu0.p, c.vu0.i, c.vu0.r, c.vu0.mac, c.vu0.status, c.vu0.clip);
}

void ranges_json(std::FILE *f, const char *name, const std::vector<Range> &v) {
    std::fprintf(f, "\"%s\":[", name);
    for (std::size_t i = 0; i < v.size(); ++i)
        std::fprintf(f, "%s{\"at\":\"0x%08X\",\"len\":%u,\"fnv\":\"%016llx\"}", i ? "," : "", v[i].address, v[i].length, static_cast<unsigned long long>(v[i].hash));
    std::fputc(']', f);
}

void ranges_bytes(std::FILE *m, std::uint8_t kind, const std::vector<Range> &v, const std::uint8_t *memory, std::uint32_t base) {
    for (const Range &r : v) {
        std::fwrite(&kind, 1, 1, m);
        std::fwrite(&r.address, 4, 1, m);
        std::fwrite(&r.length, 4, 1, m);
        std::fwrite(memory + (r.address - base), 1, r.length, m);
    }
}

}

const char *status_name(Status s) {
    switch (s) {
    case Status::ok: return "ok";
    case Status::no_code: return "no_code";
    case Status::bad_request: return "bad_request";
    case Status::hung: return "hung";
    case Status::thread_switch: return "thread_switch";
    case Status::forbidden_syscall: return "forbidden_syscall";
    case Status::halted: return "halted";
    }
    return "?";
}

bool preempt(Cpu &) {
    // Never run events, the IOP or the scheduler inside a call. The budget ran out: the call hangs.
    g_abort = Status::hung;
    g_abort_detail = "cycle budget exhausted";
    return true;   // the generated code sets c.pc |= kUnwind and returns to call()
}

bool syscall(Cpu &c, std::uint32_t number, std::uint32_t pc, bool custom_handler) {
    if (g_result) g_result->syscalls.push_back(number);
    if (!forbidden(number) && !custom_handler) return false;
    g_abort = Status::forbidden_syscall;
    char text[96];
    std::snprintf(text, sizeof text, "%ssyscall 0x%02X at 0x%08X", custom_handler ? "custom-handler " : "", number, pc);
    g_abort_detail = text;
    c.pc = pc | kUnwind;
    return true;
}

void io_write(std::uint32_t address, std::uint64_t value, unsigned size) {
    if (g_result) g_result->io.push_back({address, value, static_cast<std::uint8_t>(size)});
}

void entered(std::uint32_t address, bool by_hook) {
    if (!g_result) return;
    if (g_result->entries.empty() && address == g_result->before.pc) g_result->hook_ran = by_hook;
    g_result->entries.push_back(address);
}

Result call(const Request &q) {
    Result out;
    Cpu &c = g_cpu;
    c.pc &= ~kUnwind;
    if (!has_code(q.address)) {
        out.status = Status::no_code;
        out.detail = "no translated entry at that address";
        return out;
    }

    // Arguments and the frame. The top is the snapshot thread's sp (or the given one); the frame is
    // carved below it and the harness buffers live just above the new sp, so writes into them count
    // as results while the function's own frame does not.
    for (int i = 0; i < 8; ++i) {
        if (q.gpr[i]) c.r[4 + i].sd[0] = static_cast<std::int32_t>(*q.gpr[i]);
        if (q.fpr[i]) c.f[12 + i] = *q.fpr[i];
    }
    if (q.gp) c.r[28].sd[0] = static_cast<std::int32_t>(*q.gp);
    std::uint32_t top = q.sp ? *q.sp : c.r[29].w[0];
    if (!q.sp && (kernel::current_thread() == 0 || top < q.stack_reserve + 0x1000u || top > kRamSize)) top = kFallbackStackTop;
    const std::uint32_t sp = (top - q.stack_reserve) & ~15u;
    std::uint32_t scratch = sp + 16u;
    for (const Buffer &b : q.buffers) {
        const std::uint32_t size = (b.size + 15u) & ~15u;
        if (b.reg < 0 || b.reg > 7 || scratch + size > top) {
            out.status = Status::bad_request;
            out.detail = "buffers do not fit in the stack reserve";
            return out;
        }
        std::memset(g_ram + scratch, 0, size);
        std::memcpy(g_ram + scratch, b.bytes.data(), std::min<std::size_t>(b.bytes.size(), size));
        c.r[4 + b.reg].sd[0] = static_cast<std::int32_t>(scratch);
        scratch += size;
    }
    for (const auto &[address, bytes] : q.pokes)
        for (std::size_t i = 0; i < bytes.size(); ++i) st8(address + static_cast<std::uint32_t>(i), bytes[i]);
    c.r[29].sd[0] = static_cast<std::int32_t>(sp);
    c.r[31].sd[0] = static_cast<std::int32_t>(kHandlerReturn);
    c.pc = q.address;
    out.before = c;
    out.sp = sp;
    out.scratch = sp + 16u;

    std::vector<std::uint8_t> ram(g_ram, g_ram + kRamSize), spr(g_spr, g_spr + kScratchpadSize);
    const int thread = kernel::current_thread();
    g_result = &out;
    g_abort = Status::ok;
    g_abort_detail.clear();
    g_time = now();
    g_active = true;
    set_budget(c, q.budget);
    const std::int64_t given = c.budget;
    for (;;) {
        if (c.pc == kHandlerReturn) break;
        if ((c.pc & ~kUnwind) == kIdle) {
            // switch_to() found no runnable thread and parked the EE: the call did not return.
            out.status = Status::thread_switch;
            out.detail = "the kernel idled inside the call (no runnable thread)";
            break;
        }
        if (c.pc & kUnwind) {
            if (g_abort != Status::ok) {
                out.status = g_abort;
                out.detail = g_abort_detail;
            } else if (halted()) {
                out.status = Status::halted;
                out.detail = "runtime halted (see the [ee] lines above)";
            } else {
                out.status = Status::thread_switch;
                out.detail = kernel::current_thread() != thread ? "the kernel switched threads inside the call" : "unexpected unwind";
            }
            break;
        }
        if (halted()) {
            out.status = Status::halted;
            out.detail = "runtime halted (see the [ee] lines above)";
            break;
        }
        dispatch(c, 0, 0);
    }
    g_active = false;
    g_result = nullptr;
    out.cycles = given - c.budget;
    out.after = c;

    diff(ram.data(), g_ram, kRamSize, 0, out.ram);
    diff(spr.data(), g_spr, kScratchpadSize, kScratchpadBase, out.spr);
    const std::uint32_t low = sp >= kStackWindow ? sp - kStackWindow : 0;
    const auto frame = [&](const Range &r) { return r.address < sp && r.address >= low; };
    std::copy_if(out.ram.begin(), out.ram.end(), std::back_inserter(out.stack), frame);
    out.ram.erase(std::remove_if(out.ram.begin(), out.ram.end(), frame), out.ram.end());
    return out;
}

bool write(const Result &r, const std::string &path) {
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "{\"harness\":1,\"state_version\":%u,\"frame\":%llu,\"status\":\"%s\",\"detail\":\"%s\",\"hook_ran\":%s,\"sp\":\"0x%08X\",\"scratch\":\"0x%08X\",\"cycles\":%lld,\n",
                 state_version(), static_cast<unsigned long long>(frame_count()), status_name(r.status), r.detail.c_str(), r.hook_ran ? "true" : "false",
                 r.sp, r.scratch, static_cast<long long>(r.cycles));
    std::fprintf(f, "\"before\":"); cpu_json(f, r.before);
    std::fprintf(f, ",\n\"after\":"); cpu_json(f, r.after);
    std::fprintf(f, ",\n"); ranges_json(f, "ram", r.ram);
    std::fprintf(f, ",\n"); ranges_json(f, "spr", r.spr);
    std::fprintf(f, ",\n"); ranges_json(f, "stack", r.stack);
    std::fprintf(f, ",\n\"io\":[");
    for (std::size_t i = 0; i < r.io.size(); ++i)
        std::fprintf(f, "%s{\"at\":\"0x%08X\",\"size\":%u,\"value\":\"0x%016llx\"}", i ? "," : "", r.io[i].address, r.io[i].size, static_cast<unsigned long long>(r.io[i].value));
    std::fprintf(f, "],\n\"syscalls\":[");
    for (std::size_t i = 0; i < r.syscalls.size(); ++i) std::fprintf(f, "%s%u", i ? "," : "", r.syscalls[i]);
    std::fprintf(f, "],\n\"entries\":[");
    for (std::size_t i = 0; i < r.entries.size(); ++i) std::fprintf(f, "%s\"0x%08X\"", i ? "," : "", r.entries[i]);
    std::fprintf(f, "]}\n");
    std::fclose(f);

    // Sidecar: records of kind (1 ram, 2 spr, 3 stack), u32 address, u32 length, bytes as they are now.
    std::FILE *m = std::fopen((path + ".mem").c_str(), "wb");
    if (!m) return false;
    ranges_bytes(m, 1, r.ram, g_ram, 0);
    ranges_bytes(m, 2, r.spr, g_spr, kScratchpadBase);
    ranges_bytes(m, 3, r.stack, g_ram, 0);
    std::fclose(m);
    return true;
}

bool parse_option(const std::string &flag, const char *value, Request &q) {
    static const char *const kGpr[8] = {"--a0", "--a1", "--a2", "--a3", "--t0", "--t1", "--t2", "--t3"};
    for (int i = 0; i < 8; ++i) {
        if (flag == kGpr[i]) { q.gpr[i] = parse_u32(value); return true; }
        if (flag == "--f" + std::to_string(12 + i)) { q.fpr[i] = parse_u32(value); return true; }
    }
    if (flag == "--gp") { q.gp = parse_u32(value); return true; }
    if (flag == "--sp") { q.sp = parse_u32(value); return true; }
    if (flag == "--stack-reserve") { q.stack_reserve = parse_u32(value); return true; }
    if (flag == "--budget") { q.budget = static_cast<std::int32_t>(std::strtol(value, nullptr, 0)); return true; }
    const std::string text = value;
    if (flag == "--poke") {   // --poke ADDR:HEXBYTES
        const auto colon = text.find(':');
        if (colon == std::string::npos) return false;
        q.pokes.emplace_back(parse_u32(text.substr(0, colon).c_str()), parse_hex(text.substr(colon + 1)));
        return true;
    }
    if (flag == "--buf") {    // --buf REG:SIZE[:HEXBYTES]
        const auto first = text.find(':');
        if (first == std::string::npos) return false;
        const auto second = text.find(':', first + 1);
        Buffer b;
        b.reg = reg_index(text.substr(0, first));
        b.size = parse_u32(text.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1).c_str());
        if (second != std::string::npos) b.bytes = parse_hex(text.substr(second + 1));
        if (b.reg < 0 || !b.size) return false;
        q.buffers.push_back(std::move(b));
        return true;
    }
    return false;
}

}
```

8. `system.cpp`: add `#include "ps2/harness.hpp"` with the other `ps2/` includes, then three guards:
   - `now()` becomes
```cpp
std::uint64_t now() {
    if (harness::g_active) return harness::g_time;
    return g_base + static_cast<std::uint64_t>(g_given - g_cpu.budget);
}
```
   - `preempt()` gets a first line:
```cpp
    if (harness::g_active) return harness::preempt(c);
```
   - `request_iop_sync()` (lines 338-340) gets a first line:
```cpp
    if (harness::g_active) return;
```

9. `kernel.cpp`: add `#include "ps2/harness.hpp"`; in `handle_syscall()` directly after `if (interrupt_variant) n = 0u - n;` (line 461) and **before** the custom-syscall table check at lines 462-468:
```cpp
    if (harness::g_active && harness::syscall(c, n, pc, ld32(kSyscallTable + n * 4u) != 0)) return;
```
   (The generated code's `if (c.pc != pc+4) return;` then unwinds to `call()`. The custom-handler test reuses whatever expression lines 462-468 use to find a game-installed handler; `kSyscallTable` is the verifier's name for it and the implementer uses the real identifier. Without this test a custom handler would run through `run_guest()`, whose loop strips `kUnwind` and refills the budget (`kernel.cpp:190-192`), so an abort raised inside it would never reach `call()` and the process would spin. Alternative with the same effect: make `harness::preempt()` call `halt()`.)

10. `memory.cpp`: add `#include "ps2/harness.hpp"`; in `write_slow()`'s `Region::Io` branch, before the `hw::` write:
```cpp
        if (harness::g_active) harness::io_write(t.address, static_cast<std::uint64_t>(v), sizeof(T));
```
    and in `slow_st128()`'s `Region::Io` branch, before `hw::write128`:
```cpp
    else if (t.region == Region::Io) { if (harness::g_active) harness::io_write(t.address, v.d[0], 16); hw::write128(t.address, v); }
```

11. `CMakeLists.txt`: add `runtime/src/harness.cpp` and `runtime/src/hooks.cpp` (Part 3) to the `ps2runtime` source list.

12. `main.cpp`: include `"ps2/harness.hpp"` and `"ps2/hook.hpp"`; before the loop `ps2::harness::Request call; std::string call_out;`; in the loop:
```cpp
        else if (a == "--call" && i + 1 < argc) call.address = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 16));
        else if (a == "--snapshot" && i + 1 < argc) load_path = argv[++i];
        else if (a == "--out" && i + 1 < argc) call_out = argv[++i];
        else if (a == "--trace-calls") call.trace = true;
        else if (a == "--hook" && i + 1 < argc) { if (!ps2::enable_hook(argv[++i])) { std::fprintf(stderr, "unknown hook %s\n", argv[i]); return 2; } }
        else if (a == "--hooks" && i + 1 < argc && std::string(argv[++i]) == "all") ps2::enable_all_hooks();
        else if (i + 1 < argc && ps2::harness::parse_option(a, argv[i + 1], call)) ++i;
        else return usage();
```
    after `register_generated_vu();`: `register_hooks();` (declared `void register_hooks();` next to `register_generated_code()`, provided by the hooks library or by a stub in `hooks.cpp` when `PS2_HOOKS` is unset; see Part 3). After the `load_state` line:
```cpp
    if (call.address) {
        if (load_path.empty()) { std::fprintf(stderr, "--call needs --snapshot\n"); return 2; }
        if (call.trace) ps2::set_entry_trace(true);
        const ps2::harness::Result result = ps2::harness::call(call);
        std::fprintf(stderr, "[call] 0x%08X %s%s%s v0 %016llX v1 %016llX f0 %08X cycles %lld ram %zu spr %zu stack %zu io %zu syscalls %zu%s\n",
                     call.address, ps2::harness::status_name(result.status), result.detail.empty() ? "" : ": ", result.detail.c_str(),
                     static_cast<unsigned long long>(result.after.r[2].d[0]), static_cast<unsigned long long>(result.after.r[3].d[0]), result.after.f[0],
                     static_cast<long long>(result.cycles), result.ram.size(), result.spr.size(), result.stack.size(), result.io.size(), result.syscalls.size(),
                     result.hook_ran ? " (hook)" : "");
        if (!call_out.empty() && !ps2::harness::write(result, call_out)) { std::fprintf(stderr, "cannot write %s\n", call_out.c_str()); return 1; }
        return result.status == ps2::harness::Status::ok ? 0 : 3;
    }
```
    Usage text gains `[--snapshot-every n --snapshot-dir dir] [--call addr --snapshot file [--a0..--a3 --t0..--t3 v] [--f12..--f19 v] [--gp v] [--sp v] [--buf reg:size[:hex]] [--poke addr:hex] [--budget n] [--trace-calls] [--hook name|addr] [--hooks all] [--out file.json]]`.

Exit codes: 0 call ok, 1 cannot load or write, 2 usage, 3 call aborted (status in the JSON and the `[call]` line).

### Part 3: the decompiled-replaces-recompiled switch (`hook.hpp`, `hooks.cpp`, `dispatch.cpp`, `generated.hpp`, `CMakeLists.txt`; 3b: `codegen.cpp`)

13. New `runtime/include/ps2/hook.hpp` (the only header decompiled code needs):
```cpp
#pragma once

#include "cpu.hpp"
#include "fpu.hpp"
#include "memory.hpp"
#include "system.hpp"

#include <cstdint>
#include <string>

namespace ps2 {

// A hook replaces one guest function. It reads its arguments from c (a0-a3, t0-t3 = c.r[4..11],
// f12-f19 = c.f[12..19], further arguments on the stack at c.r[29]), writes its results to c
// (v0 = c.r[2], v1 = c.r[3], f0 = c.f[0]; 32-bit integers sign-extended with set32()), may use
// the scratch registers (v0, v1, a0-a3, t0-t9, f0-f19, facc, fcr31) freely, must leave s0-s7,
// gp, sp, fp, ra (c.r[16..23], 28, 29, 30, 31) and f20-f31 as it found them, and must not touch
// c.pc: enter() sets c.pc = ra afterwards, which is how the recompiled caller sees the return.
using HookFn = void (*)(Cpu &c);

struct Hook {
    std::uint32_t address;
    const char *name;
    HookFn fn;
};

void register_hooks(const Hook *hooks, std::uint32_t count);   // registered but disabled
bool enable_hook(const std::string &name_or_hex_address);
void enable_all_hooks();
void set_entry_trace(bool on);
void set_entry_snapshot(std::uint32_t address, std::uint32_t every, std::uint32_t max, const std::string &dir);

// Defined in runtime/src/memory.cpp and declared in the private header runtime/src/hw.hpp, which
// decompiled code must not include; re-declared here (or move the declaration into memory.hpp).
std::uint32_t physical(std::uint32_t address);

// EE memory as the host sees it. RAM addresses are physical(a) & (kRamSize - 1), so KUSEG
// 0x00xxxxxx, uncached 0x2xxxxxxx and accelerated 0x3xxxxxxx all land in g_ram; the 16 KB
// scratchpad is a separate array. Scalars: ld8/ld16/ld32/ld64/ld128 and st* from memory.hpp.
inline std::uint8_t *host_ram(std::uint32_t a) { return g_ram + (physical(a) & (kRamSize - 1)); }
inline std::uint8_t *host_spr(std::uint32_t a) { return g_spr + (a & (kScratchpadSize - 1)); }

}
```

14. New `runtime/src/hooks.cpp`:
```cpp
#include "ps2/hook.hpp"

#include "kernel.hpp"
#include "ps2/generated.hpp"
#include "ps2/harness.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace ps2 {

bool g_hooks = false;

namespace {

struct Entry {
    const Hook *hook;
    bool enabled;
};

std::unordered_map<std::uint32_t, Entry> g_table;
bool g_trace = false;
std::uint32_t g_snap_address = 0, g_snap_every = 1, g_snap_max = 0, g_snap_seen = 0, g_snap_saved = 0;
std::string g_snap_dir;

void refresh() {
    g_hooks = g_trace || g_snap_address != 0 ||
              std::any_of(g_table.begin(), g_table.end(), [](const auto &kv) { return kv.second.enabled; });
}

}

void register_hooks(const Hook *hooks, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) g_table[hooks[i].address] = {&hooks[i], false};
}

bool enable_hook(const std::string &key) {
    bool found = false;
    for (auto &[address, e] : g_table) {
        if (key == e.hook->name || (key.rfind("0x", 0) == 0 && std::strtoul(key.c_str(), nullptr, 16) == address)) e.enabled = found = true;
    }
    refresh();
    return found;
}

void enable_all_hooks() {
    for (auto &[address, e] : g_table) e.enabled = true;
    refresh();
}

void set_entry_trace(bool on) { g_trace = on; refresh(); }

void set_entry_snapshot(std::uint32_t address, std::uint32_t every, std::uint32_t max, const std::string &dir) {
    g_snap_address = address;
    g_snap_every = every ? every : 1;
    g_snap_max = max;
    g_snap_dir = dir;
    refresh();
}

// Called at every function entry the generated code or dispatch() reaches while g_hooks is set.
// Returns true when a hook served the call: the caller must return at once.
bool enter(Cpu &c, std::uint32_t address) {
    if (g_trace) harness::entered(address, false);
    if (g_snap_address == address && !harness::g_active && !kernel::in_handler() && (g_snap_max == 0 || g_snap_saved < g_snap_max) && g_snap_seen++ % g_snap_every == 0) {
        char name[96];
        std::snprintf(name, sizeof name, "/e%08X_f%06llu_%04u.entry.state", address, static_cast<unsigned long long>(frame_count()), g_snap_saved);
        c.pc = address;   // the generated code does not keep pc current between control transfers
        if (save_state(g_snap_dir + name)) ++g_snap_saved;
    }
    const auto it = g_table.find(address);
    if (it == g_table.end() || !it->second.enabled) return false;
    if (g_trace) harness::entered(address, true);
    it->second.hook->fn(c);
    c.pc = c.r[31].w[0];
    return true;
}

}

#ifndef PS2_HOOKS
void register_hooks() {}
#endif
```
   `harness::entered()` as written records `hook_ran` from the first entry; with `g_trace` off it is never called, so `hook_ran` needs the second `entered(address, true)` call to be unconditional when the entry is the harness's own: change the line to `harness::entered(address, true);` without the `if (g_trace)` and make `entered()` push to `entries` only when `g_trace` is on (pass that in or check a flag). Keep whichever the implementer finds cleaner; the JSON field is informational. The `!kernel::in_handler()` test is sufficient to avoid the `kernel::state()` throw because `g_in_handler` is also set by `run_guest()` (`kernel.cpp:177`), so entries inside a custom syscall handler are skipped too.

15. `generated.hpp`, after `bool preempt(Cpu &c);`:
```cpp
extern bool g_hooks;
bool enter(Cpu &c, std::uint32_t address);
```

16. `dispatch.cpp`, in `dispatch()` after `if (pc == kHandlerReturn || pc == kIdle) return;`:
```cpp
    if (g_hooks && enter(c, pc)) return;
```
   This covers every indirect call (`call()` → `dispatch()`), the trampolined calls past `kMaxDepth`, and the harness's own first `dispatch()`, so `ps2run ... --call ADDR --snapshot S --hook NAME` runs the decompiled function under the same guards and produces the same JSON. It does **not** cover direct `jal` callers (unit calls) or same-unit register jumps and jump tables whose target is an entry (`goto dispatch_entry`); only the Part 3b label check reaches those. (Continuation addresses also pass through `dispatch()`; they are not function starts, so they never match a registered hook; with tracing on they appear in `entries` and the driver ignores addresses that are not registered functions.)

17. The hooks library. `CMakeLists.txt`, after the `ps2game` block:
```cmake
    set(PS2_HOOKS "" CACHE PATH "Folder with decompiled functions that replace recompiled ones (hooks_table.cpp defines register_hooks())")
    if(PS2_HOOKS)
        file(GLOB PS2_HOOK_SOURCES CONFIGURE_DEPENDS "${PS2_HOOKS}/*.cpp")
        add_library(ps2hooks STATIC ${PS2_HOOK_SOURCES})
        target_include_directories(ps2hooks PRIVATE "${PS2_HOOKS}")
        target_link_libraries(ps2hooks PUBLIC ps2runtime)
        target_compile_definitions(ps2runtime PUBLIC PS2_HOOKS)
    endif()
```
    and `target_link_libraries(ps2run PRIVATE ps2game ps2runtime)` (and the `ps2web` line) gain `$<$<BOOL:${PS2_HOOKS}>:ps2hooks>` first. The hooks directory lives in our repo (`src/hooks/`, per the design spec's `src/` and `game/hooks/`), passed as `-DPS2_HOOKS=/mnt/c/.../Burnout3Decomp/src/hooks` at configure time. Its `hooks_table.cpp` is an explicit table (no static-initialiser registration, which a static library would drop):
```cpp
#include "ps2/hook.hpp"
namespace b3 { void pack_fixed16(ps2::Cpu &c); }
namespace {
const ps2::Hook kHooks[] = {
    {0x00389700u, "pack_fixed16", &b3::pack_fixed16},
};
}
void register_hooks() { ps2::register_hooks(kHooks, sizeof kHooks / sizeof kHooks[0]); }
```
    For web builds, `ps2web_*` exports gain nothing; hooks are enabled in `web.cpp` by `enable_all_hooks()` once a hook is validated (the "decompiled replaces recompiled" switch for play).

18. **Part 3b (later, one regeneration): the entry check in generated code.** `codegen.cpp`, `write_unit()`, after `out_ << label(a) << ":\n";` (line 213):
```cpp
                if (p_.functions.count(a)) line(std::format("if (g_hooks) [[unlikely]] {{ if (enter(c, {})) return; }}", pc(a)));
```
    Cost with `g_hooks` false: one global load and a predicted branch per function entry. `p_.functions` holds every `jal` target, seed, symbol and pointer-table target (`analysis.cpp:21-26, 86, 236-238, 278`); functions reached only by `jalr` already pass through `dispatch()`. A function reached only by a `j` tail call and never by `jal` is not in the set and stays recompiled on that path; the harness entry goes through `dispatch()`, so `--call` works for every address; what 3b adds is hooks taking effect for direct callers and same-unit register jumps in live play, callee tracing (`--trace-calls` lists direct callees) and `--snapshot-on-entry`. Caveat: a function whose first instruction is also a backward-branch target (a loop head at entry) would re-run `enter()` on every iteration, and with a hook enabled that replaces the function mid-loop; emit the check only for addresses in `p_.functions` and skip those that are also backward-branch targets, or accept the limitation and let the driver flag such functions. Because `write_if_changed` rewrites only changed units, the regeneration touches all 225 units once. `return` inside the unit is correct for every entry path: `switch` entry, `goto` from a same-unit `j`, and `musttail` from another unit all resume in the caller's `if (c.pc != ret) return;` with `c.pc == ret`.

19. **What decompiled code looks like.** Proposed C++ for the first target, `0x00389700` (`unit_00A2.cpp` entry 100, 18 direct callers, pure: reads `f12`, `a0`, `a1`; writes `v0`; no memory access; the recompiled body is 43 lines). It packs a float as 16-bit sign-magnitude fixed point with `a0` fraction bits (sign-extended byte), bit 15 the sign when `a1` is non-zero:
```cpp
#include "ps2/hook.hpp"

namespace b3 {

// 0x00389700: float -> 16-bit fixed point, `bits` fraction bits, sign-magnitude when signed_format.
void pack_fixed16(ps2::Cpu &c) {
    using namespace ps2;
    const std::uint32_t x = c.f[12];
    const std::uint32_t bits = static_cast<std::uint32_t>(static_cast<std::int8_t>(c.r[4].w[0])) & 31u;
    const bool signed_format = c.r[5].d[0] != 0;
    const bool negative = fp::key(x) < fp::key(0u);
    std::uint32_t whole, frac;
    if (negative) {
        whole = 0u - fp::cvt_w_s(x);                               // |trunc(x)|, computed as the game does: trunc then negate
        frac = fp::sub(x ^ fp::kSign, fp::cvt_s_w(whole));
    } else {
        whole = fp::cvt_w_s(x);
        frac = fp::sub(x, fp::cvt_s_w(whole));
    }
    const std::uint32_t scale = fp::cvt_s_w(1u << bits);
    std::uint32_t v = ((whole << bits) & 0xFFFFu) + (fp::cvt_w_s(fp::mul(frac, scale)) & 0xFFFFu);
    v &= 0xFFFFu;
    if (signed_format) v = negative ? (v | 0x8000u) & 0xFFFFu : v & 0x7FFFu;
    c.r[2].d[0] = v;   // the recompiled code ends with an andi, so v0 is zero-extended, not set32()
}

}
```
    Rules this illustrates: every float operation goes through `fp::` on `uint32_t` bit patterns (host `float` arithmetic rounds to nearest and keeps denormals; `fp::` truncates and flushes, so last-bit differences would otherwise masquerade as logic errors, as the crosswalk warned); `cvt_w_s` truncates toward zero; results are written with the same extension the original leaves (`set32()` for `addu`-style 32-bit results, raw `d[0]` after `andi`/`ori`/`dsll`); the saturating edge (`cvt_w_s` of huge negatives) is kept by mirroring the original's negate-after-truncate order rather than taking `|x|` first. For readability a small value type `struct f32 { std::uint32_t bits; }` with operators mapping to `fp::add/sub/mul` and `fpu_div` can live in our repo next to the hooks (`src/ee/f32.hpp`); MADD stays two roundings because no operator fuses. Second target with memory: `0x00216E40` (`unit_0045.cpp` entry 264, 8 callers) reads `a0+220` and `a0+224` as floats and `f12`, `f13`, returns `f0`; run it with `--buf a0:256:<hex of the struct>` or on entry snapshots.

### Part 4: the validation driver (`tools/difftest.py`, spec only; another session owns `tools/` right now)

- Invocation: `python3 -I tools/difftest.py --ps2run ~/b3/ps2-web-recomp/build/native-burnout3/ps2run --elf ~/b3/ps2-web-recomp/games/burnout3/disc/SLUS_210.50 --function 0x00389700 --hook pack_fixed16 --snapshots ~/b3/out/snaps --out ~/b3/out/diff/pack_fixed16 [--args "--f12 f:1.5 --a0 8 --a1 1"] [--args-file cases.txt] [--args snapshot] [--returns v0] [--compare-fpr f0] [-j 8] [--disc ISO] [--env PS2_GS=off]`.
- For each snapshot `S` (every `*.state` in the directory, and for `--args snapshot` only `*.entry.state` files whose address matches), and each argument vector (one line of `cases.txt`, or the single `--args`, or none for entry snapshots), run two processes with `PS2_GPU_FALLBACK=1 PS2_GS=off`: `ps2run ELF --snapshot S --call ADDR <args> --out OUT/recomp/<case>.json` and the same plus `--hook NAME` to `OUT/decomp/<case>.json`. `timeout 120` each; a non-zero exit with status `hung`/`forbidden_syscall`/`thread_switch`/`halted` on the recompiled side marks the case `SKIP` (the function is not harness material on that snapshot), on the decompiled side only `FAIL`.
- Compare, in this order, stopping at the first difference per case: `status`; return registers (`after.r[2]` and `after.r[3]` as full 64-bit, `after.f[0]`, `after.f[1]`; the set is `--returns`); callee-saved registers equal to `before` on both sides (`r16-r23, r28, r29, r30, r31`, `f20-f31`) as an ABI check; `ram` and `spr` range lists identical (address, length, fnv) with the bytes from `.mem` compared when the hashes differ, reporting the first differing offset and 16 bytes of each; `io` lists identical; `syscalls` identical. `stack` ranges are ignored (the decompiled side never writes a guest frame). `fcr31` and `facc` are compared only with `--strict`.
- Output: one line per case, `s003600 PASS cycles 312`, `s003600 FAIL v0 0000000000003f00 != 0000000000003e80`, `s003600 FAIL ram 0x004E7280+16 differs at +4: 00 00 80 3f .. != 00 00 00 40 ..`, `s003600 SKIP recomp forbidden_syscall: syscall 0x44 at 0x0010F208`; a summary line `pack_fixed16: 18 pass, 0 fail, 2 skip`; `OUT/summary.json` with the per-case verdicts and the two JSON paths. Exit code 0 all pass (skips allowed), 1 any fail, 2 harness or setup error (missing snapshot, ps2run exit 1 or 2, JSON missing). The verdict for accepting a function (design spec: "a function is accepted only after passing on a recorded corpus from real play") is `pass >= 1 and fail == 0` over the corpus, recorded in `docs/re/` by the implementer with the corpus name and fork commit.

### Part 5: gathering snapshots during a scripted run

- Periodic: the main-menu route from the task, extended to the race with the route in `docs/re/menu-route.md`, with `--snapshot-every 300 --snapshot-dir ~/b3/out/snaps/<run>`: `PS2_GPU_FALLBACK=1 ps2run SLUS_210.50 --disc games/burnout3/disc.iso --frames 3600 --press 1900:start:10 --press 2300:start:10 --press 2500:down:6 --press 2600:cross:10 --press 2800:cross:10 --press 3000:cross:10 --press 3150:up:6 --press 3250:cross:10 --snapshot-every 300 --snapshot-dir ~/b3/out/snaps/menu01`. Twelve states of about 40 MB each (480 MB); a race run to frame 12000 at every 300 frames is 1.6 GB, so keep corpora under `~/b3/out/snaps/` and prune. Each state is taken at the `run()` loop top after a vblank unwind, between instructions of whichever thread was running; `g_cpu.pc` may carry bit 0, which `call()` masks.
- On entry (needs Part 3b for directly called functions): `--snapshot-on-entry ADDR[:EVERY[:MAX]]` → `ps2::set_entry_snapshot()`; `enter()` saves `DIR/eADDR_fFRAME_NNNN.entry.state` with the real arguments in `g_cpu`, skipping entries inside interrupt handlers and guest calls (`kernel::state()` would throw) and during harness calls. These files are for `--call --args snapshot` only: resuming one with `run()` would re-enter the function from its start with the frame counter and events of the saving run, so the `.entry.state` suffix marks them and `main.cpp` refuses `--load-state` on that suffix without `--call`.
- `--snapshot-on-entry` makes `g_hooks` true for the whole run, so every function entry does a hash lookup: slower runs, acceptable for corpus building.

### Behaviour notes

- **Isolation.** During a call nothing but the EE runs: `preempt()` aborts, so no vblank, no presses, no `sync_iop()`/`iop::run()`, no SPU2 tick (an IOP kernel job), no DMAC/INTC handler (those run only from `kernel::service()` in `run()`), no reschedule, and no custom syscall handler (forbidden by the guard in step 9). A function that waits for the IOP (SIF RPC polling, `WaitSema` on a reply) ends as `hung` or `forbidden_syscall`; that is the correct verdict: such functions are not harness material and stay recompiled.
- **Entry addresses.** `has_code()` and `dispatch()` accept every entry, and entries are not only function starts: call return addresses (`analysis.cpp:87, 90`), syscall returns (`:74`), backward branch and jump targets (`:103`), seeds and symbols (`:238`) and pointer-table targets (`:266-277`). So `--call` on a loop head inside a function is accepted and runs from there; `no_code` is reported only for a non-entry address (`0x00389704` is one: `entries_00A2[1473] == 0`). The driver should take its addresses from the symbol database, not from guesses.
- **Time.** `now()` is frozen at the snapshot's time during a call, so COP0 Count and the T0-T3 counters read the same on both sides. `cycles` in the JSON is the recompiled budget consumed; a hook consumes none.
- **Hardware stores.** Writes through `write_slow()` to I/O are performed (the GIF/VIF/DMAC/GS code runs inline) and logged; a function that kicks DMA shows it in `io` and the driver compares the list. GS memory and IOP RAM are not diffed.
- **What a thread switch does to the process.** A `SignalSema` that wakes a higher-priority thread makes `switch_to()` load another context into `g_cpu` and mutates the kernel's thread table. `call()` reports `thread_switch` (also when the kernel parks the EE at `kIdle`); the process must not run another call afterwards. One call per process is the rule in step one; batching (`--calls FILE`) would reload the state between calls from the in-memory buffer.
- **The frame.** The stack reserve is carved below the snapshot thread's `sp` (or `0x80000 - reserve`, the kernel stack top `run_guest` uses, when the EE was idle or `sp` is implausible). Buffers are placed in `[sp + 16, top)`, so writes into them are results; writes below `sp` down to 64 KB are the frame and are reported in `stack`.
- **Register conventions observed in the generated code** (to be confirmed per function; the ELF is CodeWarrior-built): integer arguments in `a0-a3` then `t0-t3`, float arguments in `f12-f19`, results in `v0`/`v1`/`f0`. 32-bit results are sign-extended to 64 bits by `set32()` (`addu`, `lw`, shifts) but zero-extended after `andi`/`ori`; the driver compares 64 bits, so hooks must reproduce the extension.
- **Hooks and ps2web.** `enter()` has no threading concern: the web build calls generated code from the emulation worker only.
- **No new provenance row.** The method is reB3's differential validation (already credited in `docs/research/PROVENANCE.md`, knowledge only); Unicorn is not used; all code above is written fresh.

### State-version implications

- Parts 1-5 do not change `machine_state()`: `kStateVersion` stays 4 and existing states (`~/b3/out/ee007/s590.state` style) keep loading. The harness JSON records `state_version` and `frame` so a corpus can be matched to the build that made it.
- A corpus is tied to a fork build: any later gap fix that bumps `kStateVersion` (as gap 007 did, 3 → 4) makes every snapshot unloadable, and the driver reports exit 2 (`state file from another version`). Regenerating is one scripted run with `--snapshot-every`; keep the run command next to the corpus (`~/b3/out/snaps/<run>/CMD.txt`). The recompiled-versus-decompiled comparison itself is within one binary, so it is immune to version changes.
- `Cpu` is saved as a POD including `budget` and `base`; `call()` overrides `budget` with `set_budget()` and Burnout never sets `base` (no SNR2 modules: `table.cpp` registers units only). If `Cpu` ever gains a field, that is a `kStateVersion` bump by the existing rule.
- Entry snapshots (`.entry.state`) are version-4 files with the EE stopped mid-function; nothing else about them differs.

### Risks

- **Threads**: none in `ps2run` (verified: `host/main.cpp` starts neither the render nor the VU thread). In the web build the harness is not offered; if it ever is, `gs::state()`/`sync_vu_thread()` already fence the workers, but a hook running on the emulation worker while the VU worker shadows VIF1 is the same situation as recompiled code today.
- **The VU worker and VU0 micro**: `vcallms` runs `vu::run()` inline on the EE thread; functions that start a VU0 microprogram and read its results are recompiled-only material (a hook cannot cheaply reproduce the microprogram). VU0 macro state lives in `c.vu0` and is compared through `vf`/`vi`/`acc` in the JSON when `--compare-vu` is given.
- **The GS queue and WebGPU**: with the queue inactive the GS runs inline; the harness runs with `PS2_GS=off` so no adapter is created per call. Unverified: whether a state saved under the WebGPU renderer loads under `PS2_GS=off` (the `gs` section is `render_state()` in both cases, `gs.cpp:1024-1029`; a mismatch would surface as `state section mismatch at gif` and is a verification item below).
- **The IOP**: never advanced inside a call; `sif::set_dma()` (`SifSetDma` syscall) still copies into IOP RAM synchronously, which is a side effect outside the diff. Functions doing SIF work are skipped by the driver.
- **The audio tick**: an IOP kernel job (`spu2.cpp:317,324`), so it cannot run during a call. The SPU2 register writes from the EE side go through I/O and are logged.
- **Custom syscall handlers**: forbidden during a call by the step-9 guard, because a handler runs through `run_guest()`, whose loop strips `kUnwind` and calls `set_budget(c, 20000)` when the budget is exhausted (`kernel.cpp:190-192`), which would both reset the harness budget and swallow an abort. Burnout's use of `SetSyscall` is unknown; the syscall log of the first runs shows it.
- **Entry check cost (3b)**: one load and branch per function entry in all builds, including ps2web; expected unmeasurable, but check `PS2_TIME` before and after the regeneration.
- **Trace noise**: `dispatch()`-driven `enter()` calls fire on continuations too; the driver filters `entries` against the hook table and, with 3b, the function set.
- **Snapshot size**: 40 MB each; disk under `~/b3/out/` only.
- **ABI**: hooks must preserve callee-saved registers; the driver's `before == after` check on `s0-s7`, `gp`, `sp`, `fp`, `ra`, `f20-f31` catches a hook that clobbers them, and also catches a recompiled function that is not a plain leaf (so a difference there is reported, not assumed).

## Verification (for the implementer)

1. `ninja -C /home/mark/b3/ps2-web-recomp/build/native-burnout3 ps2run` builds with no new warnings (`-Wall -Wextra` on `ps2runtime`).
2. Snapshots: the main-menu route with `--snapshot-every 300 --snapshot-dir ~/b3/out/<name>/snaps`, `timeout 600`, 300 MB watcher; expected `[state] saved frame 300 to .../s000300.state (…KB)` lines at 300, 600, …, 3600, twelve files of about 40 MB, and the run otherwise identical to run 010 (same primitive count).
3. Load check under the software renderer: `PS2_GPU_FALLBACK=1 PS2_GS=off ps2run SLUS_210.50 --disc … --load-state ~/b3/out/<name>/snaps/s003300.state --frames 3400` must print `[state] loaded frame 3300` and reach `frame limit reached` with no `state section mismatch`.
4. First call, recompiled: `PS2_GPU_FALLBACK=1 PS2_GS=off ps2run SLUS_210.50 --snapshot …/s003300.state --call 0x00389700 --f12 f:1.5 --a0 8 --a1 1 --out ~/b3/out/<name>/c1.json`. Expected `[call] 0x00389700 ok v0 0000000000000180 … cycles 4x ram 0 spr 0 stack 0 io 0 syscalls 0` and exit 0 (1.5 with 8 fraction bits is 0x180; `--a1 0` gives the same, `--f12 f:-1.5` gives 0x8180). A `--call` on a non-entry address (e.g. `0x00389704`) must give `no_code` and exit 3.
5. Guards: `--call 0x001130B0 --a0 0x1000` (the libkernel heap wrapper, which calls into the SIF RPC client at `0x0010F1E8`) must end as `forbidden_syscall` or `hung`, never run past the budget, and never print `[frame]`, `[sys]`-driven thread switches or IOP lines; `--budget 1000` on any looping function must give `hung` quickly. If the syscall log shows a game-installed handler, the call must end as `forbidden_syscall` with the `custom-handler` detail.
6. Hook A/B: configure with `-DPS2_HOOKS=<repo>/src/hooks` holding `hooks_table.cpp` and `pack_fixed16.cpp`; the same `--call` with `--hook pack_fixed16` must print `(hook)` and the same `v0`; then `tools/difftest.py` over the twelve snapshots and a `cases.txt` grid (values 0, ±0.5, ±1.5, ±1000.25, ±70000.0, bits 0/4/8/12/15, signed 0/1) must report all `PASS` except the saturation cases, which are the intended demonstration of the harness catching an ordering mistake if the hook is written with `|x|` first.
7. `git -C /home/mark/b3/ps2-web-recomp status --short` shows only the files named in this Fix (Part 3b adds `recomp/codegen.cpp` and regenerated `generated/burnout3/unit_*.cpp`, which are outside the repo tree and never committed to ours).
8. `PS2_TIME=1000,500` on a 3600-frame run before and after Part 3b: ms per frame unchanged within noise.

## Verifier corrections applied (2026-10-12)

The verifier confirmed the five fork facts the design rests on (single-threaded `ps2run`; direct calls bypass `dispatch()`; `preempt()` is the only scheduler re-entry in generated code; the return convention and the `run_guest` loop; the save-state contents and timing) and changed the text above in these places:

1. `request_iop_sync()` is `system.cpp:338-340`, not 338-341.
2. The `vblank()` line that sets `g_state_request` is `system.cpp:140`, not 141 (Part 1 step 2 and the save-state paragraph).
3. `physical()` is declared in the private `runtime/src/hw.hpp:19`, not in `memory.hpp`; `hook.hpp` now re-declares it (or the declaration moves into `memory.hpp`).
4. `has_code()`/`dispatch()` accept every entry, not only function starts; `--call` on a loop head runs from there (behaviour note "Entry addresses").
5. `gs::init()` is `gs.cpp:371-381`, `render_reset()` is `gs.cpp:383` with the `init()` call at 384.
6. `kernel::state()` throws for `g_in_handler || !g_guest_calls.empty()`; the `--snapshot-on-entry` check is sufficient because `run_guest()` also sets `g_in_handler`.
7. The harness syscall guard runs before the custom-syscall table check at `kernel.cpp:462-468`, so it now also forbids any syscall with a game-installed handler (refuted item: guarding `preempt()`, syscalls and `request_iop_sync()` alone did not isolate a call, because `run_guest()`'s loop would mask the abort and spin).
8. Part 3b: a loop head at a function entry would re-run `enter()` every iteration; the check is emitted only for `p_.functions` and should skip backward-branch targets or the driver must flag such functions.
9. `kIdle` (`0x1010`) is an early return in `dispatch()` and is set with `kUnwind` by `switch_to()` when no thread is ready; `call()` now treats it like a thread switch.
10. Wording: "only indirect calls reach `dispatch()`" was too strong; the exact lists of what does and does not reach it are in the first section, and the `dispatch()`-only hook check is stated to miss same-unit register jumps and jump tables as well as direct `jal`.

## Implementer corrections applied (2026-10-12)

Made while applying Parts 1, 2, 3a and 4 on top of fork commit `3e42c71`. Where the applied code differs from the proposed code above, the applied code is what this section describes. Outcomes are in `docs/re/harness-log-001.md`.

1. **The state due on the last frame.** Part 1 step 3 as written saved eleven states on a 3600-frame run: the vblank of frame 3600 sets both the state request and the frame-limit halt, and the loop in `run()` exits before honouring the request. The block is now a function, `save_requested_state()`, called at the top of the loop and once more after the loop. `--save-state` on the last frame had the same hole and is fixed by the same change.
2. **The custom-handler test (step 9).** The applied test is the one `handle_syscall()` itself uses: `n < 0x100`, `ld32(kSyscallTable + n * 4)` non-zero, and `has_code(handler & 0x1FFFFFFF)`. `kSyscallTable` is the real identifier (`0x00001800`). Syscall numbers of `0x100` and above do not read past the table.
3. **`hook_ran`.** `harness::entered(address, by_hook)` is split into `entered(address)`, called only when entries are traced, and `hooked(address)`, called whenever a hook serves an entry. `hooked()` sets `hook_ran` only for the harness's own entry: the address matches and `ra` is `kHandlerReturn`.
4. **`call()` checks.** A stack top outside EE RAM (possible with an explicit `--sp`) ends as `bad_request` ("the stack top is outside EE RAM") before anything is written to `g_ram`, and the buffer-fit test also rejects a size that would wrap.
5. **`main.cpp` ordering.** `--hook` names are collected in the option loop and enabled after `register_hooks()`; as written they were looked up before the table was registered, so every name was unknown. `--snapshot-every` without `--snapshot-dir` and `--call` without `--snapshot` exit 2 before boot. The usage text also lists `--stack-reserve`.
6. **`enable_hook()`** accepts a `0X` prefix as well as `0x`.
7. **Hook shape.** The demo hook is a pure function `b3::pack_fixed16(std::uint32_t x, std::int8_t fraction_bits, bool signed_format)` returning `std::uint16_t`, plus the shim `b3::hook_pack_fixed16(ps2::Cpu &)` that the table registers under the name `pack_fixed16`.
8. **Driver additions (Part 4).** A case fails when the decompiled side reports `hook_ran` false (otherwise recompiled would be compared with recompiled and pass) or when the recompiled side reports a hook. A timeout is the status `timeout`: SKIP on the recompiled side, FAIL on the decompiled side. The hooked call is not made when the recompiled side is skipped. `summary.json` has an `accepted` field (`pass >= 1 and fail == 0`). `--args` takes a vector that starts with dashes. Tests are `unittest` style, as the other tests in `tests/`.
9. **Not applied.** The `--snapshot-on-entry` flag and the refusal of `--load-state` on `.entry.state` files (Part 5, useful only with Part 3b). `set_entry_snapshot()` exists in `hooks.cpp` and nothing calls it.

Two further changes, made on review of the first report:

10. **Hooks library (step 17).** The source glob is `file(GLOB_RECURSE ... "${PS2_HOOKS}/*.cpp")`, so `src/hooks/<subsystem>/` is compiled, and the parent of `PS2_HOOKS` is an include directory as well, so sources can include `"hooks/<subsystem>/<name>.hpp"`. Turning `PS2_HOOKS` on recompiles every generated unit once, because the definition is public on `ps2runtime`.
11. **Verification grid (step 6).** ±70000.0 does not saturate the float-to-integer conversion, so the grid as written cannot show the ordering mistake; ±3e9 was added. Cycle counts for `0x00389700` are 28 to 38, not "4x".
