// The fork's VU0 interpreter alone. It has two loops that must leave the same state, bit for bit: the exact
// one (every lane computed by the integer model of ps2/fpu.hpp) and the fast one (instructions decoded once
// per code image, the model's arithmetic on host floats). Every call here runs three times from one state:
// through the exact loop, through the fast loop, and in the check mode, which runs both and compares them
// itself. Registers up to tpc, the 4 KB of data memory and what XGKICK sent must agree. Close is a failure:
// raw bytes are compared.
//
// The microprograms are made up: assembled here from instruction fields, by hand for the timing and
// ordering cases and from a seeded generator for the rest. The register contents are made up too.
//
// usage: test_vu0_loop [scale, default 1] [--seed N] [--stderr]
//        test_vu0_loop --environment fast|exact|check|live
//        test_vu0_loop --environments
//   scale multiplies the number of generated programs (2,000 programs x 3 register sets at 1);
//   --seed N draws the generated programs from another seed (the hashed set keeps its own);
//   --stderr keeps what the interpreter prints about the cases that provoke it;
//   --environment is a run of its own: the caller has set the variables that select the named mode, and
//     nothing here sets one (see mode_from_the_environment below);
//   --environments lists those runs, a mode and its variables on each line. The native program starts
//     every one of them itself; tests/wasm/run.sh starts them for the WebAssembly build.
// The "hash" line comes from a fixed set of calls and must be the same from every build, native and
// WebAssembly.
//
// fork-source: runtime/src/vu_micro.cpp
// needs-clang

#include "check.hpp"

#include "hw.hpp"
#include "vu_jit_web.hpp"
#include "vu_micro.hpp"
#include "gs/gif.hpp"
#include "gs/queue.hpp"

#include "ps2/fpu.hpp"
#include "ps2/generated.hpp"
#include "ps2/system.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

// What a call sent with XGKICK.
struct Kicks {
    u64 calls = 0, qwords = 0, hash = 0;
    bool operator==(const Kicks &) const = default;
};
Kicks g_kicks;

}

// The few things vu_micro.cpp takes from the rest of the runtime.
namespace ps2 {

Cpu g_cpu;
Options g_options;
std::uint8_t g_vu0_code[4096], g_vu0_mem[4096], g_vu1_code[16384], g_vu1_mem[16384];

namespace gif {
void transfer(int path, const std::uint8_t *data, std::uint32_t qwords) {
    ++g_kicks.calls;
    g_kicks.qwords += qwords;
    g_kicks.hash = (g_kicks.hash ^ static_cast<u64>(path)) * 0x9E3779B97F4A7C15ull;
    for (std::uint32_t i = 0; i < qwords * 16; ++i) g_kicks.hash = (g_kicks.hash ^ data[i]) * 0x100000001B3ull;
}
}

namespace gs::queue {
Role g_role = Role::Emulation;
Ring g_ring;
Role &role() { return g_role; }
Ring &vu() { return g_ring; }
bool vu_active() { return false; }
void Ring::post(Type, std::uint32_t, std::uint32_t, const void *, std::uint32_t) {}
}

namespace vu::jit {
bool g_dry = false;
std::int64_t g_escape = -1;
std::vector<std::uint32_t> *g_kicks = nullptr;
int mode() { return 0; }
ProgramFn compile(const std::uint8_t *, std::uint32_t) { return nullptr; }
}

}

namespace {

namespace vu = ps2::vu;
namespace fp = ps2::fp;
using vu::Vu0Mode;

ps2::Vu0Regs &r = ps2::g_cpu.vu0;

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

constexpr u32 kEnd = 0x40000000u, kIbit = 0x80000000u;   // bits of the upper word

// Upper instructions. The first table is indexed by the funct field; the forms that take one lane of ft for
// all lanes have that lane in the low two bits. The second table (funct 0x3C to 0x3F) is indexed by the fd
// field and the low two bits of funct, and holds the same arithmetic into ACC under the same numbers.
enum : u32 {
    ADDbc = 0x00, SUBbc = 0x04, MADDbc = 0x08, MSUBbc = 0x0C, MAXbc = 0x10, MINIbc = 0x14, MULbc = 0x18, MULq = 0x1C, MAXi = 0x1D, MULi = 0x1E, MINIi = 0x1F,
    ADDq = 0x20, MADDq = 0x21, ADDi = 0x22, MADDi = 0x23, SUBq = 0x24, MSUBq = 0x25, SUBi = 0x26, MSUBi = 0x27,
    ADD = 0x28, MADD = 0x29, MUL = 0x2A, MAX = 0x2B, SUB = 0x2C, MSUB = 0x2D, OPMSUB = 0x2E, MINI = 0x2F,
    ITOF0 = 0x10, FTOI0 = 0x14, ABS = 0x1D, CLIP = 0x1F, OPMULA = 0x2E, NOP = 0x2F,   // second table only
};
u32 up1(u32 funct, u32 dest, u32 ft, u32 fs, u32 fd) { return (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | funct; }
u32 up2(u32 index, u32 dest, u32 ft, u32 fs) { return (dest << 21) | (ft << 16) | (fs << 11) | ((index >> 2) << 6) | 0x3Cu | (index & 3u); }

// Lower instructions with an opcode in the top seven bits.
enum : u32 {
    LQ = 0x00, SQ = 0x01, ILW = 0x04, ISW = 0x05, IADDIU = 0x08, ISUBIU = 0x09, FCEQ = 0x10, FCSET = 0x11, FCAND = 0x12, FCOR = 0x13,
    FSEQ = 0x14, FSSET = 0x15, FSAND = 0x16, FSOR = 0x17, FMEQ = 0x18, FMAND = 0x1A, FMOR = 0x1B, FCGET = 0x1C,
    B = 0x20, BAL = 0x21, JR = 0x24, JALR = 0x25, IBEQ = 0x28, IBNE = 0x29, IBLTZ = 0x2C, IBGTZ = 0x2D, IBLEZ = 0x2E, IBGEZ = 0x2F,
};
// The other lowers: a funct field (integer arithmetic), or the fd field and the low two bits of funct.
enum : u32 {
    IADD = 0x30, ISUB = 0x31, IADDI = 0x32, IAND = 0x34, IOR = 0x35,
    MOVE = 0x30, MR32 = 0x31, LQI = 0x34, SQI = 0x35, LQD = 0x36, SQD = 0x37, DIV = 0x38, SQRT = 0x39, RSQRT = 0x3A, WAITQ = 0x3B, MTIR = 0x3C, MFIR = 0x3D,
    ILWR = 0x3E, ISWR = 0x3F, RNEXT = 0x40, RGET = 0x41, RINIT = 0x42, RXOR = 0x43, MFP = 0x64, XTOP = 0x68, XITOP = 0x69, XGKICK = 0x6C,
    ESADD = 0x70, ESQRT = 0x78, WAITP = 0x7B, ESIN = 0x7C,
};
u32 lom(u32 opcode, u32 dest, u32 t, u32 s, u32 imm11) { return (opcode << 25) | (dest << 21) | (t << 16) | (s << 11) | (imm11 & 0x7FFu); }
u32 lo1(u32 funct, u32 dest, u32 t, u32 s, u32 d) { return 0x80000000u | (dest << 21) | (t << 16) | (s << 11) | (d << 6) | funct; }
u32 lo2(u32 index, u32 dest, u32 t, u32 s) { return 0x80000000u | (dest << 21) | (t << 16) | (s << 11) | ((index >> 2) << 6) | 0x3Cu | (index & 3u); }
// A 12-bit immediate (the status instructions) and a 15-bit one (IADDIU, ISUBIU): the top bits sit in the dest field.
u32 lo12(u32 opcode, u32 t, u32 imm12) { return lom(opcode, (imm12 >> 11) & 1u, t, 0, imm12); }
u32 lo15(u32 opcode, u32 t, u32 s, u32 imm15) { return lom(opcode, (imm15 >> 11) & 15u, t, s, imm15); }
u32 lo24(u32 opcode, u32 imm24) { return (opcode << 25) | (imm24 & 0xFFFFFFu); }
// A branch at position `from` to position `to`: the offset counts from the instruction after the branch.
u32 branch(u32 opcode, u32 t, u32 s, int from, int to) { return lom(opcode, 0, t, s, static_cast<u32>(to - from - 1)); }

const u32 kUpperNop = up2(NOP, 0, 0, 0), kLowerNop = lo2(MOVE, 0, 0, 0);

struct Pair {
    u32 lower, upper;
};

void put(u32 slot, u32 lower, u32 upper) {
    std::memcpy(ps2::g_vu0_code + (slot & 511u) * 8, &lower, 4);
    std::memcpy(ps2::g_vu0_code + (slot & 511u) * 8 + 4, &upper, 4);
}

// Every slot ends the program: an idle pair with the E bit. A program that runs past its last instruction
// stops two instructions later.
void clear_code() {
    for (u32 slot = 0; slot < 512; ++slot) put(slot, kLowerNop, kUpperNop | kEnd);
}

void program(u32 start, std::initializer_list<Pair> pairs) {
    clear_code();
    for (const Pair &p : pairs) put(start++, p.lower, p.upper);
    vu::code_changed(0);
}

// The first `count` slots idle and without the E bit, for a case that then places instructions among them.
void idle_program(u32 count) {
    clear_code();
    for (u32 slot = 0; slot < count; ++slot) put(slot, kLowerNop, kUpperNop);
}

// ---- register contents -------------------------------------------------------------------------------

u32 bits(float f) { return std::bit_cast<u32>(f); }

// Ordinary numbers: magnitudes around one, some zeros and ones.
u32 typical(Rng &g) {
    const u32 pick = g.below(100);
    if (pick < 12) return g.bits() & 0x80000000u;
    if (pick < 17) return fp::kOne;
    return (g.bits() & 0x807FFFFFu) | ((117u + g.below(21)) << 23);
}

u32 g_recent[8];

// The classes arithmetic gets wrong first: both ends of the range, the top binade (numbers on a VU, not on
// the host), denormals, and values next to an earlier one, for cancellation and ties.
u32 edge(Rng &g) {
    u32 v;
    switch (g.below(12)) {
    case 0: v = g.bits(); break;
    case 1: v = g.bits() & 0x807FFFFFu; break;
    case 2: v = g.bits() | 0x7F800000u; break;
    case 3: v = (g.bits() & 0x80000000u) | (g.chance(500) ? 0x7FFFFFFFu : 0x7F7FFFFFu); break;
    case 4: v = (g.bits() & 0x807FFFFFu) | ((1u + g.below(3)) << 23); break;
    case 5: v = (g.bits() & 0x807FFFFFu) | ((252u + g.below(3)) << 23); break;
    case 6: v = g.bits() & 0x80000000u; break;
    case 7: v = (g.bits() & 0x80000000u) | (g.below(256) << 23) | (g.chance(500) ? 0u : 0x7FFFFFu); break;
    case 8:
        v = g_recent[g.below(8)];
        if (g.chance(500)) v ^= 0x80000000u;
        v += g.below(5) - 2u;
        if (g.chance(300)) v = (v & 0x807FFFFFu) | ((((v >> 23) + g.below(60) - 30u) & 0xFFu) << 23);
        break;
    default: v = typical(g); break;
    }
    g_recent[g.below(8)] = v;
    return v;
}

void fresh_registers(Rng &g, bool edges) {
    const auto value = [&]() { return edges ? edge(g) : typical(g); };
    for (int i = 0; i < 32; ++i)
        for (int k = 0; k < 4; ++k) r.vf[i].w[k] = value();
    r.vf[0].w[0] = r.vf[0].w[1] = r.vf[0].w[2] = 0;
    r.vf[0].w[3] = fp::kOne;
    for (int k = 0; k < 4; ++k) r.acc.w[k] = value();
    for (int i = 0; i < 16; ++i) r.vi[i] = static_cast<std::uint16_t>(edges && g.chance(300) ? g.bits() : g.below(256));
    r.vi[0] = 0;
    r.q = value();
    r.p = value();
    r.i = value();
    r.r = g.bits() & 0x7FFFFFu;
    r.mac = g.bits() & 0xFFFFu;
    r.status = g.bits() & 0xFFFu;
    r.clip = g.bits() & 0xFFFFFFu;
    r.cmsar0 = g.bits() & 0xFFFFu;
    r.tpc = g.bits() & 0xFFFu;
    for (int i = 0; i < 1024; ++i) {
        const u32 v = value();
        std::memcpy(ps2::g_vu0_mem + i * 4, &v, 4);
    }
}

void set_vf(int index, float x, float y, float z, float w) {
    r.vf[index].w[0] = bits(x);
    r.vf[index].w[1] = bits(y);
    r.vf[index].w[2] = bits(z);
    r.vf[index].w[3] = bits(w);
}

void set_mem(u32 qword, u32 x, u32 y, u32 z, u32 w) {
    const u32 v[4] = {x, y, z, w};
    std::memcpy(ps2::g_vu0_mem + (qword & 255u) * 16, v, 16);
}

u32 mem_word(u32 qword, int lane, const std::uint8_t *mem = ps2::g_vu0_mem) {
    u32 v;
    std::memcpy(&v, mem + (qword & 255u) * 16 + lane * 4, 4);
    return v;
}

// ---- one call through both loops ---------------------------------------------------------------------

constexpr std::size_t kRegBytes = offsetof(ps2::Vu0Regs, tpc) + sizeof(u32);   // the struct ends in padding
constexpr std::int32_t kBudget = 123456;                                       // the EE's cycles: no VU0 call may touch them

struct State {
    ps2::Vu0Regs regs;
    std::uint8_t mem[4096];
};

State take() {
    State s;
    s.regs = r;
    std::memcpy(s.mem, ps2::g_vu0_mem, sizeof s.mem);
    return s;
}

void restore(const State &s) {
    r = s.regs;
    std::memcpy(ps2::g_vu0_mem, s.mem, sizeof s.mem);
}

bool same(const State &a, const State &b) { return std::memcmp(&a.regs, &b.regs, kRegBytes) == 0 && std::memcmp(a.mem, b.mem, sizeof a.mem) == 0; }

// Where two states first differ, for a failure message.
const char *where(const State &a, const State &b) {
    static char text[64];
    for (int i = 0; i < 32; ++i)
        if (std::memcmp(&a.regs.vf[i], &b.regs.vf[i], 16) != 0) {
            std::snprintf(text, sizeof text, "vf%d (%08X %08X %08X %08X)", i, a.regs.vf[i].w[0], a.regs.vf[i].w[1], a.regs.vf[i].w[2], a.regs.vf[i].w[3]);
            return text;
        }
    if (std::memcmp(&a.regs.acc, &b.regs.acc, 16) != 0) return "acc";
    for (int i = 0; i < 16; ++i)
        if (a.regs.vi[i] != b.regs.vi[i]) {
            std::snprintf(text, sizeof text, "vi%d (%04X, not %04X)", i, a.regs.vi[i], b.regs.vi[i]);
            return text;
        }
    if (a.regs.q != b.regs.q) return "q";
    if (a.regs.p != b.regs.p) return "p";
    if (a.regs.i != b.regs.i) return "i";
    if (a.regs.r != b.regs.r) return "r";
    if (a.regs.mac != b.regs.mac) return "mac";
    if (a.regs.status != b.regs.status) return "status";
    if (a.regs.clip != b.regs.clip) return "clip";
    if (a.regs.cmsar0 != b.regs.cmsar0) return "cmsar0";
    if (a.regs.tpc != b.regs.tpc) return "tpc";
    return "data memory";
}

Kicks run(Vu0Mode mode, u32 slot) {
    g_kicks = Kicks{};
    vu::set_vu0_mode(mode);
    ps2::vcallms(ps2::g_cpu, slot * 8);
    return g_kicks;
}

// Calls since the last settle() in which something disagreed, by what did.
struct Tally {
    u64 calls = 0, steps = 0, kicks = 0, kicked_qwords = 0;
    u64 fast_differs = 0;      // the fast loop's state is not the exact loop's
    u64 check_differs = 0;     // the state the check mode left is not the exact loop's
    u64 check_reported = 0;    // the check mode counted a difference
    u64 not_compared = 0;      // the check mode did not compare the call
    u64 kicks_differ = 0;      // XGKICK: not the same data from every run, or not skipped in the check's first run
    u64 never_ended = 0;       // the call ran into the limit of a million instructions
    int shown = 0;
};
Tally g_tally;
const char *g_case = "";

// One call from the current state through the exact loop, the fast loop and the check mode, each from the
// same state. The exact loop's outcome is left in place and returned.
State call(u32 slot) {
    const State before = take();
    const u64 steps_before = vu::vu0_counters().steps;
    const Kicks exact_kicks = run(Vu0Mode::Exact, slot);
    const u64 steps = vu::vu0_counters().steps - steps_before;
    const State exact = take();
    restore(before);
    const Kicks fast_kicks = run(Vu0Mode::Fast, slot);
    const State fast = take();
    restore(before);
    const vu::Vu0Counters counted = vu::vu0_counters();
    const Kicks check_kicks = run(Vu0Mode::Check, slot);
    const State check = take();
    const vu::Vu0Counters &now = vu::vu0_counters();

    ++g_tally.calls;
    g_tally.steps += steps;
    g_tally.kicks += exact_kicks.calls;
    g_tally.kicked_qwords += exact_kicks.qwords;
    if (steps >= 1'000'000) ++g_tally.never_ended;
    const bool fast_differs = !same(fast, exact), check_differs = !same(check, exact);
    if (fast_differs) ++g_tally.fast_differs;
    if (check_differs) ++g_tally.check_differs;
    if (now.differ != counted.differ) ++g_tally.check_reported;
    if (now.compared != counted.compared + 1) ++g_tally.not_compared;
    const bool kicks_differ = !(fast_kicks == exact_kicks) || !(check_kicks == exact_kicks) || now.kicks_skipped != counted.kicks_skipped + exact_kicks.calls;
    if (kicks_differ) ++g_tally.kicks_differ;
    if ((fast_differs || check_differs || kicks_differ) && g_tally.shown++ < 4) {
        std::printf("  %s: call %llu, entered at slot %u: ", g_case, static_cast<unsigned long long>(g_tally.calls), slot);
        if (fast_differs) std::printf("the fast loop differs from the exact one first in %s\n", where(fast, exact));
        else if (check_differs) std::printf("the check mode left a state that differs from the exact loop's first in %s\n", where(check, exact));
        else std::printf("XGKICK did not send the same from every run, or was not skipped once in the check\n");
    }
    restore(exact);
    return exact;
}

// Ends a group of calls: nothing may have disagreed, the cycle budget is untouched, and every program ended
// (but for the ones a case counts on not to).
Tally settle(u64 never_ended = 0) {
    const Tally t = g_tally;
    CHECK(t.calls > 0);
    CHECK_EQ(t.fast_differs, 0u);
    CHECK_EQ(t.check_differs, 0u);
    CHECK_EQ(t.check_reported, 0u);
    CHECK_EQ(t.not_compared, 0u);
    CHECK_EQ(t.kicks_differ, 0u);
    CHECK_EQ(t.never_ended, never_ended);
    CHECK_EQ(ps2::g_cpu.budget, kBudget);
    g_tally = Tally{};
    return t;
}

// ---- every upper instruction -------------------------------------------------------------------------

// Six readers after the instruction under test: the MAC flags, the status and the clip flags as later
// instructions see them, twice each, so a flag that arrives too early or too late lands in a register.
void put_readers(u32 slot) {
    put(slot + 0, lom(FMAND, 0, 1, 15, 0), kUpperNop);
    put(slot + 1, lo12(FSAND, 2, 0xFFF), kUpperNop);
    put(slot + 2, lom(FCGET, 0, 3, 0, 0), kUpperNop);
    put(slot + 3, lom(FMAND, 0, 4, 15, 0), kUpperNop);
    put(slot + 4, lo12(FSAND, 5, 0xFFF), kUpperNop);
    put(slot + 5, lom(FCGET, 0, 6, 0, 0), kUpperNop);
}

// Every code of both upper tables, the unassigned ones included, under all 16 lane masks, with the
// registers picked six ways: apart, the result into vf0, vf0 as either operand, and operands that coincide.
void every_upper_under_every_lane_mask(Rng &g) {
    g_case = "every upper";
    const u64 slow_before = vu::vu0_counters().slow;
    for (u32 table = 1; table <= 2; ++table)
        for (u32 code = 0; code < (table == 1 ? 0x3Cu : 128u); ++code)
            for (u32 dest = 0; dest < 16; ++dest)
                for (u32 shape = 0; shape < 6; ++shape) {
                    u32 ft = 1 + g.below(31), fs = 1 + g.below(31), fd = 1 + g.below(31);
                    if (shape == 1) (table == 1 ? fd : ft) = 0;   // the result goes to vf0 (the second table writes ft or ACC)
                    if (shape == 2) ft = 0;                       // 0.0 in x, y and z, 1.0 in w
                    if (shape == 3) fs = 0;
                    if (shape == 4) ft = fs;
                    if (shape == 5) fd = fs;
                    const u32 start = g.below(500);
                    clear_code();
                    put(start, kLowerNop, table == 1 ? up1(code, dest, ft, fs, fd) : up2(code, dest, ft, fs));
                    put_readers(start + 1);
                    vu::code_changed(0);
                    fresh_registers(g, g.chance(600));
                    r.vi[15] = 0xFFFF;
                    call(start);
                }
    const Tally t = settle();
    CHECK_EQ(t.calls, 188u * 16u * 6u);
    // Edge values put results in the top binade, which the fast loop hands to the model.
    CHECK(vu::vu0_counters().slow > slow_before);

    // Only the instruction's own lanes can send it to the model: a top-binade value in a lane it does not
    // write costs nothing.
    program(0, {{kLowerNop, up1(ADD, 0xE, 2, 1, 5)}, {kLowerNop, up2(MUL, 0x6, 2, 1)}, {kLowerNop, up1(MSUBbc + 1, 0xC, 2, 1, 6)}});
    fresh_registers(g, false);
    r.vf[1].w[3] = 0x7FFFFFFFu;
    r.vf[2].w[3] = 0xFF800001u;
    r.acc.w[3] = 0x7F800000u;
    const u64 slow_ordinary = vu::vu0_counters().slow;
    call(0);
    settle();
    CHECK_EQ(vu::vu0_counters().slow, slow_ordinary);
}

// ---- when flags, Q and I become visible --------------------------------------------------------------

// The MAC flags and the status of a result reach the flag instructions four instructions later, the clip
// flags three later. A reader of each kind at every distance from one to six after each kind of setter.
void flag_readers_at_every_distance(Rng &g) {
    g_case = "flag readers";
    const Pair setters[] = {
        {kLowerNop, up1(ADD, 0xF, 2, 1, 5)},
        {kLowerNop, up2(SUB, 0xE, 2, 1)},              // SUBA
        {kLowerNop, up1(MULq, 0xB, 0, 1, 0)},          // flags only
        {kLowerNop, up2(CLIP, 0xE, 2, 1)},
        {lo12(FSSET, 0, 0xAC0), kUpperNop},
        {lo24(FCSET, 0x5A5A5A), kUpperNop},
        {lo2(DIV, 0, 2, 1), up1(SUB, 0x8, 2, 1, 5)},   // DIV writes its two status bits; the upper its own
        {lo12(FSSET, 0, 0x540), up1(ADD, 0xF, 2, 1, 5)},
    };
    const u32 readers[] = {lom(FMEQ, 0, 1, 14, 0), lom(FMAND, 0, 1, 15, 0), lom(FMOR, 0, 1, 14, 0), lo12(FSEQ, 1, 0xAC2), lo12(FSAND, 1, 0xFFF), lo12(FSOR, 1, 0x101),
                           lo24(FCEQ, 0x5A5A5A),   lo24(FCAND, 0x00003F), lo24(FCOR, 0xFFFFC0),   lom(FCGET, 0, 1, 0, 0)};
    for (const Pair &setter : setters)
        for (u32 reader : readers)
            for (u32 distance = 1; distance <= 6; ++distance)
                for (int values = 0; values < 3; ++values) {
                    idle_program(8);
                    put(0, setter.lower, setter.upper);
                    put(distance, reader, kUpperNop);
                    vu::code_changed(0);
                    fresh_registers(g, values == 2);
                    r.vi[15] = 0xFFFF;
                    call(0);
                }
    settle();

    // Two results in a row, and the MAC flags read after every instruction: the fifth instruction sees the
    // first result's flags, the sixth the second's.
    clear_code();
    put(0, kLowerNop, up1(ADD, 0xF, 2, 1, 5));
    for (u32 k = 1; k <= 6; ++k) put(k, lom(FMAND, 0, k, 15, 0), k == 1 ? up1(SUB, 0xF, 2, 1, 6) : kUpperNop);
    vu::code_changed(0);
    fresh_registers(g, false);
    set_vf(1, 1.0f, -2.0f, 0.0f, 4.0f);
    set_vf(2, 0.0f, 0.0f, 0.0f, -4.0f);
    r.vi[15] = 0xFFFF;
    r.mac = 0x0F00;
    call(0);
    CHECK_EQ(r.vi[3], 0x0F00);   // three instructions after the first result: still what the call began with
    CHECK_EQ(r.vi[4], 0x0043);   // the sum (1, -2, 0, 0): zero in z and w, negative in y
    CHECK_EQ(r.vi[5], 0x0042);   // the difference (1, -2, 0, 8)
    CHECK_EQ(r.mac, 0x0042u);
    settle();
}

// DIV and SQRT deliver Q seven instructions after they start, RSQRT thirteen; until then Q holds the result
// before. A multiply by Q at every distance from 1 to 14.
void q_readers_at_every_distance(Rng &g) {
    g_case = "Q readers";
    const struct {
        u32 lower;
        u32 latency;
        float result;
    } starts[] = {{lo2(DIV, 0, 2, 1), 7, 2.0f}, {lo2(SQRT, 0, 2, 0), 7, 3.0f}, {lo2(RSQRT, 0, 2, 1), 13, 6.0f}};
    for (const auto &start : starts) {
        for (u32 distance = 1; distance <= 14; ++distance) {
            idle_program(16);
            put(0, start.lower, kUpperNop);
            put(distance, kLowerNop, up1(MULq, 0xF, 0, 3, 10));
            vu::code_changed(0);
            fresh_registers(g, false);
            set_vf(1, 18.0f, 0.0f, 0.0f, 0.0f);
            set_vf(2, 9.0f, 0.0f, 0.0f, 0.0f);
            set_vf(3, 1.0f, 1.0f, 1.0f, 1.0f);
            r.q = bits(5.0f);
            call(0);
            CHECK_EQ(r.vf[10].w[0], bits(distance >= start.latency ? start.result : 5.0f));
            CHECK_EQ(r.q, bits(start.result));
        }
        // The same with a reader in every instruction, with WAITQ at each place, and with a second divide
        // started at each place while the first is still under way.
        for (u32 variant = 0; variant < 3; ++variant)
            for (u32 at = 1; at <= 14; ++at)
                for (int values = 0; values < 2; ++values) {
                    idle_program(16);
                    put(0, start.lower, kUpperNop);
                    for (u32 k = 1; k <= 15; ++k) put(k, kLowerNop, up1(k & 1 ? MULq : ADDq, 0xF, 0, 3, 10 + k));
                    if (variant == 1) put(at, lo2(WAITQ, 0, 0, 0), up1(MULq, 0xF, 0, 3, 10 + at));
                    if (variant == 2) put(at, lo2(DIV, 0xD, 4, 3), up2(MADDq, 0xF, 0, 3));
                    vu::code_changed(0);
                    fresh_registers(g, values == 1);
                    call(0);
                }
    }
    // The program ends with a result still on its way: Q has it afterwards.
    for (u32 length = 1; length <= 3; ++length) {
        clear_code();
        for (u32 k = 0; k < length; ++k) put(k, k + 1 == length ? lo2(DIV, 0, 2, 1) : kLowerNop, kUpperNop | (k + 2 == length || length == 1 ? kEnd : 0u));
        vu::code_changed(0);
        fresh_registers(g, false);
        set_vf(1, 18.0f, 0.0f, 0.0f, 0.0f);
        set_vf(2, 9.0f, 0.0f, 0.0f, 0.0f);
        call(0);
        CHECK_EQ(r.q, bits(2.0f));
    }
    // A divide by zero: Q is the largest magnitude, the status says so, and Q is then an operand, which
    // the fast loop cannot compute on host floats.
    const u64 slow_before = vu::vu0_counters().slow;
    idle_program(10);
    put(0, lo2(DIV, 0, 2, 1), kUpperNop);
    put(7, kLowerNop, up1(MULq, 0xF, 0, 3, 10));
    put(8, kLowerNop, up1(ADDq, 0xE, 0, 3, 11));
    put(9, lo12(FSAND, 1, 0xFFF), up2(MSUBq, 0xF, 0, 3));
    vu::code_changed(0);
    fresh_registers(g, false);
    set_vf(1, -18.0f, 0.0f, 0.0f, 0.0f);
    set_vf(2, 0.0f, 0.0f, 0.0f, 0.0f);
    set_vf(3, 1.0f, 2.0f, -3.0f, 0.0f);
    call(0);
    CHECK_EQ(r.q, 0xFFFFFFFFu);
    CHECK_EQ(r.vf[10].w[0], 0xFFFFFFFFu);
    CHECK_EQ(r.vf[10].w[3], 0x80000000u);
    CHECK_EQ(r.status & 0x30u, 0x20u);
    settle();
    CHECK(vu::vu0_counters().slow > slow_before);

    // P the same way: a result on its way, read back with MFP, and WAITP.
    g_case = "P readers";
    for (u32 index : {u32{ESADD}, u32{ESQRT}, u32{ESIN}})
        for (u32 at = 1; at <= 30; at += 1 + at / 12)
            for (int wait = 0; wait < 2; ++wait) {
                idle_program(34);
                put(0, lo2(index, 0, 0, 1), kUpperNop);
                if (wait) put(at - 1 ? at - 1 : 1, lo2(WAITP, 0, 0, 0), kUpperNop);
                put(at + wait, lo2(MFP, 0xF, 10, 0), kUpperNop);
                vu::code_changed(0);
                fresh_registers(g, false);
                call(0);
            }
    settle();
}

// ---- order inside one instruction, and the ends of a program -----------------------------------------

void order_within_an_instruction(Rng &g) {
    g_case = "order";
    // The I bit: the lower word is a number for I, and an upper in the same instruction still reads the old I.
    program(0, {{bits(8.0f), up1(ADDi, 0xF, 0, 1, 5) | kIbit}, {kLowerNop, up1(ADDi, 0xF, 0, 1, 6)}});
    fresh_registers(g, false);
    set_vf(1, 1.0f, 1.0f, 1.0f, 1.0f);
    r.i = bits(2.0f);
    call(0);
    CHECK_EQ(r.vf[5].w[0], bits(3.0f));
    CHECK_EQ(r.vf[6].w[0], bits(9.0f));
    CHECK_EQ(r.i, bits(8.0f));

    // Upper and lower write one register: the upper's lanes win, the lower's other lanes stay.
    program(0, {{lom(LQ, 0xF, 5, 3, 0), up1(ADD, 0xC, 2, 1, 5)}});
    fresh_registers(g, false);
    set_vf(1, 1.0f, 2.0f, 3.0f, 4.0f);
    set_vf(2, 10.0f, 20.0f, 30.0f, 40.0f);
    r.vi[3] = 7;
    set_mem(7, 0x11111111, 0x22222222, 0x33333333, 0x44444444);
    call(0);
    CHECK_EQ(r.vf[5].w[0], bits(11.0f));
    CHECK_EQ(r.vf[5].w[1], bits(22.0f));
    CHECK_EQ(r.vf[5].w[2], 0x33333333u);
    CHECK_EQ(r.vf[5].w[3], 0x44444444u);

    // A lower reads the register the upper writes, and an upper the register the lower writes: both see
    // the value from before the instruction.
    program(0, {{lom(SQ, 0xF, 4, 5, 0), up1(ADD, 0xF, 2, 1, 5)}, {lom(LQ, 0xF, 2, 3, 0), up1(ADD, 0xF, 2, 1, 6)}, {lo2(MTIR, 0, 9, 7), up2(ITOF0, 0xF, 7, 7)}});
    fresh_registers(g, false);
    set_vf(1, 1.0f, 2.0f, 3.0f, 4.0f);
    set_vf(2, 10.0f, 20.0f, 30.0f, 40.0f);
    set_vf(5, 5.0f, 5.0f, 5.0f, 5.0f);
    r.vf[7].w[0] = 0x1234;
    r.vi[3] = 7;
    r.vi[4] = 8;
    set_mem(7, bits(100.0f), bits(200.0f), bits(300.0f), bits(400.0f));
    call(0);
    CHECK_EQ(mem_word(8, 0), bits(5.0f));
    CHECK_EQ(r.vf[5].w[0], bits(11.0f));
    CHECK_EQ(r.vf[6].w[0], bits(11.0f));
    CHECK_EQ(r.vf[2].w[0], bits(100.0f));
    CHECK_EQ(r.vi[9], 0x1234);
    CHECK_EQ(r.vf[7].w[0], bits(4660.0f));

    // The lower sets the status, starts a divide or moves a register while the upper in the same
    // instruction sets flags, multiplies by Q or writes the moved register.
    const Pair pairs[] = {{lo12(FSSET, 0, 0xFC0), up1(SUB, 0xF, 2, 1, 5)}, {lo2(DIV, 0x7, 2, 1), up1(MULq, 0xE, 0, 1, 5)}, {lo2(MOVE, 0xF, 5, 2), up1(MUL, 0x6, 2, 1, 5)},
                          {lo2(MR32, 0xF, 6, 5), up1(MSUB, 0xF, 2, 1, 5)}, {lo2(MFIR, 0x9, 5, 3), up2(ABS, 0xF, 5, 1)}, {lo2(RNEXT, 0xF, 5, 0), up2(ITOF0 + 2, 0xA, 5, 1)},
                          {lom(FMAND, 0, 2, 15, 0), up1(ADDbc + 3, 0xF, 0, 1, 5)}, {lo15(IADDIU, 3, 3, 1), up2(FTOI0 + 1, 0xF, 5, 1)}, {lo24(FCSET, 0x123456), up2(CLIP, 0xE, 2, 1)}};
    for (const Pair &first : pairs)
        for (const Pair &second : pairs)
            for (int values = 0; values < 2; ++values) {
                program(0, {first, second, {lom(FMAND, 0, 7, 15, 0), kUpperNop}, {lo12(FSAND, 8, 0xFFF), kUpperNop}, {kLowerNop, kUpperNop}, {lom(FMAND, 0, 10, 15, 0), kUpperNop}});
                fresh_registers(g, values == 1);
                r.vi[15] = 0xFFFF;
                call(0);
            }
    settle();
}

void the_ends_of_a_program(Rng &g) {
    g_case = "program ends";
    // The E bit in a branch's delay slot: the branch target still runs, then the program stops.
    program(0, {{branch(B, 0, 0, 0, 4), kUpperNop}, {lo15(IADDIU, 1, 0, 1), kUpperNop | kEnd}, {lo15(IADDIU, 2, 0, 2), kUpperNop}, {lo15(IADDIU, 2, 0, 2), kUpperNop},
                {lo15(IADDIU, 3, 0, 3), kUpperNop}, {lo15(IADDIU, 4, 0, 4), kUpperNop}});
    fresh_registers(g, false);
    r.vi[1] = r.vi[2] = r.vi[3] = r.vi[4] = 0;
    call(0);
    CHECK_EQ(r.vi[1], 1);
    CHECK_EQ(r.vi[2], 0);
    CHECK_EQ(r.vi[3], 3);
    CHECK_EQ(r.vi[4], 0);
    CHECK_EQ(r.tpc, 5u * 8u);

    // A branch in the instruction with the E bit: the next instruction runs, and the program counter left
    // behind is the branch target.
    program(0, {{branch(B, 0, 0, 0, 6), kUpperNop | kEnd}, {lo15(IADDIU, 1, 0, 1), kUpperNop}});
    fresh_registers(g, false);
    call(0);
    CHECK_EQ(r.vi[1], 1);
    CHECK_EQ(r.tpc, 6u * 8u);

    // A branch in a delay slot: one instruction at the first target, then the second target.
    program(0, {{branch(B, 0, 0, 0, 5), kUpperNop}, {branch(B, 0, 0, 1, 8), kUpperNop}, {kLowerNop, kUpperNop}, {kLowerNop, kUpperNop}, {kLowerNop, kUpperNop},
                {lo15(IADDIU, 1, 0, 1), kUpperNop}, {lo15(IADDIU, 2, 0, 2), kUpperNop}, {kLowerNop, kUpperNop}, {lo15(IADDIU, 3, 0, 3), kUpperNop}});
    fresh_registers(g, false);
    r.vi[1] = r.vi[2] = r.vi[3] = 0;
    call(0);
    CHECK_EQ(r.vi[1], 1);
    CHECK_EQ(r.vi[2], 0);
    CHECK_EQ(r.vi[3], 3);

    // Every branch, taken and not, with its register at the ends of the 16-bit range; the linking ones
    // and the jumps through a register go through the general path.
    const std::uint16_t ends[] = {0, 1, 0x7FFF, 0x8000, 0xFFFF};
    for (u32 opcode : {u32{B}, u32{BAL}, u32{JR}, u32{JALR}, u32{IBEQ}, u32{IBNE}, u32{IBLTZ}, u32{IBGTZ}, u32{IBLEZ}, u32{IBGEZ}})
        for (std::uint16_t a : ends)
            for (std::uint16_t b : ends) {
                const bool jump = opcode == JR || opcode == JALR;
                program(20, {{lo15(IADDIU, 3, 0, 1), kUpperNop}, {jump ? lom(opcode, 0, 7, 5, 0) : branch(opcode, opcode == BAL ? 7 : 6, 5, 1, 4), kUpperNop},
                             {lo15(IADDIU, 3, 3, 2), kUpperNop}, {lo15(IADDIU, 3, 3, 4), kUpperNop}, {lo15(IADDIU, 3, 3, 8), kUpperNop}});
                fresh_registers(g, false);
                r.vi[5] = a;
                r.vi[6] = b;
                if (jump) r.vi[5] = static_cast<std::uint16_t>(24 + (a & 3));
                const State exact = call(20);
                if (opcode == BAL || opcode == JALR) CHECK_EQ(exact.regs.vi[7], 23);   // the instruction after the delay slot
            }
    // The offset near its ends: the target wraps around the 4 KB of code, to the slot before the branch
    // or the one after it.
    for (u32 imm : {0x1FEu, 0x200u, 0x3FEu, 0x400u, 0x7FEu, 0u})
        for (u32 start : {0u, 2u, 510u, 511u}) {
            program(start, {{lom(B, 0, 0, 0, imm), up1(ADD, 0xF, 2, 1, 5)}, {lom(IBNE, 0, 1, 0, imm), up1(SUB, 0xF, 2, 1, 6)}});
            fresh_registers(g, false);
            call(start);
        }
    settle();

    // A program that never ends: both loops give up after the same number of instructions, with a divide
    // still on its way and the program counter as the call found it.
    g_case = "no end";
    program(0, {{branch(B, 0, 0, 0, 0), up1(ADD, 0xF, 2, 1, 1)}, {lo2(DIV, 0x4, 3, 1), up1(MULq, 0xF, 0, 1, 2)}});
    fresh_registers(g, false);
    set_vf(1, 1.0f, 1.0f, 1.0f, 1.0f);
    set_vf(2, 0.25f, -0.5f, 0.0f, 1.0f);
    set_vf(3, 3.0f, 3.0f, 3.0f, 3.0f);
    r.tpc = 0x123;
    const State stuck = call(0);
    CHECK_EQ(stuck.regs.tpc, 0x123u);
    settle(1);
}

// ---- the lowers the fast loop decodes ----------------------------------------------------------------

void decoded_lowers_at_their_edges(Rng &g) {
    g_case = "decoded lowers";
    // vi0 and vf0 as the target of each: nothing may be written.
    const u32 into_zero[] = {lo15(IADDIU, 0, 3, 0x7FFF), lo15(ISUBIU, 0, 3, 1), lo1(IADD, 0, 4, 3, 0), lo1(IAND, 0, 4, 3, 0), lo1(IOR, 0, 4, 3, 0), lom(LQ, 0xF, 0, 3, 5),
                             lom(FMAND, 0, 0, 15, 0), lom(FMOR, 0, 0, 3, 0), lo2(MOVE, 0xF, 0, 5), lo2(MR32, 0xF, 0, 5), lo2(MFIR, 0xF, 0, 3), lo2(LQI, 0xF, 0, 0), lo2(MTIR, 0, 0, 5)};
    for (u32 lower : into_zero) {
        program(3, {{lower, up1(ADD, 0xF, 2, 1, 5)}, {lo1(IADD, 0, 0, 0, 7), up1(ADDbc + 3, 0xF, 0, 1, 6)}});
        fresh_registers(g, false);
        r.vi[15] = 0xFFFF;
        const State exact = call(3);
        CHECK_EQ(exact.regs.vi[0], 0);
        CHECK_EQ(exact.regs.vi[7], 0);
        CHECK_EQ(exact.regs.vf[0].w[3], fp::kOne);
        CHECK_EQ(exact.regs.vf[0].w[0], 0u);
    }
    // The 15-bit immediates at their ends, on registers at theirs.
    for (u32 opcode : {u32{IADDIU}, u32{ISUBIU}})
        for (u32 imm : {0u, 1u, 0x7FFu, 0x800u, 0x4000u, 0x7FFFu})
            for (std::uint16_t value : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{0x7FFF}, std::uint16_t{0x8000}, std::uint16_t{0xFFFF}}) {
                program(0, {{lo15(opcode, 4, 3, imm), kUpperNop}, {lo15(opcode, 3, 3, imm), kUpperNop}});
                fresh_registers(g, false);
                r.vi[3] = value;
                call(0);
                CHECK_EQ(r.vi[4], static_cast<std::uint16_t>(opcode == IADDIU ? value + imm : value - imm));
            }
    // LQ and SQ with the offset at its ends, under every lane mask: the address wraps at 256 qwords, and
    // only the instruction's lanes move.
    for (u32 imm : {0u, 1u, 0x3FFu, 0x400u, 0x7FFu})
        for (std::uint16_t base : {std::uint16_t{0}, std::uint16_t{255}, std::uint16_t{256}, std::uint16_t{0xFFFF}})
            for (u32 dest = 0; dest < 16; ++dest) {
                program(0, {{lom(SQ, dest, 3, 5, imm), kUpperNop}, {lom(LQ, dest, 6, 3, imm + 1), kUpperNop}, {lom(LQ, dest, 7, 3, imm), kUpperNop}});
                fresh_registers(g, dest & 1);
                r.vi[3] = base;
                const State before = take();
                call(0);
                const u32 qword = (base + (imm & 0x400u ? imm | 0xFFFFF800u : imm)) & 255u;
                int wrong = 0;
                for (int lane = 0; lane < 4; ++lane) {
                    const bool moved = dest & (8u >> lane);
                    if (mem_word(qword, lane) != (moved ? before.regs.vf[5].w[lane] : mem_word(qword, lane, before.mem))) ++wrong;
                    if (r.vf[7].w[lane] != (moved ? before.regs.vf[5].w[lane] : before.regs.vf[7].w[lane])) ++wrong;
                }
                CHECK_EQ(wrong, 0);
            }
    settle();
}

// XGKICK sends a packet from data memory to the GIF. The check mode runs every call twice and must still
// send it once: the fast loop's run skips it and counts it.
void xgkick_is_sent_once(Rng &g) {
    g_case = "XGKICK";
    program(0, {{lo15(IADDIU, 5, 0, 10), kUpperNop}, {lo2(XGKICK, 0, 0, 5), up1(ADD, 0xF, 2, 1, 6)}});
    fresh_registers(g, false);
    // A made-up packet at qword 10: a tag for two qwords of register data, marked as the last, then the two.
    set_mem(10, 0x00008002, 0x10000000, 0x0000000E, 0);
    const u64 skipped = vu::vu0_counters().kicks_skipped;
    call(0);
    Tally t = settle();
    CHECK_EQ(t.kicks, 1u);
    CHECK_EQ(t.kicked_qwords, 3u);
    CHECK_EQ(vu::vu0_counters().kicks_skipped, skipped + 1);
    // The same when the call starts with vi0 wrong and the fast loop hands it to the exact loop.
    r.vi[0] = 3;
    call(0);
    t = settle();
    CHECK_EQ(t.kicks, 1u);
    CHECK_EQ(vu::vu0_counters().kicks_skipped, skipped + 2);
}

// The fast loop relies on vf0 being (0, 0, 0, 1.0) and vi0 being 0, which the exact loop only restores
// after each instruction. A call that starts with either wrong goes to the exact loop.
void a_wrong_vf0_or_vi0_goes_to_the_exact_loop(Rng &g) {
    g_case = "entry guard";
    program(0, {{lo15(IADDIU, 1, 0, 5), up1(ADD, 0xF, 0, 2, 4)}, {lom(LQ, 0xF, 6, 0, 2), up1(MULbc + 3, 0xF, 0, 2, 3)}, {lo1(IADD, 0, 0, 0, 2), up1(MADDbc + 3, 0xE, 0, 2, 5)}});
    for (int wrong = 0; wrong < 4; ++wrong) {
        fresh_registers(g, false);
        set_vf(2, 3.0f, -5.0f, 0.5f, 7.0f);
        if (wrong == 1) r.vf[0].w[3] = bits(2.0f);
        if (wrong == 2) r.vf[0].w[0] = bits(4.0f);
        if (wrong == 3) r.vi[0] = 9;
        const u64 guarded = vu::vu0_counters().guarded;
        const State exact = call(0);
        // Once for the fast loop's own run and once for its run inside the check.
        CHECK_EQ(vu::vu0_counters().guarded - guarded, wrong ? 2u : 0u);
        // The first instruction still saw the wrong value; after it the registers are right again.
        CHECK_EQ(exact.regs.vf[4].w[0], bits(wrong == 2 ? 7.0f : 3.0f));
        CHECK_EQ(exact.regs.vf[4].w[3], bits(wrong == 1 ? 9.0f : 8.0f));
        CHECK_EQ(exact.regs.vi[1], wrong == 3 ? 14 : 5);
        CHECK_EQ(exact.regs.vf[3].w[0], bits(3.0f));
        CHECK_EQ(exact.regs.vf[0].w[0], 0u);
        CHECK_EQ(exact.regs.vf[0].w[3], fp::kOne);
        CHECK_EQ(exact.regs.vi[0], 0);
    }
    settle();
}

// ---- generated programs ------------------------------------------------------------------------------

u32 some_lanes(Rng &g) {
    const u32 pick = g.below(100);
    if (pick < 25) return 0x8;
    if (pick < 65) return 0xE;
    if (pick < 90) return 0xF;
    return g.below(16);
}

// vi14 and vi15 belong to the jumps and the counted loops of a generated program: no instruction writes them.
u32 some_vi(Rng &g) { return g.below(14); }

bool is_branch(u32 lower) { return (lower >> 25) >= B && (lower >> 25) <= IBGEZ; }

// The uppers that VU0 code of a game mostly consists of: a quarter idle, the rest multiply-subtract,
// multiply, add and subtract, three in four of them with one lane of ft for all lanes, some into ACC.
u32 common_upper(Rng &g) {
    const u32 dest = some_lanes(g), fs = g.below(32), fd = g.chance(100) ? 0u : g.below(32), form = g.below(100);
    u32 ft = g.below(32), lane = g.below(4);
    if (g.chance(60)) {   // the w of vf0: a product with 1.0
        ft = 0;
        lane = 3;
    }
    const bool to_acc = g.chance(250);
    const auto arithmetic = [&](u32 one_lane, u32 vector, u32 q, u32 i) {
        const u32 code = form < 75 ? one_lane + lane : form < 96 ? vector : form < 98 ? q : i;
        return to_acc ? up2(code, dest, ft, fs) : up1(code, dest, ft, fs, fd);
    };
    const u32 pick = g.below(1000);
    if (pick < 270) return kUpperNop;
    if (pick < 490) return arithmetic(MSUBbc, MSUB, MSUBq, MSUBi);
    if (pick < 650) return arithmetic(MULbc, MUL, MULq, MULi);
    if (pick < 720) return arithmetic(MADDbc, MADD, MADDq, MADDi);
    if (pick < 735) return g.chance(500) ? up2(OPMULA, dest, ft, fs) : up1(OPMSUB, dest, ft, fs, fd);
    if (pick < 870) return arithmetic(ADDbc, ADD, ADDq, ADDi);
    if (pick < 980) return arithmetic(SUBbc, SUB, SUBq, SUBi);
    if (pick < 990) return up2((g.chance(800) ? ITOF0 : FTOI0) + g.below(4), dest, g.below(32), fs);
    if (pick < 995) return up1(form < 75 ? MAXbc + lane : form < 96 ? MAX : MAXi, dest, ft, fs, fd);
    return up1(form < 75 ? MINIbc + lane : form < 96 ? MINI : MINIi, dest, ft, fs, fd);
}

// The lowers that go with them: half idle, then flag tests, integer arithmetic, loads, stores and branches.
u32 common_lower(Rng &g, bool may_branch) {
    const u32 it = some_vi(g), is = some_vi(g), id = some_vi(g), ft = g.below(32), fs = g.below(32), dest = some_lanes(g), ahead = 1 + g.below(6);
    const u32 pick = g.below(1000);
    if (pick < 450) return kLowerNop;
    if (pick < 540) return lom(FMAND, 0, it, is, 0);
    if (pick < 555) return lom(FMOR, 0, it, is, 0);
    if (pick < 640) return lo1(IAND, 0, it, is, id);
    if (pick < 710) return lo1(IOR, 0, it, is, id);
    if (pick < 790) return lo15(IADDIU, it, is, g.below(16));
    if (pick < 810) return lo15(ISUBIU, it, is, g.below(16));
    if (pick < 830) return lo1(IADD, 0, it, is, id);
    if (pick < 880) return lom(LQ, dest, ft, is, g.below(64));
    if (pick < 895) return lom(SQ, dest, it, fs, g.below(64));
    if (pick < 903) return lom(ILW, 8u >> g.below(4), it, is, g.below(64));
    if (pick < 906) return lom(ISW, dest, it, is, g.below(64));
    if (pick < 966) {
        if (!may_branch) return kLowerNop;
        if (pick < 936) return lom(IBNE, 0, it, is, ahead);
        if (pick < 946) return lom(B, 0, 0, 0, ahead);
        if (pick < 955) return lom(IBEQ, 0, it, is, ahead);
        if (pick < 962) return lom(IBLEZ, 0, 0, is, ahead);
        return lom(IBGTZ, 0, 0, is, ahead);
    }
    if (pick < 978) return lo2(MR32, dest, ft, fs);
    if (pick < 981) return lo2(MFIR, dest, ft, is);
    if (pick < 983) return lo2(MTIR, g.below(4), it, fs);
    if (pick < 988) return lo2(RSQRT, g.below(16), ft, fs);
    if (pick < 993) return lo2(DIV, g.below(16), ft, fs);
    if (pick < 996) return lo2(SQRT, g.below(16), ft, fs);
    if (pick < 998) return lo2(WAITQ, 0, 0, 0);
    return lo2(MOVE, dest, ft, fs);
}

// Any upper code of either table, assigned or not.
u32 any_upper(Rng &g) {
    if (g.chance(400)) return common_upper(g);
    const u32 dest = g.below(16), ft = g.below(32), fs = g.below(32);
    return g.chance(500) ? up1(g.below(0x3C), dest, ft, fs, g.below(32)) : up2(g.below(128), dest, ft, fs);
}

// Any lower: every instruction the loops know, with its fields at random, and some they do not know.
// Branches go forward; the jumps through a register go through vi14.
u32 any_lower(Rng &g, bool may_branch, bool may_jump) {
    if (g.chance(400)) return common_lower(g, may_branch);
    const u32 dest = g.below(16), it = some_vi(g), is = some_vi(g), id = some_vi(g), ft = g.below(32), fs = g.below(32), ahead = 1 + g.below(6);
    static const u32 kNoSuchIndex[] = {0x00, 0x2F, 0x32, 0x33, 0x44, 0x50, 0x6D, 0x7F}, kNoSuchOpcode[] = {0x02, 0x0A, 0x19, 0x1D, 0x22, 0x2A, 0x30, 0x3F, 0x41, 0x55, 0x7F};
    switch (g.below(60)) {
    case 0: return lo1(IADD, dest, it, is, id);
    case 1: return lo1(ISUB, dest, it, is, id);
    case 2: return lo1(IADDI, dest, it, is, g.below(32));
    case 3: return lo1(IAND, dest, it, is, id);
    case 4: return lo1(IOR, dest, it, is, id);
    case 5: return lo1(g.below(0x30), dest, it, is, id);   // no such instruction
    case 6: return lo2(MOVE, dest, ft, fs);
    case 7: return lo2(MR32, dest, ft, fs);
    case 8: return lo2(LQI, dest, ft, is);
    case 9: return lo2(SQI, dest, it, fs);
    case 10: return lo2(LQD, dest, ft, is);
    case 11: return lo2(SQD, dest, it, fs);
    case 12: return lo2(DIV, dest, ft, fs);
    case 13: return lo2(SQRT, dest, ft, fs);
    case 14: return lo2(RSQRT, dest, ft, fs);
    case 15: return lo2(WAITQ, dest, ft, fs);
    case 16: return lo2(MTIR, dest, it, fs);
    case 17: return lo2(MFIR, dest, ft, is);
    case 18: return lo2(ILWR, dest, it, is);
    case 19: return lo2(ISWR, dest, it, is);
    case 20: return lo2(RNEXT, dest, ft, fs);
    case 21: return lo2(RGET, dest, ft, fs);
    case 22: return lo2(RINIT, dest, ft, fs);
    case 23: return lo2(RXOR, dest, ft, fs);
    case 24: return lo2(MFP, dest, ft, fs);
    case 25: return lo2(XTOP, dest, it, is);
    case 26: return lo2(XITOP, dest, it, is);
    case 27: return lo2(XGKICK, dest, it, is);
    case 28: case 29: return lo2(ESADD + g.below(15), dest, ft, fs);   // the instructions that leave a result in P, and WAITP
    case 30: return lo2(kNoSuchIndex[g.below(8)], dest, ft, fs);
    case 31: return lom(LQ, dest, ft, is, g.bits());
    case 32: return lom(SQ, dest, it, fs, g.bits());
    case 33: return lom(ILW, dest, it, is, g.bits());
    case 34: return lom(ISW, dest, it, is, g.bits());
    case 35: return lo15(IADDIU, it, is, g.bits() & 0x7FFFu);
    case 36: return lo15(ISUBIU, it, is, g.bits() & 0x7FFFu);
    case 37: return lo24(FCEQ, g.bits());
    case 38: return lo24(FCSET, g.bits());
    case 39: return lo24(FCAND, g.bits());
    case 40: return lo24(FCOR, g.bits());
    case 41: return lo12(FSEQ, it, g.bits() & 0xFFFu);
    case 42: return lo12(FSSET, it, g.bits() & 0xFFFu);
    case 43: return lo12(FSAND, it, g.bits() & 0xFFFu);
    case 44: return lo12(FSOR, it, g.bits() & 0xFFFu);
    case 45: return lom(FMEQ, 0, it, is, 0);
    case 46: return lom(FMAND, 0, it, is, 0);
    case 47: return lom(FMOR, 0, it, is, 0);
    case 48: return lom(FCGET, 0, it, 0, 0);
    case 49: return (kNoSuchOpcode[g.below(11)] << 25) | (g.bits() & 0x01FFFFFFu);
    default: break;
    }
    if (!may_branch) return kLowerNop;
    switch (g.below(10)) {
    case 0: return lom(B, 0, 0, 0, ahead);
    case 1: return lom(BAL, 0, it, 0, ahead);
    case 2: return may_jump ? lom(JR, 0, 0, 14, 0) : lom(B, 0, 0, 0, ahead);
    case 3: return may_jump ? lom(JALR, 0, it, 14, 0) : lom(BAL, 0, it, 0, ahead);
    case 4: return lom(IBEQ, 0, it, is, ahead);
    case 5: return lom(IBNE, 0, it, is, ahead);
    case 6: return lom(IBLTZ, 0, 0, is, ahead);
    case 7: return lom(IBGTZ, 0, 0, is, ahead);
    case 8: return lom(IBLEZ, 0, 0, is, ahead);
    default: return lom(IBGEZ, 0, 0, is, ahead);
    }
}

// A program of `length` instructions at slot `start`. It always ends: branches go forward, the jumps go
// to the one slot vi14 names (position jump_at, and only instructions before it jump), and the only way
// back is a counted loop, IBGTZ on vi15 with the decrement in its delay slot. vi15 starts at three or
// less and every pass through such a branch counts it down. With counted loops no branch sits in another
// branch's delay slot, where it would send the decrement elsewhere.
void generate(Rng &g, u32 start, u32 length, bool every, u32 loops, u32 jump_at) {
    std::vector<Pair> pairs(length);
    bool after_branch = false;
    for (u32 i = 0; i < length; ++i) {
        const bool may_branch = !(loops && after_branch);
        Pair &p = pairs[i];
        p.upper = every ? any_upper(g) : common_upper(g);
        p.lower = every ? any_lower(g, may_branch, i + 1 < jump_at) : common_lower(g, may_branch);
        after_branch = is_branch(p.lower);
        if (g.chance(every ? 30 : 15)) {   // the lower word is a number for the I register
            p.upper |= kIbit;
            p.lower = every ? edge(g) : typical(g);
            after_branch = false;
        }
        if (every && g.chance(15)) p.upper |= kEnd;
        if (every && g.chance(20)) p.upper |= (1u + g.below(7)) << 27;   // the M, D and T bits, which neither loop acts on
    }
    for (u32 n = 0; n < loops; ++n) {
        // Each loop in its own half of the program: the instruction before the branch, the branch and
        // its delay slot.
        const u32 half = length / 2, at = n * half + 2 + g.below(half - 4), back = 1 + g.below(4);
        pairs[at - 1].lower = kLowerNop;
        pairs[at].lower = branch(IBGTZ, 0, 15, static_cast<int>(at), static_cast<int>(at > back ? at - back : 0));
        pairs[at + 1].lower = lo15(ISUBIU, 15, 15, 1);
        pairs[at].upper &= ~kIbit;
        pairs[at + 1].upper &= ~kIbit;
    }
    clear_code();
    for (u32 i = 0; i < length; ++i) put(start + i, pairs[i].lower, pairs[i].upper);
    vu::code_changed(0);
}

// One generated program and the registers for a call of it.
struct Generated {
    u32 start, length, jump_at;
};

Generated next_program(Rng &g, bool every) {
    Generated p;
    p.start = g.below(512);
    p.length = 12 + g.below(60);
    p.jump_at = p.length - 1 - g.below(3);
    generate(g, p.start, p.length, every, g.below(3), p.jump_at);
    return p;
}

void registers_for(Rng &g, const Generated &p, bool edges) {
    fresh_registers(g, edges);
    r.vi[14] = static_cast<std::uint16_t>((p.start + p.jump_at) & 511u);
    r.vi[15] = static_cast<std::uint16_t>(g.below(4));
}

// Half the programs from the common mix, half from every opcode; each called with three sets of registers,
// ordinary and edge values, the third time entered somewhere inside.
void generated_programs(Rng &g, u64 programs) {
    const u64 slow_before = vu::vu0_counters().slow;
    u64 calls = 0, steps = 0;
    for (int every = 0; every < 2; ++every) {
        g_case = every ? "generated, every opcode" : "generated, common mix";
        for (u64 n = 0; n < programs / 2; ++n) {
            const Generated p = next_program(g, every);
            for (int set = 0; set < 3; ++set) {
                registers_for(g, p, set == 1 || (every && set == 2));
                call(p.start + (set == 2 ? g.below(p.length) : 0));
            }
        }
        const Tally t = settle();
        calls += t.calls;
        steps += t.steps;
    }
    CHECK_EQ(calls, programs / 2 * 6);
    CHECK(vu::vu0_counters().slow > slow_before);
    std::printf("test_vu0_loop: %llu generated calls, %llu instructions each way\n", static_cast<unsigned long long>(calls), static_cast<unsigned long long>(steps));
}

// ---- code images -------------------------------------------------------------------------------------

// The fast loop keeps the decoded form of the last 16 code images it ran, found again by their content
// when the code changes. Twenty programs, all entered at slot 0, that differ in one instruction.
void load_numbered(u32 n) { program(0, {{lo15(IADDIU, 1, 0, n + 1), up1(ADD, 0xF, 2, 1, 5)}, {lo15(IADDIU, 2, 1, 1), up1(MULbc + (n & 3), 0xF, 2, 1, 6)}}); }

void code_images(Rng &g) {
    g_case = "code images";
    fresh_registers(g, false);
    const vu::Vu0Counters before = vu::vu0_counters();
    // Never seen: each is decoded once. A call() runs the fast loop twice, and only the first looks.
    for (u32 n = 0; n < 20; ++n) {
        load_numbered(n);
        call(0);
        CHECK_EQ(r.vi[1], n + 1);
    }
    CHECK_EQ(vu::vu0_counters().images - before.images, 20u);
    // The same again in the same order: each has just been pushed out by the 16 after it.
    for (u32 n = 0; n < 20; ++n) {
        load_numbered(n);
        call(0);
        CHECK_EQ(r.vi[1], n + 1);
    }
    CHECK_EQ(vu::vu0_counters().images - before.images, 40u);
    // The 16 most recent, newest first: all still there.
    for (u32 n = 20; n-- > 4;) {
        load_numbered(n);
        call(0);
        CHECK_EQ(r.vi[1], n + 1);
    }
    CHECK_EQ(vu::vu0_counters().images - before.images, 40u);
    CHECK_EQ(vu::vu0_counters().lookups - before.lookups, 56u);
    settle();

    // The code changes and nobody tells the unit: the fast loop still runs what it decoded. The check
    // sees that the image is not the code in memory, and keeps the exact loop's outcome.
    load_numbered(3);
    call(0);
    settle();
    put(0, lo15(IADDIU, 1, 0, 100), up1(SUB, 0xF, 2, 1, 5));
    const State entry = take();
    vu::Vu0Counters counted = vu::vu0_counters();
    run(Vu0Mode::Check, 0);
    CHECK_EQ(vu::vu0_counters().stale - counted.stale, 1u);
    CHECK_EQ(vu::vu0_counters().differ - counted.differ, 1u);
    CHECK_EQ(r.vi[1], 100);
    // Told, the unit decodes the new code and the check is quiet again.
    vu::code_changed(0);
    restore(entry);
    counted = vu::vu0_counters();
    call(0);
    CHECK_EQ(r.vi[1], 100);
    CHECK_EQ(vu::vu0_counters().stale, counted.stale);
    settle();
    // A silent change in a slot this call never reaches leaves both outcomes equal. It is reported all the
    // same: the next call may reach it.
    put(100, lo15(IADDIU, 1, 0, 7), kUpperNop);
    counted = vu::vu0_counters();
    run(Vu0Mode::Check, 0);
    CHECK_EQ(vu::vu0_counters().stale - counted.stale, 1u);
    CHECK_EQ(vu::vu0_counters().differ - counted.differ, 1u);
    vu::code_changed(0);
}

// ---- the check itself --------------------------------------------------------------------------------

int g_damage = 0;           // what the hook damages: 1 one bit of a register lane, 2 one bit of data memory, 3 one MAC flag
u64 g_damage_calls = 0;

void apply_damage(ps2::Vu0Regs &regs, std::uint8_t *mem) {
    if (g_damage == 1) regs.vf[9].w[2] ^= 0x00000001u;
    if (g_damage == 2) mem[0x123] ^= 0x80;
    if (g_damage == 3) regs.mac ^= 0x0100u;
}

// Damages the fast loop's outcome on every third call.
void damage_hook(ps2::Vu0Regs &regs, std::uint8_t *mem) {
    if (++g_damage_calls % 3 == 0) apply_damage(regs, mem);
}

// One wrong bit anywhere in the fast loop's outcome is reported, on the calls that have it and on no
// others. The check keeps the exact loop's outcome; asked to keep the fast loop's, it keeps the damage.
void the_check_reports_one_wrong_bit(Rng &g) {
    program(0, {{kLowerNop, up1(ADD, 0xF, 2, 1, 5)}, {lom(SQ, 0xF, 3, 5, 0), up1(MUL, 0xF, 2, 1, 6)}});
    vu::set_vu0_tamper(damage_hook);
    for (g_damage = 1; g_damage <= 3; ++g_damage)
        for (int live = 0; live < 2; ++live) {
            g_damage_calls = 0;
            for (int n = 1; n <= 9; ++n) {
                fresh_registers(g, false);
                const State entry = take();
                run(Vu0Mode::Exact, 0);
                State expect = take();
                restore(entry);
                const u64 differ = vu::vu0_counters().differ;
                run(live ? Vu0Mode::CheckLive : Vu0Mode::Check, 0);
                const bool damaged = n % 3 == 0;
                CHECK_EQ(vu::vu0_counters().differ - differ, damaged ? 1u : 0u);
                if (live && damaged) apply_damage(expect.regs, expect.mem);
                CHECK(same(take(), expect));
            }
        }
    vu::set_vu0_tamper(nullptr);
    g_damage = 0;
    CHECK_EQ(ps2::g_cpu.budget, kBudget);
}

// ---- whole runs --------------------------------------------------------------------------------------

// A fixed chain of calls through one loop only, each call starting from what the one before left, so a
// difference would grow instead of being put right by the next comparison. Returns a hash of every state
// on the way. Common-mix programs only: nothing in them goes through the host's math library, so the hash
// is the same from every build. Without a mode the calls go as the game's do, in whatever mode the
// interpreter has.
u64 chain(std::optional<Vu0Mode> mode) {
    Rng g{0x5EEDF00D5EEDF00Dull};
    for (u32 &v : g_recent) v = typical(g);
    u64 hash = 0;
    for (int n = 0; n < 300; ++n) {
        const Generated p = next_program(g, false);
        registers_for(g, p, n % 4 == 3);
        for (int again = 0; again < 4; ++again) {
            const u32 slot = p.start + (again ? g.below(p.length) : 0);
            if (mode) run(*mode, slot);
            else ps2::vcallms(ps2::g_cpu, slot * 8);
            const State s = take();
            const auto *bytes = reinterpret_cast<const std::uint8_t *>(&s.regs);
            for (std::size_t i = 0; i < kRegBytes; ++i) hash = (hash ^ bytes[i]) * 0x100000001B3ull;
            for (std::size_t i = 0; i < sizeof s.mem; ++i) hash = (hash ^ s.mem[i]) * 0x100000001B3ull;
        }
    }
    return hash;
}

void both_loops_give_one_hash() {
    const u64 start = vu::vu0_counters().steps;
    const u64 exact = chain(Vu0Mode::Exact);
    const u64 exact_steps = vu::vu0_counters().steps - start;
    const u64 fast = chain(Vu0Mode::Fast);
    const u64 fast_steps = vu::vu0_counters().steps - start - exact_steps;
    CHECK_EQ(fast, exact);
    CHECK_EQ(fast_steps, exact_steps);
    std::printf("test_vu0_loop: hash %016llX after 1200 calls, %llu instructions\n", static_cast<unsigned long long>(fast), static_cast<unsigned long long>(fast_steps));
}

#if defined(__SSE2__)
// The fast loop's arithmetic needs a host that keeps denormals. Asked for on a thread that flushes them to
// zero, the fast loop and the check are refused and the exact loop runs.
void a_host_that_flushes_denormals_gets_the_exact_loop() {
    const unsigned csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040u);
    vu::set_vu0_mode(Vu0Mode::Fast);
    const Vu0Mode fast = vu::vu0_mode();
    vu::set_vu0_mode(Vu0Mode::Check);
    const Vu0Mode check = vu::vu0_mode();
    _mm_setcsr(csr);
    CHECK(fast == Vu0Mode::Exact);
    CHECK(check == Vu0Mode::Exact);
    vu::set_vu0_mode(Vu0Mode::Fast);
    CHECK(vu::vu0_mode() == Vu0Mode::Fast);
}
#endif

// ---- the mode from the environment -------------------------------------------------------------------

// ps2run and the page never set a mode. The interpreter reads it from the environment at its first VU0
// call, and the page's environment is the PS2_ parameters of its address. These are the ways the
// environment names a mode; the check wins over the switches for the exact loop.
const struct {
    const char *mode, *variables;
} kEnvironments[] = {{"fast", ""},
                     {"exact", "PS2_VU0_EXACT=1"},
                     {"exact", "PS2_VU_EXACT=1"},
                     {"check", "PS2_VU0_CHECK=1"},
                     {"live", "PS2_VU0_CHECK=live"},
                     {"check", "PS2_VU0_CHECK=1 PS2_VU_EXACT=1"},
                     {"live", "PS2_VU0_CHECK=live PS2_VU0_EXACT=1"}};

// One of those runs: the caller has set the variables, and nothing before the fixed chain of calls sets a
// mode. What must show is the named mode and what only that mode does: a check compares every call, the
// exact loop decodes no code image, and a damaged outcome of the fast loop is reported by both checks and
// kept by the one that keeps the fast loop's outcome.
int mode_from_the_environment(const char *name) {
    static const struct {
        const char *name;
        Vu0Mode mode;
    } kModes[] = {{"fast", Vu0Mode::Fast}, {"exact", Vu0Mode::Exact}, {"check", Vu0Mode::Check}, {"live", Vu0Mode::CheckLive}};
    std::optional<Vu0Mode> named;
    for (const auto &m : kModes)
        if (!std::strcmp(name, m.name)) named = m.mode;
    if (!named) {
        std::printf("test_vu0_loop: --environment takes fast, exact, check or live\n");
        return 2;
    }
    const bool checks = *named == Vu0Mode::Check || *named == Vu0Mode::CheckLive;

    const u64 hash = chain(std::nullopt);
    const vu::Vu0Counters counted = vu::vu0_counters();
    CHECK(vu::vu0_mode() == *named);
    CHECK_EQ(counted.calls, 1200u);
    CHECK_EQ(counted.compared, checks ? 1200u : 0u);
    CHECK_EQ(counted.differ, 0u);
    CHECK_EQ(counted.images != 0, *named != Vu0Mode::Exact);
    CHECK_EQ(ps2::g_cpu.budget, kBudget);

    // One more call in that mode, with one bit of the fast loop's outcome damaged.
    Rng g{0x0DDBA11C0FFEEull};
    program(0, {{kLowerNop, up1(ADD, 0xF, 2, 1, 5)}, {lom(SQ, 0xF, 3, 5, 0), up1(MUL, 0xF, 2, 1, 6)}});
    fresh_registers(g, false);
    const State entry = take();
    g_damage = 1;
    vu::set_vu0_tamper(apply_damage);
    ps2::vcallms(ps2::g_cpu, 0);
    vu::set_vu0_tamper(nullptr);
    const State left = take();
    CHECK_EQ(vu::vu0_counters().differ, checks ? 1u : 0u);

    // From here the modes are set by hand, as in the rest of this test: the exact loop's outcome of that
    // call, and the chain again through the exact loop.
    restore(entry);
    run(Vu0Mode::Exact, 0);
    State expect = take();
    if (*named == Vu0Mode::CheckLive) apply_damage(expect.regs, expect.mem);
    CHECK(same(left, expect));
    g_damage = 0;
    CHECK_EQ(hash, chain(Vu0Mode::Exact));
    return check::done((std::string("test_vu0_loop --environment ") + name).c_str());
}

#if !defined(__EMSCRIPTEN__)
// Every one of those runs, each in a process of its own started from this one with nothing but its
// variables in the environment. (A WebAssembly program cannot start another; tests/wasm/run.sh does it.)
void every_mode_the_environment_names(const char *program) {
    std::string quoted = "'";
    for (const char *c = program; *c; ++c) quoted += *c == '\'' ? std::string("'\\''") : std::string(1, *c);
    quoted += "'";
    std::fflush(stdout);
    for (const auto &e : kEnvironments) CHECK_EQ(std::system(("env -i " + std::string(e.variables) + " " + quoted + " --environment " + e.mode).c_str()), 0);
}
#endif

}

int main(int argc, char **argv) {
    u64 scale = 1, seed = 1;
    bool keep_stderr = false;
    const char *environment = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--stderr")) keep_stderr = true;
        else if (!std::strcmp(argv[i], "--environment") && i + 1 < argc) environment = argv[++i];
        else if (!std::strcmp(argv[i], "--environments")) {
            for (const auto &e : kEnvironments) std::printf("%s %s\n", e.mode, e.variables);
            return 0;
        } else scale = std::strtoull(argv[i], nullptr, 10);
    }
    if (!scale) scale = 1;
    // The interpreter reports on stderr every instruction it does not know, every program that does not end
    // and every difference its check finds. The cases here provoke all three on purpose and count them
    // through the interpreter's counters, so those lines are dropped unless --stderr asks for them.
    if (!keep_stderr && !std::freopen("/dev/null", "w", stderr)) return 2;
    ps2::g_cpu.budget = kBudget;
    if (environment) return mode_from_the_environment(environment);
    Rng g{seed * 0x9E3779B97F4A7C15ull + 1};
    for (u32 &v : g_recent) v = typical(g);

    every_upper_under_every_lane_mask(g);
    flag_readers_at_every_distance(g);
    q_readers_at_every_distance(g);
    order_within_an_instruction(g);
    the_ends_of_a_program(g);
    decoded_lowers_at_their_edges(g);
    xgkick_is_sent_once(g);
    a_wrong_vf0_or_vi0_goes_to_the_exact_loop(g);
    generated_programs(g, 2000 * scale);
    code_images(g);
    the_check_reports_one_wrong_bit(g);
    both_loops_give_one_hash();
#if defined(__SSE2__)
    a_host_that_flushes_denormals_gets_the_exact_loop();
#endif
#if !defined(__EMSCRIPTEN__)
    every_mode_the_environment_names(argv[0]);
#endif

    const vu::Vu0Counters &c = vu::vu0_counters();
    std::printf("test_vu0_loop: %llu calls compared by the check mode, %llu instructions through the model, %llu calls left to the exact loop, %llu code images\n",
                static_cast<unsigned long long>(c.compared), static_cast<unsigned long long>(c.slow), static_cast<unsigned long long>(c.guarded), static_cast<unsigned long long>(c.images));
    return check::done("test_vu0_loop");
}
