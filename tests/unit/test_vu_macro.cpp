// The EE's COP2 macro-mode arithmetic (ps2/vu_macro.hpp: VADD, VMADDbc, VMULA, VOPMSUB and the rest, as the
// recompiled code calls them) against the lane-by-lane integer model it replaced, kept here as the reference:
// each lane of dest computed with ps2::fp::add, sub and mul (ps2/fpu.hpp), the other lanes and vf0 left
// alone. The four-lane forms of ps2/vu_exact.hpp are tested on their own in test_vu_exact.cpp; this test is
// about the wiring: every dest mask, a destination that is also a source, vf0 as the destination, the
// accumulator forms, a broadcast operand, and the fall back to the model for a lane host floats cannot
// compute. The whole register file is compared bit for bit after every instruction.
//
// usage: test_vu_macro [thousands of instructions per operation, default 100]
// The "hash" line comes from a fixed stream and must be the same from every build of this test, native
// and WebAssembly.

#include "check.hpp"

#ifndef __clang__
int main() {
    std::puts("test_vu_macro: skipped (ps2/vu_exact.hpp needs clang)");
    return 0;
}
#else
#include "ps2/cpu.hpp"
#include "ps2/fpu.hpp"
#include "ps2/vu.hpp"
#include "ps2/vu_macro.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace fp = ps2::fp;
namespace vu = ps2::vu;
using ps2::Reg128;
using ps2::Vu0Regs;

namespace {

// The model: what ps2/vu.hpp computed before the four-lane forms, lane by lane.
namespace model {
using vu::apply;
using vu::apply_acc;
void add(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, unsigned ft) { apply(v, dest, fd, [&](int i) { return fp::add(v.vf[fs].w[i], v.vf[ft].w[i]); }); }
void addbc(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, std::uint32_t t) { apply(v, dest, fd, [&](int i) { return fp::add(v.vf[fs].w[i], t); }); }
void sub(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, unsigned ft) { apply(v, dest, fd, [&](int i) { return fp::sub(v.vf[fs].w[i], v.vf[ft].w[i]); }); }
void subbc(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, std::uint32_t t) { apply(v, dest, fd, [&](int i) { return fp::sub(v.vf[fs].w[i], t); }); }
void mul(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, unsigned ft) { apply(v, dest, fd, [&](int i) { return fp::mul(v.vf[fs].w[i], v.vf[ft].w[i]); }); }
void mulbc(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, std::uint32_t t) { apply(v, dest, fd, [&](int i) { return fp::mul(v.vf[fs].w[i], t); }); }
void madd(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, unsigned ft) { apply(v, dest, fd, [&](int i) { return fp::add(v.acc.w[i], fp::mul(v.vf[fs].w[i], v.vf[ft].w[i])); }); }
void maddbc(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, std::uint32_t t) { apply(v, dest, fd, [&](int i) { return fp::add(v.acc.w[i], fp::mul(v.vf[fs].w[i], t)); }); }
void msub(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, unsigned ft) { apply(v, dest, fd, [&](int i) { return fp::sub(v.acc.w[i], fp::mul(v.vf[fs].w[i], v.vf[ft].w[i])); }); }
void msubbc(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, std::uint32_t t) { apply(v, dest, fd, [&](int i) { return fp::sub(v.acc.w[i], fp::mul(v.vf[fs].w[i], t)); }); }
void max(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, unsigned ft) { apply(v, dest, fd, [&](int i) { return fp::max(v.vf[fs].w[i], v.vf[ft].w[i]); }); }
void maxbc(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, std::uint32_t t) { apply(v, dest, fd, [&](int i) { return fp::max(v.vf[fs].w[i], t); }); }
void mini(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, unsigned ft) { apply(v, dest, fd, [&](int i) { return fp::min(v.vf[fs].w[i], v.vf[ft].w[i]); }); }
void minibc(Vu0Regs &v, unsigned dest, unsigned fd, unsigned fs, std::uint32_t t) { apply(v, dest, fd, [&](int i) { return fp::min(v.vf[fs].w[i], t); }); }
void adda(Vu0Regs &v, unsigned dest, unsigned fs, unsigned ft) { apply_acc(v, dest, [&](int i) { return fp::add(v.vf[fs].w[i], v.vf[ft].w[i]); }); }
void addabc(Vu0Regs &v, unsigned dest, unsigned fs, std::uint32_t t) { apply_acc(v, dest, [&](int i) { return fp::add(v.vf[fs].w[i], t); }); }
void suba(Vu0Regs &v, unsigned dest, unsigned fs, unsigned ft) { apply_acc(v, dest, [&](int i) { return fp::sub(v.vf[fs].w[i], v.vf[ft].w[i]); }); }
void subabc(Vu0Regs &v, unsigned dest, unsigned fs, std::uint32_t t) { apply_acc(v, dest, [&](int i) { return fp::sub(v.vf[fs].w[i], t); }); }
void mula(Vu0Regs &v, unsigned dest, unsigned fs, unsigned ft) { apply_acc(v, dest, [&](int i) { return fp::mul(v.vf[fs].w[i], v.vf[ft].w[i]); }); }
void mulabc(Vu0Regs &v, unsigned dest, unsigned fs, std::uint32_t t) { apply_acc(v, dest, [&](int i) { return fp::mul(v.vf[fs].w[i], t); }); }
void madda(Vu0Regs &v, unsigned dest, unsigned fs, unsigned ft) { apply_acc(v, dest, [&](int i) { return fp::add(v.acc.w[i], fp::mul(v.vf[fs].w[i], v.vf[ft].w[i])); }); }
void maddabc(Vu0Regs &v, unsigned dest, unsigned fs, std::uint32_t t) { apply_acc(v, dest, [&](int i) { return fp::add(v.acc.w[i], fp::mul(v.vf[fs].w[i], t)); }); }
void msuba(Vu0Regs &v, unsigned dest, unsigned fs, unsigned ft) { apply_acc(v, dest, [&](int i) { return fp::sub(v.acc.w[i], fp::mul(v.vf[fs].w[i], v.vf[ft].w[i])); }); }
void msubabc(Vu0Regs &v, unsigned dest, unsigned fs, std::uint32_t t) { apply_acc(v, dest, [&](int i) { return fp::sub(v.acc.w[i], fp::mul(v.vf[fs].w[i], t)); }); }
void opmula(Vu0Regs &v, unsigned fs, unsigned ft) {
    const Reg128 s = v.vf[fs], t = v.vf[ft];
    v.acc.w[0] = fp::mul(s.w[1], t.w[2]);
    v.acc.w[1] = fp::mul(s.w[2], t.w[0]);
    v.acc.w[2] = fp::mul(s.w[0], t.w[1]);
}
void opmsub(Vu0Regs &v, unsigned fd, unsigned fs, unsigned ft) {
    const Reg128 s = v.vf[fs], t = v.vf[ft];
    Reg128 r = v.vf[fd];
    r.w[0] = fp::sub(v.acc.w[0], fp::mul(s.w[1], t.w[2]));
    r.w[1] = fp::sub(v.acc.w[1], fp::mul(s.w[2], t.w[0]));
    r.w[2] = fp::sub(v.acc.w[2], fp::mul(s.w[0], t.w[1]));
    if (fd) v.vf[fd] = r;
}
}

// One instruction: the registers, dest, fd, fs, ft and the broadcast value.
using Fn = void (*)(Vu0Regs &, unsigned dest, unsigned fd, unsigned fs, unsigned ft, std::uint32_t t);

struct Operation {
    const char *name;
    Fn now, before;
    std::uint64_t wrong = 0;
};

#define VEC(op) {#op, [](Vu0Regs &v, unsigned d, unsigned fd, unsigned fs, unsigned ft, std::uint32_t) { vu::op(v, d, fd, fs, ft); }, \
                      [](Vu0Regs &v, unsigned d, unsigned fd, unsigned fs, unsigned ft, std::uint32_t) { model::op(v, d, fd, fs, ft); }}
#define VBC(op) {#op, [](Vu0Regs &v, unsigned d, unsigned fd, unsigned fs, unsigned, std::uint32_t t) { vu::op(v, d, fd, fs, t); }, \
                      [](Vu0Regs &v, unsigned d, unsigned fd, unsigned fs, unsigned, std::uint32_t t) { model::op(v, d, fd, fs, t); }}
#define ACC(op) {#op, [](Vu0Regs &v, unsigned d, unsigned, unsigned fs, unsigned ft, std::uint32_t) { vu::op(v, d, fs, ft); }, \
                      [](Vu0Regs &v, unsigned d, unsigned, unsigned fs, unsigned ft, std::uint32_t) { model::op(v, d, fs, ft); }}
#define ABC(op) {#op, [](Vu0Regs &v, unsigned d, unsigned, unsigned fs, unsigned, std::uint32_t t) { vu::op(v, d, fs, t); }, \
                      [](Vu0Regs &v, unsigned d, unsigned, unsigned fs, unsigned, std::uint32_t t) { model::op(v, d, fs, t); }}

Operation g_operations[] = {
    VEC(add), VBC(addbc), VEC(sub), VBC(subbc), VEC(mul), VBC(mulbc), VEC(madd), VBC(maddbc), VEC(msub), VBC(msubbc),
    VEC(max), VBC(maxbc), VEC(mini), VBC(minibc),
    ACC(adda), ABC(addabc), ACC(suba), ABC(subabc), ACC(mula), ABC(mulabc), ACC(madda), ABC(maddabc), ACC(msuba), ABC(msubabc),
    {"opmula", [](Vu0Regs &v, unsigned, unsigned, unsigned fs, unsigned ft, std::uint32_t) { vu::opmula(v, fs, ft); },
               [](Vu0Regs &v, unsigned, unsigned, unsigned fs, unsigned ft, std::uint32_t) { model::opmula(v, fs, ft); }},
    {"opmsub", [](Vu0Regs &v, unsigned, unsigned fd, unsigned fs, unsigned ft, std::uint32_t) { vu::opmsub(v, fd, fs, ft); },
               [](Vu0Regs &v, unsigned, unsigned fd, unsigned fs, unsigned ft, std::uint32_t) { model::opmsub(v, fd, fs, ft); }},
};

std::uint64_t g_state = 1;
std::uint32_t next() {
    g_state = g_state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<std::uint32_t>(g_state >> 32);
}

std::uint32_t make(std::uint32_t sign, std::uint32_t exponent, std::uint32_t mantissa) {
    return ((sign & 1u) << 31) | ((exponent & 0xFFu) << 23) | (mantissa & 0x7FFFFFu);
}

// A register lane: mostly what a game computes with, some values at the ends of the range (exponents 0, 1,
// 254 and 255: the top binade is where the four-lane forms hand over to the model) and some any bits.
std::uint32_t value() {
    const std::uint32_t r = next();
    switch (r & 15u) {
    case 0: return next();
    case 1: {
        static const std::uint32_t ends[] = {0u, 1u, 2u, 126u, 127u, 128u, 253u, 254u, 255u};
        return make(r >> 31, ends[(r >> 4) % 9u], (r & 0x100u) ? next() : 0u);
    }
    case 2: return r & fp::kSign;
    default: return make(r >> 31, 100u + (r >> 4) % 56u, next());
    }
}

void fill(Vu0Regs &v) {
    std::memset(&v, 0, sizeof v);
    for (int r = 1; r < 32; ++r)
        for (int i = 0; i < 4; ++i) v.vf[r].w[i] = value();
    v.vf[0].w[3] = fp::kOne;
    for (int i = 0; i < 4; ++i) v.acc.w[i] = value();
    v.q = value();
    v.i = value();
}

std::uint64_t g_hash = 0;
std::uint64_t g_instructions = 0;

// One instruction on two copies of the same registers, one with each implementation; all of them must agree.
void one(Operation &op, Vu0Regs &state, unsigned dest, unsigned fd, unsigned fs, unsigned ft, std::uint32_t t) {
    Vu0Regs now = state, before = state;
    op.now(now, dest, fd, fs, ft, t);
    op.before(before, dest, fd, fs, ft, t);
    ++g_instructions;
    if (std::memcmp(&now, &before, sizeof now) != 0) {
        if (op.wrong++ < 3)
            std::printf("  %s dest %X fd %u fs %u ft %u t %08X: vf[fd] %08X %08X %08X %08X acc %08X %08X %08X %08X, the model %08X %08X %08X %08X acc %08X %08X %08X %08X\n",
                        op.name, dest, fd, fs, ft, t, now.vf[fd].w[0], now.vf[fd].w[1], now.vf[fd].w[2], now.vf[fd].w[3], now.acc.w[0], now.acc.w[1], now.acc.w[2],
                        now.acc.w[3], before.vf[fd].w[0], before.vf[fd].w[1], before.vf[fd].w[2], before.vf[fd].w[3], before.acc.w[0], before.acc.w[1],
                        before.acc.w[2], before.acc.w[3]);
    }
    for (const Reg128 &r : now.vf) g_hash = ((g_hash ^ r.d[0]) * 0x9E3779B97F4A7C15ull) ^ r.d[1];
    g_hash = ((g_hash ^ now.acc.d[0]) * 0x9E3779B97F4A7C15ull) ^ now.acc.d[1];
    g_hash ^= g_hash >> 29;
    state = now;
}

// Registers chosen to meet every way the fields can coincide: the destination vf0, a destination that is
// a source, both sources the same.
void every_dest_and_register_overlap() {
    static const unsigned regs[][3] = {{1, 2, 3}, {0, 2, 3}, {2, 2, 3}, {3, 2, 3}, {1, 2, 2}, {2, 2, 2}, {0, 0, 0}, {1, 0, 2}};
    Vu0Regs state;
    for (Operation &op : g_operations)
        for (unsigned dest = 0; dest < 16; ++dest)
            for (const auto &r : regs)
                for (int k = 0; k < 40; ++k) {
                    fill(state);
                    const std::uint32_t t = (k & 1) ? state.vf[r[2]].w[k % 4] : value();
                    one(op, state, dest, r[0], r[1], r[2], t);
                }
}

// The top binade in one lane: inside dest the instruction goes to the model, outside it the four-lane
// result is kept. Both must give the model's registers.
void a_lane_the_host_cannot_compute() {
    Vu0Regs state;
    for (Operation &op : g_operations)
        for (unsigned dest = 1; dest < 16; ++dest)
            for (int lane = 0; lane < 4; ++lane) {
                fill(state);
                state.vf[2].w[lane] = make(lane & 1, 255u, next());
                state.vf[3].w[lane] = make(lane >> 1, 254u, next());
                one(op, state, dest, 1, 2, 3, make(0, 255u, next()));
            }
}

// A stream of instructions on registers that carry their results on, as a program's would.
void a_running_stream(std::uint64_t count) {
    Vu0Regs state;
    fill(state);
    for (std::uint64_t n = 0; n < count; ++n) {
        Operation &op = g_operations[next() % (sizeof g_operations / sizeof g_operations[0])];
        const std::uint32_t r = next();
        const unsigned dest = 1u + r % 15u, fd = (r >> 4) & 7u, fs = (r >> 8) & 7u, ft = (r >> 12) & 7u;
        const std::uint32_t t = (r & 0x10000u) ? state.vf[ft].w[(r >> 17) & 3u] : (r & 0x20000u) ? state.q : value();
        if ((r >> 20) % 64u == 0) state.vf[(r >> 26) & 7u].w[(r >> 29) & 3u] = value();   // now and then a fresh value
        one(op, state, dest, fd, fs, ft, t);
        if (n % 4096u == 4095u) fill(state);
    }
}

}

int main(int argc, char **argv) {
    const std::uint64_t thousands = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 100;
    g_state = 0x5EED;
    every_dest_and_register_overlap();
    a_lane_the_host_cannot_compute();
    a_running_stream(thousands * 1000u * (sizeof g_operations / sizeof g_operations[0]));
    std::printf("test_vu_macro: hash %016llX after %llu instructions\n", static_cast<unsigned long long>(g_hash),
                static_cast<unsigned long long>(g_instructions));
    for (const Operation &op : g_operations) CHECK_EQ(op.wrong, 0u);
    return check::done("test_vu_macro");
}
#endif
