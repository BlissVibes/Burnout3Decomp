// The fork's VIF unpacks (runtime/src/vif.cpp): the fast loops, which the common forms take, against the
// general loop, which handles every form. An unpack with the mask flag and a mask of zeros goes through the
// general loop and must write what the same unpack without the flag writes. Every format, signed and unsigned,
// under STMOD 0, 1 (the row added) and 2 (the row added and kept), with write cycles that skip, addresses
// that wrap past the end of VU1 memory, and made-up rows and data: VU1 memory and the row registers must come
// out the same, byte for byte. Also a run of NOPs, which the command loop passes over at once: CODE must show
// the last one, and a NOP with the interrupt bit must still stall the unit.
//
// fork-source: runtime/src/vif.cpp

#include "check.hpp"

#include "dmac.hpp"
#include "gs/gif.hpp"
#include "gs/queue.hpp"
#include "hw.hpp"
#include "vif.hpp"
#include "vu_micro.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// The few things vif.cpp takes from the rest of the runtime: nothing here kicks a program or the GS.
namespace ps2 {
std::uint8_t g_vu0_code[4096], g_vu0_mem[4096], g_vu1_code[16384], g_vu1_mem[16384];
namespace hw {
void intc_raise(unsigned) {}
}
namespace dmac {
void resume(int) {}
}
namespace vu {
void set_tops(std::uint32_t, std::uint32_t) {}
std::uint32_t tpc(int) { return 0; }
void code_changed(int) {}
void run(int, std::uint32_t) {}
}
namespace gif {
void transfer(int, const std::uint8_t *, std::uint32_t) {}
void scan_signals(int, const std::uint8_t *, std::uint32_t) {}
void set_path3_mask(bool) {}
void path3_advance() {}
bool path3_advance_releases() { return false; }
bool path3_unmask_releases() { return false; }
}
namespace gs::queue {
Role g_role = Role::Emulation;
Role &role() { return g_role; }
bool vu_active() { return false; }
Ring &vu() {
    static Ring ring;
    return ring;
}
void Ring::post(Type, std::uint32_t, std::uint32_t, const void *, std::uint32_t) {}
}
}

namespace {

namespace vif = ps2::vif;
using u32 = std::uint32_t;
using Words = std::vector<u32>;

struct Rng {
    std::uint64_t s;
    u32 next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<u32>((s * 0x2545F4914F6CDD1Dull) >> 32);
    }
    u32 below(u32 n) { return next() % n; }
};

u32 code(u32 op, u32 imm) { return (op << 24) | imm; }

// Sends the words, padded with NOPs to whole qwords, to VIF1.
void send(Words w) {
    while (w.size() % 4) w.push_back(0);
    vif::transfer(1, reinterpret_cast<const std::uint8_t *>(w.data()), static_cast<u32>(w.size() / 4));
}

// The words of the data of an unpack of `num` vectors with every write cycle filled (cl >= wl).
u32 data_words(u32 vn, u32 vl, u32 num) {
    const u32 bits = (vn == 3 && vl == 3) ? 16u : (vn + 1) * (vl == 0 ? 32u : vl == 1 ? 16u : vl == 2 ? 8u : 16u);
    return (num * bits + 31) / 32;
}

struct Outcome {
    std::vector<std::uint8_t> mem;
    u32 row[4];
};

Outcome unpack_once(const std::vector<std::uint8_t> &mem, u32 cl, u32 wl, u32 mode, const u32 row[4], u32 vn, u32 vl, bool usn, u32 num, u32 addr, const Words &data, bool through_mask) {
    std::memcpy(ps2::g_vu1_mem, mem.data(), 16384);
    vif::write(1, 0x10, 1);   // reset
    Words w{code(0x01, cl | (wl << 8)), code(0x05, mode), code(0x30, 0), row[0], row[1], row[2], row[3]};
    if (through_mask) {
        w.push_back(code(0x20, 0));
        w.push_back(0);
    }
    w.push_back(((0x60u | (through_mask ? 0x10u : 0u) | (vn << 2) | vl) << 24) | ((num & 0xFFu) << 16) | (usn ? 0x4000u : 0u) | addr);
    w.insert(w.end(), data.begin(), data.end());
    send(w);
    Outcome o;
    o.mem.assign(ps2::g_vu1_mem, ps2::g_vu1_mem + 16384);
    for (u32 j = 0; j < 4; ++j) o.row[j] = vif::read(1, 0x100 + j * 0x10);
    return o;
}

void the_fast_loops_write_what_the_general_loop_writes() {
    Rng g{0x5EED0F1F0ull};
    std::vector<std::uint8_t> mem(16384);
    int cases = 0, differ = 0;
    for (int round = 0; round < 3000; ++round) {
        for (auto &b : mem) b = static_cast<std::uint8_t>(g.next());
        const u32 vn = g.below(4), vl = g.below(4), mode = g.below(3);
        const bool usn = g.below(2);
        const u32 wl = 1 + g.below(4), cl = wl + (g.below(3) == 0 ? g.below(4) : 0);
        const u32 num = 1 + (g.below(4) == 0 ? g.below(256) : g.below(40));
        const u32 addr = g.below(3) == 0 ? 1024 - g.below(16) : g.below(1024);
        u32 row[4];
        for (u32 &r : row) r = g.below(3) == 0 ? g.below(16) : g.next();
        Words data(data_words(vn, vl, num));
        for (u32 &d : data) d = g.next();
        const Outcome fast = unpack_once(mem, cl, wl, mode, row, vn, vl, usn, num, addr, data, false);
        const Outcome general = unpack_once(mem, cl, wl, mode, row, vn, vl, usn, num, addr, data, true);
        ++cases;
        const bool same = fast.mem == general.mem && std::memcmp(fast.row, general.row, sizeof row) == 0;
        if (!same && ++differ <= 10)
            std::printf("unpack vn %u vl %u usn %d mode %u cl %u wl %u num %u addr %u: the fast loop and the general loop differ\n", vn, vl, usn ? 1 : 0, mode, cl, wl, num, addr);
        CHECK(same);
    }
    std::printf("test_vif_unpack: %d unpacks compared, %d differ\n", cases, differ);
}

void a_run_of_nops_is_passed_over() {
    vif::write(1, 0x10, 1);
    send({0x00000000u, 0x00001234u, 0x00050000u, code(0x01, 0x0102), 0x00000000u, 0x00000000u, 0x00000000u, 0x007F0000u});
    CHECK_EQ(vif::read(1, 0x80), 0x007F0000u);
    CHECK_EQ(vif::read(1, 0x40), 0x0102u);
    CHECK(!vif::stalled(1));
    // The interrupt bit stalls the unit on that NOP; what follows waits for FBRST.
    send({0x00000000u, 0x80000000u, 0x00000077u, code(0x01, 0x0203)});
    CHECK(vif::stalled(1));
    CHECK_EQ(vif::read(1, 0x80), 0x80000000u);
    CHECK_EQ(vif::read(1, 0x40), 0x0102u);
    vif::write(1, 0x10, 8);
    CHECK(!vif::stalled(1));
    CHECK_EQ(vif::read(1, 0x80), 0x01000203u);
    CHECK_EQ(vif::read(1, 0x40), 0x0203u);
}

}

int main() {
    the_fast_loops_write_what_the_general_loop_writes();
    a_run_of_nops_is_passed_over();
    return check::done("test_vif_unpack");
}
