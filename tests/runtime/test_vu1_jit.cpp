// The fork's VU1 JIT (runtime/src/vu_jit.cpp), which turns each VU1 microprogram into WebAssembly in the page,
// against the arithmetic of the fast paths: the C++ helpers of ps2/vu_ops.hpp and ps2/fpu.hpp, which the fast
// interpreter (runtime/src/vu_micro.cpp) computes with. Every upper instruction, under every lane mask, with
// ordinary numbers and the edge classes, runs as a made-up program through vu::run(1, ...), and what it leaves
// in the registers is compared with what the helpers give. Made-up programs with a register jump check when
// a Q result arrives.
//
// The JIT exists only in WebAssembly. Natively (tests/runtime/run.sh) the same programs run through the fast
// interpreter, which is checked against the same helpers. tests/wasm/run.sh builds this test for the page and
// runs it under node, where every program goes through the JIT, and its hash line must be the native build's:
// the JIT leaves the interpreter's state, bit for bit, after every call. The JIT emits shared-memory
// WebAssembly, so only the page's (threaded) build can run it.
//
// usage: test_vu1_jit
//
// fork-source: runtime/src/vu_micro.cpp
// fork-source: runtime/src/vu_jit.cpp
// fork-source: runtime/src/vu_jit_web.cpp
// needs-clang
// wasm-builds: page
// wasm-link: -sALLOW_TABLE_GROWTH=1 -sDECLARE_ASM_MODULE_EXPORTS=0
// (The JIT's module finds its helpers by their names among the program's exports, and adds its function to
// the table; the page keeps the names because it links with JSPI.)

#include "check.hpp"

#include "hw.hpp"
#include "vu_jit_web.hpp"
#include "vu_micro.hpp"
#include "gs/gif.hpp"
#include "gs/queue.hpp"

#include "ps2/fpu.hpp"
#include "ps2/system.hpp"
#include "ps2/vu.hpp"
#include "ps2/vu_ops.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

}

// The few things vu_micro.cpp takes from the rest of the runtime.
namespace ps2 {

Cpu g_cpu;
Options g_options;
std::uint8_t g_vu0_code[4096], g_vu0_mem[4096], g_vu1_code[16384], g_vu1_mem[16384];

namespace gif {
void transfer(int, const std::uint8_t *, std::uint32_t) {}
}

namespace gs::queue {
Role g_role = Role::Emulation;
Ring g_ring;
Role &role() { return g_role; }
Ring &vu() { return g_ring; }
bool vu_active() { return false; }
void Ring::post(Type, std::uint32_t, std::uint32_t, const void *, std::uint32_t) {}
}

}

namespace {

namespace vu = ps2::vu;
namespace vuops = ps2::vuops;
namespace fp = ps2::fp;

ps2::Vu0Regs &r = vu::regs(1);

struct Rng {
    u64 s;
    u64 next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    u32 below(u32 n) { return static_cast<u32>((next() >> 20) % n); }
    u32 bits() { return static_cast<u32>(next() >> 16); }
    bool chance(u32 per_thousand) { return below(1000) < per_thousand; }
};

// ---- instruction words from their fields -------------------------------------------------------------

constexpr u32 kEnd = 0x40000000u;
constexpr int kAcc = 32;

u32 up1(u32 funct, u32 dest, u32 ft, u32 fs, u32 fd) { return (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | funct; }
u32 up2(u32 index, u32 dest, u32 ft, u32 fs) { return (dest << 21) | (ft << 16) | (fs << 11) | ((index >> 2) << 6) | 0x3Cu | (index & 3u); }
u32 lom(u32 opcode, u32 dest, u32 t, u32 s, u32 imm11) { return (opcode << 25) | (dest << 21) | (t << 16) | (s << 11) | (imm11 & 0x7FFu); }
u32 lo2(u32 index, u32 dest, u32 t, u32 s) { return 0x80000000u | (dest << 21) | (t << 16) | (s << 11) | ((index >> 2) << 6) | 0x3Cu | (index & 3u); }
u32 lo12(u32 opcode, u32 t, u32 imm12) { return lom(opcode, (imm12 >> 11) & 1u, t, 0, imm12); }
// A branch at slot `from` to slot `to`: the offset counts from the instruction after the branch.
u32 branch(u32 opcode, u32 t, u32 s, int from, int to) { return lom(opcode, 0, t, s, static_cast<u32>(to - from - 1)); }

enum : u32 { FSAND = 0x16, BAL = 0x21, JR = 0x24 };
enum : u32 { MOVE = 0x30, DIV = 0x38 };
enum : u32 { MULq = 0x1C, NOP = 0x2F };

const u32 kUpperNop = up2(NOP, 0, 0, 0), kLowerNop = lo2(MOVE, 0, 0, 0);

void put(u32 slot, u32 lower, u32 upper) {
    std::memcpy(ps2::g_vu1_code + (slot & 2047u) * 8, &lower, 4);
    std::memcpy(ps2::g_vu1_code + (slot & 2047u) * 8 + 4, &upper, 4);
}

void clear_code() {
    for (u32 slot = 0; slot < 2048; ++slot) put(slot, kLowerNop, kUpperNop | kEnd);
}

// ---- register contents -------------------------------------------------------------------------------

u32 bits(float f) { return std::bit_cast<u32>(f); }

// Ordinary numbers: magnitudes around one, some zeros and ones, and integers for ITOF.
u32 typical(Rng &g) {
    const u32 pick = g.below(100);
    if (pick < 10) return g.bits() & 0x80000000u;
    if (pick < 15) return fp::kOne;
    if (pick < 22) return g.bits() >> g.below(31);
    return (g.bits() & 0x807FFFFFu) | ((117u + g.below(21)) << 23);
}

u32 g_recent[8];

// The classes arithmetic gets wrong first: both ends of the range, the top binade (numbers on a VU, not on the
// host), denormals, values next to an earlier one (cancellation and ties), and pairs whose host sum or partial
// sum overflows.
u32 edge(Rng &g) {
    u32 v;
    switch (g.below(13)) {
    case 0: v = g.bits(); break;
    case 1: v = g.bits() & 0x807FFFFFu; break;
    case 2: v = g.bits() | 0x7F800000u; break;
    case 3: v = (g.bits() & 0x80000000u) | (g.chance(500) ? 0x7FFFFFFFu : 0x7F7FFFFFu); break;
    case 4: v = (g.bits() & 0x807FFFFFu) | ((1u + g.below(3)) << 23); break;
    case 5: v = (g.bits() & 0x807FFFFFu) | ((228u + g.below(27)) << 23); break;
    case 6: v = g.bits() & 0x80000000u; break;
    case 7: v = (g.bits() & 0x80000000u) | (g.below(256) << 23) | (g.chance(500) ? 0u : 0x7FFFFFu); break;
    case 8:
        v = g_recent[g.below(8)];
        if (g.chance(500)) v ^= 0x80000000u;
        v += g.below(5) - 2u;
        if (g.chance(300)) v = (v & 0x807FFFFFu) | ((((v >> 23) + g.below(60) - 30u) & 0xFFu) << 23);
        break;
    case 9: v = (g.bits() & 0x80000000u) | ((150u + g.below(10)) << 23) | (g.bits() & 0x7FFFFFu); break;   // integers near 2^24..2^32 as floats
    default: v = typical(g); break;
    }
    g_recent[g.below(8)] = v;
    return v;
}

void fresh_registers(Rng &g, bool edges) {
    const auto value = [&]() { return edges ? edge(g) : typical(g); };
    for (int i = 1; i < 32; ++i)
        for (int k = 0; k < 4; ++k) r.vf[i].w[k] = value();
    r.vf[0].w[0] = r.vf[0].w[1] = r.vf[0].w[2] = 0;
    r.vf[0].w[3] = fp::kOne;
    for (int k = 0; k < 4; ++k) r.acc.w[k] = value();
    for (int i = 1; i < 16; ++i) r.vi[i] = static_cast<std::uint16_t>(g.below(256));
    r.vi[0] = 0;
    r.q = value();
    r.p = value();
    r.i = value();
    r.r = g.bits() & 0x7FFFFFu;
    r.mac = g.bits() & 0xFFFFu;
    r.status = g.bits() & 0xFFFu;
    r.clip = g.bits() & 0xFFFFFFu;
}

// ---- the fast paths' arithmetic, one upper instruction -----------------------------------------------

// What the fast interpreter does with an upper instruction (vu_micro.cpp, fast_upper and commit), written
// with the helpers it uses: the target lanes the instruction writes, MAC and status when it sets flags and
// flags are read in the program, and the clip flags.
void reference_upper(ps2::Vu0Regs &v, u32 hi, bool flags_read) {
    const u32 dest = (hi >> 21) & 15u, ft = (hi >> 16) & 31u, fs = (hi >> 11) & 31u, fd = (hi >> 6) & 31u, bc = hi & 3u;
    const ps2::Reg128 s = v.vf[fs], t = v.vf[ft], acc = v.acc;
    const u32 funct = hi & 63u;
    const u32 index = funct < 0x3C ? funct : ((hi >> 6) & 31u) * 4u + (funct & 3u);
    const bool second = funct >= 0x3C;
    u32 out[4] = {};
    int target = second ? kAcc : static_cast<int>(fd);
    bool flags = true;
    u32 lanes = dest;
    const auto each = [&](auto f) {
        for (int i = 0; i < 4; ++i) out[i] = f(i);
    };
    const u32 b_bc = t.w[bc];
    if (second && index >= 0x10 && index <= 0x17) {
        static const int kFrac[4] = {0, 4, 12, 15};
        const int frac = kFrac[index & 3u];
        target = static_cast<int>(ft);
        flags = false;
        if (index < 0x14) each([&](int i) { return fp::from_fixed(static_cast<std::int32_t>(s.w[i]), frac); });
        else each([&](int i) { return fp::to_fixed(s.w[i], frac); });
    } else if (second && index == 0x1D) {
        target = static_cast<int>(ft);
        flags = false;
        each([&](int i) { return s.w[i] & fp::kAbs; });
    } else if (second && index == 0x1F) {
        v.clip = vuops::clip_value(v, s, t.w[3]);
        return;
    } else if (second && index == 0x2F) {
        return;
    } else if (index == 0x2E) {
        lanes = 0xE;
        out[0] = vuops::mul(s.w[1], t.w[2]);
        out[1] = vuops::mul(s.w[2], t.w[0]);
        out[2] = vuops::mul(s.w[0], t.w[1]);
        if (!second) {
            out[0] = vuops::msub(acc.w[0], s.w[1], t.w[2]);
            out[1] = vuops::msub(acc.w[1], s.w[2], t.w[0]);
            out[2] = vuops::msub(acc.w[2], s.w[0], t.w[1]);
        }
    } else {
        u32 b[4];
        for (int i = 0; i < 4; ++i) b[i] = index >= 0x28 ? t.w[i] : index < 0x1C ? b_bc : (index == 0x1C || index == 0x20 || index == 0x21 || index == 0x24 || index == 0x25) ? v.q : v.i;
        static const u32 kGroups[7] = {1, 2, 3, 4, 6, 7, 5};
        u32 op = 0;   // 1 add, 2 sub, 3 madd, 4 msub, 5 mul, 6 max, 7 min
        if (index < 0x1C) op = kGroups[index >> 2];
        else switch (index) {
            case 0x1C: case 0x1E: case 0x2A: op = 5; break;
            case 0x1D: case 0x2B: op = 6; break;
            case 0x1F: case 0x2F: op = 7; break;
            case 0x20: case 0x22: case 0x28: op = 1; break;
            case 0x21: case 0x23: case 0x29: op = 3; break;
            case 0x24: case 0x26: case 0x2C: op = 2; break;
            case 0x25: case 0x27: case 0x2D: op = 4; break;
            default: return;   // nothing assigned
            }
        if (second && (op == 6 || op == 7)) return;   // MAX and MINI have no ACC form: those codes are unassigned
        flags = op <= 5;
        each([&](int i) {
            switch (op) {
            case 1: return vuops::add(s.w[i], b[i]);
            case 2: return vuops::sub(s.w[i], b[i]);
            case 3: return vuops::madd(acc.w[i], s.w[i], b[i]);
            case 4: return vuops::msub(acc.w[i], s.w[i], b[i]);
            case 5: return vuops::mul(s.w[i], b[i]);
            case 6: return vuops::max(s.w[i], b[i]);
            default: return vuops::min(s.w[i], b[i]);
            }
        });
    }
    if (target > 0) {
        ps2::Reg128 &dst = target == kAcc ? v.acc : v.vf[target];
        for (int i = 0; i < 4; ++i)
            if (lanes & (8u >> i)) dst.w[i] = out[i];
    }
    if (flags && flags_read) {
        ps2::Reg128 value;
        for (int i = 0; i < 4; ++i) value.w[i] = out[i];
        vuops::flags(v, value, lanes);
    }
}

// ---- calls -------------------------------------------------------------------------------------------

constexpr std::size_t kRegBytes = offsetof(ps2::Vu0Regs, tpc) + sizeof(u32);

u64 g_hash = 0xCBF29CE484222325ull;
u64 g_calls = 0;

void hash_state() {
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(&r);
    for (std::size_t i = 0; i < kRegBytes; ++i) g_hash = (g_hash ^ bytes[i]) * 0x100000001B3ull;
}

void call(u32 slot) {
    vu::run(1, slot * 8);
    ++g_calls;
    hash_state();
}

const char *g_case = "";
int g_reported = 0;   // failures printed in the current case: the first 20 of each

void start_case(const char *name) {
    g_case = name;
    g_reported = 0;
}

void report(u32 hi, const char *what, int lane, u32 got, u32 want, const ps2::Vu0Regs &before) {
    ++check::g_failures;
    if (++g_reported > 20) return;
    const u32 fs = (hi >> 11) & 31u, ft = (hi >> 16) & 31u;
    std::printf("%s: upper %08X, %s lane %d: %08X, the helpers give %08X (fs %08X %08X %08X %08X, ft %08X %08X %08X %08X, acc %08X %08X %08X %08X, q %08X, i %08X)\n", g_case, hi, what, lane, got,
                want, before.vf[fs].w[0], before.vf[fs].w[1], before.vf[fs].w[2], before.vf[fs].w[3], before.vf[ft].w[0], before.vf[ft].w[1], before.vf[ft].w[2], before.vf[ft].w[3],
                before.acc.w[0], before.acc.w[1], before.acc.w[2], before.acc.w[3], before.q, before.i);
}

// One upper instruction at slot 0, the end at slot 1, with an FSAND there when flags are read, which makes
// the fast interpreter and the JIT compute MAC and status.
void run_upper(Rng &g, u32 hi, bool flags_read, bool edges) {
    const ps2::Vu0Regs before = (fresh_registers(g, edges), r);
    ps2::Vu0Regs want = before;
    reference_upper(want, hi, flags_read);
    call(0);
    ++check::g_checks;
    for (int i = 0; i < 32; ++i)
        for (int k = 0; k < 4; ++k)
            if (r.vf[i].w[k] != want.vf[i].w[k]) {
                static char name[8];
                std::snprintf(name, sizeof name, "vf%d", i);
                report(hi, name, k, r.vf[i].w[k], want.vf[i].w[k], before);
            }
    for (int k = 0; k < 4; ++k)
        if (r.acc.w[k] != want.acc.w[k]) report(hi, "acc", k, r.acc.w[k], want.acc.w[k], before);
    if (r.clip != want.clip) report(hi, "clip", 0, r.clip, want.clip, before);
    if (flags_read && r.mac != want.mac) report(hi, "mac", 0, r.mac, want.mac, before);
    if (flags_read && r.status != want.status) report(hi, "status", 0, r.status, want.status, before);
}

// Every assigned code of both upper tables, under all 15 lane masks that write something, with the registers
// picked several ways (apart, the result into vf0, vf0 as an operand, operands that coincide, the result into
// an operand), with flags read and not, each program called with ordinary numbers and with edge values.
void every_upper_against_the_helpers(Rng &g) {
    start_case("every upper");
    for (u32 table = 1; table <= 2; ++table)
        for (u32 code = 0; code < (table == 1 ? 0x30u : 0x30u); ++code) {
            if (table == 2 && (code == 0x2B || code == 0x2F)) continue;   // unassigned, and NOP
            for (u32 dest = 1; dest < 16; ++dest)
                for (u32 shape = 0; shape < 6; ++shape) {
                    u32 ft = 1 + g.below(31), fs = 1 + g.below(31), fd = 1 + g.below(31);
                    if (shape == 1) (table == 1 ? fd : ft) = 0;
                    if (shape == 2) ft = 0;
                    if (shape == 3) fs = 0;
                    if (shape == 4) ft = fs;
                    if (shape == 5) fd = fs;
                    const u32 hi = table == 1 ? up1(code, dest, ft, fs, fd) : up2(code, dest, ft, fs);
                    const bool flags_read = (shape + dest) & 1u;
                    clear_code();
                    put(0, kLowerNop, hi);
                    put(1, flags_read ? lo12(FSAND, 1, 0xFFF) : kLowerNop, kUpperNop | kEnd);
                    put(2, kLowerNop, kUpperNop);
                    vu::code_changed(1);
                    for (int k = 0; k < 6; ++k) run_upper(g, hi, flags_read, k & 1);
                }
        }
}

// The pairs that took the adder's two-sum out of range: an operand of the largest magnitude and another whose
// sum with it overflows on the way (gap 013's form; fp::add gives 0xFF7FFFFD for the first).
void the_top_of_the_range(Rng &g) {
    start_case("top of the range");
    const u32 pairs[][2] = {{0x73C00000u, 0xFF7FFFFFu}, {0xFE800003u, 0x7F7FFFFFu}, {0x7F7FFFFFu, 0xF3800001u}, {0xFF7FFFFFu, 0xFF7FFFFFu}, {0x7F7FFFFFu, 0x7F000000u}, {0x7F7FFFFEu, 0xF4000001u}};
    const u32 forms[] = {up1(0x28, 0xF, 2, 1, 3), up1(0x2C, 0xF, 2, 1, 3), up1(0x29, 0xF, 4, 1, 3), up1(0x2D, 0xF, 4, 1, 3), up2(0x28, 0xF, 2, 1), up2(0x2C, 0xF, 2, 1)};
    for (u32 hi : forms) {
        clear_code();
        put(0, kLowerNop, hi);
        put(1, kLowerNop, kUpperNop | kEnd);
        vu::code_changed(1);
        for (const auto &p : pairs) {
            fresh_registers(g, false);
            const bool accumulates = ((hi & 63u) == 0x29u) || ((hi & 63u) == 0x2Du);
            for (int k = 0; k < 4; ++k) {
                // MADD and MSUB: ACC + vf1 * 1.0, so the adder sees the pair itself.
                r.vf[1].w[k] = accumulates ? p[1] : p[0];
                r.vf[2].w[k] = k & 1 ? p[0] : p[1];
                r.vf[4].w[k] = fp::kOne;
                r.acc.w[k] = p[0];
            }
            if (!accumulates)
                for (int k = 0; k < 4; ++k) r.vf[1].w[k] = k & 1 ? p[1] : p[0];
            const ps2::Vu0Regs before = r;
            ps2::Vu0Regs want = before;
            reference_upper(want, hi, false);
            call(0);
            ++check::g_checks;
            const ps2::Reg128 &got = (hi & 0x3Cu) == 0x3Cu ? r.acc : r.vf[3], &expect = (hi & 0x3Cu) == 0x3Cu ? want.acc : want.vf[3];
            for (int k = 0; k < 4; ++k)
                if (got.w[k] != expect.w[k]) report(hi, "result", k, got.w[k], expect.w[k], before);
        }
    }
}

// A DIV in a subroutine called with BAL, which returns with JR before Q arrives: the result must arrive on
// time after the jump, as the interpreter counts it. Slots: 0 BAL to 10, 1 its delay slot, 2 the return,
// 5 and 6 MULq; 10 DIV, 11 JR, 12 its delay slot with a MULq that reads Q early on purpose. Order run:
// 0 1 10 11 12 2 3 4 5 6 7; Q arrives at the seventh instruction after the DIV, slot 6.
void q_across_a_register_jump(Rng &g) {
    start_case("Q across JR");
    clear_code();
    for (u32 slot = 0; slot < 16; ++slot) put(slot, kLowerNop, kUpperNop);
    put(0, branch(BAL, 15, 0, 0, 10), kUpperNop);
    put(5, kLowerNop, up1(MULq, 0xF, 0, 3, 5));
    put(6, kLowerNop, up1(MULq, 0xF, 0, 3, 6));
    put(7, kLowerNop, kUpperNop | kEnd);
    put(10, lo2(DIV, 0, 2, 1), kUpperNop);
    put(11, lom(JR, 0, 0, 15, 0), kUpperNop);
    put(12, kLowerNop, up1(MULq, 0xF, 0, 3, 7));
    vu::code_changed(1);
    for (int k = 0; k < 4; ++k) {
        fresh_registers(g, false);
        const u32 old_q = r.q;
        r.vf[1].w[0] = bits(9.0f * static_cast<float>(2 + k));
        r.vf[2].w[0] = bits(9.0f);
        const u32 new_q = bits(static_cast<float>(2 + k));
        const ps2::Vu0Regs before = r;
        call(0);
        for (int lane = 0; lane < 4; ++lane) {
            CHECK_EQ(r.vf[7].w[lane], vuops::mul(before.vf[3].w[lane], old_q));
            CHECK_EQ(r.vf[5].w[lane], vuops::mul(before.vf[3].w[lane], old_q));
            CHECK_EQ(r.vf[6].w[lane], vuops::mul(before.vf[3].w[lane], new_q));
        }
        CHECK_EQ(r.q, new_q);
    }
}

}

int main() {
    // The interpreter reports programs it cannot run on stderr; none is expected here.
    Rng g{0x1F2E3D4C5B6A7988ull};
    for (u32 &v : g_recent) v = typical(g);
    every_upper_against_the_helpers(g);
    the_top_of_the_range(g);
    q_across_a_register_jump(g);
    std::printf("test_vu1_jit: hash %016llX after %llu calls\n", static_cast<unsigned long long>(g_hash), static_cast<unsigned long long>(g_calls));
#if defined(__EMSCRIPTEN__)
    // In the page's build every program must have gone through the JIT, none through the interpreter.
    CHECK_EQ(vu::interpreted_calls(), 0u);
    std::printf("test_vu1_jit: %llu calls through the interpreter\n", static_cast<unsigned long long>(vu::interpreted_calls()));
#endif
    return check::done("test_vu1_jit");
}
