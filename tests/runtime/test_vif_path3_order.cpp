// The fork's VIF1, GIF and queue together: in the page the VU thread runs VIF1 from the words the emulation
// thread posts to the VU ring, and the emulation thread runs the same words in shadow for MSKPATH3, which lets
// held PATH3 data go to the GS through the same ring. Whatever the threads, the GS must get its data in the
// order the native program gives it: a PATH3 packet released by a VIF1 code comes after what the codes before
// it sent and before what the codes after it send, also when the codes after it are in the same transfer, when
// the transfer stalled on an interrupt bit before the release and FBRST let it go on, and when a command is
// split over two transfers.
//
// Each case runs twice: natively (no rings: everything is drawn at once on the one thread) and as the page
// does it (rings, the emulation thread's shadow pass, then the VU thread's loop as in system.cpp
// run_vu_thread, then the render thread's), here one after the other on one thread. The GS register writes
// of the two runs must be the same, in the same order. The VIF1 streams and packets are made up; a VU
// microprogram here is one XGKICK of a packet named by its address.
//
// fork-source: runtime/src/vif.cpp
// fork-source: runtime/src/gs/gif.cpp
// fork-source: runtime/src/gs/queue.cpp

#include "check.hpp"

#include "gif.hpp"
#include "gs.hpp"
#include "queue.hpp"
#include "vif.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Write {
    std::uint32_t kind, reg;
    std::uint64_t lo, hi;
    bool operator==(const Write &o) const { return kind == o.kind && reg == o.reg && lo == o.lo && hi == o.hi; }
};
std::vector<Write> g_gs;   // what reached the GS, in order

using Words = std::vector<std::uint32_t>;
using Bytes = std::vector<std::uint8_t>;

// A GS packet: one PACKED tag (A+D, end of packet) and `n` qwords, each writing mark + i to register 0x3F.
Bytes packet(std::uint64_t mark, std::uint32_t n) {
    Bytes out(16 * (n + 1));
    const std::uint64_t tag = n | (1ull << 15) | (1ull << 60), regs = 0xE;
    std::memcpy(out.data(), &tag, 8);
    std::memcpy(out.data() + 8, &regs, 8);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint64_t value = mark + i, reg = 0x3F;
        std::memcpy(out.data() + 16 * (i + 1), &value, 8);
        std::memcpy(out.data() + 16 * (i + 1) + 8, &reg, 8);
    }
    return out;
}

}

namespace ps2 {
std::uint8_t g_vu0_code[4096], g_vu0_mem[4096], g_vu1_code[16384], g_vu1_mem[16384];
namespace hw {
void intc_raise(unsigned) {}
}
namespace dmac {
void resume(int) {}
}
namespace vu {
std::uint32_t g_top = 0;
void set_tops(std::uint32_t top, std::uint32_t) { g_top = top; }
std::uint32_t tpc(int) { return 0; }
void code_changed(int) {}
// A microprogram: one XGKICK of three qwords marked by its address.
void run(int, std::uint32_t address) {
    const Bytes p = packet(0x100000ull * (address + 1), 3);
    gif::transfer(1, p.data(), static_cast<std::uint32_t>(p.size() / 16));
}
}
namespace gs {
State g_state;
void write(std::uint32_t reg, std::uint64_t value) { g_gs.push_back({0, reg, value, 0}); }
void signal(std::uint32_t, std::uint64_t) {}
void write_packed(std::uint32_t reg, const Reg128 &q) { g_gs.push_back({1, reg, q.d[0], q.d[1]}); }
void write_image(const std::uint8_t *, std::uint32_t) {}
}
}

namespace {

namespace gif = ps2::gif;
namespace vif = ps2::vif;
namespace queue = ps2::gs::queue;

constexpr std::uint32_t kStall = 0x80000000u;   // the interrupt bit of a VIF code
std::uint32_t nop() { return 0; }
std::uint32_t mskpath3(bool on) { return (0x06u << 24) | (on ? 0x8000u : 0u); }
std::uint32_t stcycl() { return (0x01u << 24) | 0x0101u; }
std::uint32_t mscal(std::uint32_t address) { return (0x14u << 24) | address; }

// DIRECT with a packet of `n` qwords marked `mark`: the code and the packet's words.
Words direct(std::uint64_t mark, std::uint32_t n) {
    const Bytes p = packet(mark, n);
    Words out{(0x50u << 24) | (n + 1)};
    for (std::size_t i = 0; i < p.size(); i += 4) {
        std::uint32_t w;
        std::memcpy(&w, p.data() + i, 4);
        out.push_back(w);
    }
    return out;
}

Words join(std::initializer_list<Words> parts) {
    Words out;
    for (const Words &p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// What the EE does, in order: a VIF1 transfer, a PATH3 transfer, or an FBRST that lets a stalled VIF1 go on.
struct Step {
    enum Kind { kVif1, kPath3, kFbrst } kind;
    Words words;     // kVif1: whole qwords (padded with NOPs here)
    Bytes bytes;     // kPath3
};

Step vif1(Words w) {
    while (w.size() % 4) w.push_back(nop());
    return {Step::kVif1, std::move(w), {}};
}
Step path3(std::uint64_t mark, std::uint32_t n) { return {Step::kPath3, {}, packet(mark, n)}; }
Step fbrst() { return {Step::kFbrst, {}, {}}; }

void feed(const std::vector<Step> &steps) {
    for (const Step &s : steps) {
        if (s.kind == Step::kVif1) vif::transfer(1, reinterpret_cast<const std::uint8_t *>(s.words.data()), static_cast<std::uint32_t>(s.words.size() / 4));
        else if (s.kind == Step::kPath3) gif::transfer(3, s.bytes.data(), static_cast<std::uint32_t>(s.bytes.size() / 16));
        else vif::write(1, 0x10, 8);
    }
}

void fresh() {
    vif::reset();
    vif::reset_vu_side();
    gif::reset();
    g_gs.clear();
}

std::vector<Write> native(const std::vector<Step> &steps) {
    fresh();
    feed(steps);
    return g_gs;
}

// The VU thread's loop of system.cpp (run_vu_thread), and the render thread's handling of GIF data (gs.cpp
// render_pending), run to the end of what was posted.
std::vector<Write> page(const std::vector<Step> &steps) {
    queue::role() = queue::Role::Emulation;
    fresh();
    feed(steps);
    queue::role() = queue::Role::Vu;
    queue::Header h;
    const std::uint8_t *payload = nullptr;
    while (queue::vu().peek(h, payload)) {
        switch (h.type) {
        case queue::kVif1: vif::transfer_words(1, reinterpret_cast<const std::uint32_t *>(payload), h.arg0); break;
        case queue::kVif1Write: vif::write(1, h.arg0, h.arg1); break;
        case queue::kGif: queue::render().add_gif(h.arg0, payload, h.bytes); break;
        default: queue::render().post(static_cast<queue::Type>(h.type), h.arg0, h.arg1, payload, h.bytes); break;
        }
        queue::vu().pop(h);
    }
    queue::render().send();
    queue::role() = queue::Role::Render;
    g_gs.clear();
    while (queue::render().peek(h, payload)) {
        if (h.type == queue::kGif) gif::render(static_cast<int>(h.arg0), payload, h.arg1);
        else if (h.type == queue::kGifReset) gif::render_reset();
        queue::render().pop(h);
    }
    queue::role() = queue::Role::Emulation;
    return g_gs;
}

struct Case {
    const char *name;
    std::vector<Step> steps;
};

std::vector<Case> cases() {
    const Step held = vif1({mskpath3(true)});
    return {
        {"codes after the release in the same transfer",
         {held, path3(0xA000, 2), path3(0xB000, 3),
          vif1(join({{mscal(1), mskpath3(false), mskpath3(true)}, direct(0xD000, 2), {mscal(2), mskpath3(false), mskpath3(true)}, direct(0xE000, 1)}))}},
        {"an open PATH3 drains before the next working code",
         {held, path3(0xA000, 2), path3(0xB000, 1), vif1(join({{mscal(1), mskpath3(false), stcycl()}, direct(0xD000, 2)}))}},
        {"a release after a stall that FBRST ends",
         {held, path3(0xA000, 2), path3(0xB000, 2),
          vif1(join({{mscal(1), nop() | kStall, mskpath3(false), mskpath3(true)}, direct(0xD000, 2), {mscal(3)}})), fbrst(),
          vif1(join({{mskpath3(false), mskpath3(true)}, direct(0xE000, 1)}))}},
        {"data that arrives while stalled waits behind the stall",
         {held, path3(0xA000, 1), vif1({mscal(1), nop() | kStall}), vif1(join({{mskpath3(false), mskpath3(true)}, direct(0xD000, 1)})), fbrst(),
          vif1({mscal(4)})}},
        {"a command split over two transfers",
         {held, path3(0xA000, 2), path3(0xB000, 1)}},
        {"nothing held: the stream as it was",
         {vif1(join({{mscal(1)}, direct(0xD000, 3), {mskpath3(true), mskpath3(false), mscal(2)}})), path3(0xA000, 2), vif1({mscal(5)})}},
    };
}

}

int main() {
    std::vector<Case> all = cases();
    // The split command: a DIRECT whose code and first data words are in one transfer, the rest in the next,
    // with a release just before it.
    {
        const Words d = direct(0xD000, 3);
        Words first = join({{mscal(1), mskpath3(false), mskpath3(true)}, Words(d.begin(), d.begin() + 5)});
        Words second(d.begin() + 5, d.end());
        second.push_back(mscal(2));
        Case &split = all[4];
        Step a{Step::kVif1, first, {}}, b{Step::kVif1, second, {}};
        while (a.words.size() % 4) a.words.insert(a.words.begin(), nop());   // NOPs before, so the DIRECT's words stay together
        b = vif1(b.words);
        split.steps.push_back(a);
        split.steps.push_back(b);
    }
    std::vector<std::vector<Write>> want;
    for (const Case &c : all) want.push_back(native(c.steps));
    queue::vu().start(1u << 20);
    queue::render().start(1u << 20);
    for (std::size_t i = 0; i < all.size(); ++i) {
        const std::vector<Write> got = page(all[i].steps);
        const bool same = got == want[i];
        if (!same) {
            std::printf("test_vif_path3_order: %s: the page's GS writes differ from the native ones (%zu against %zu)\n", all[i].name, got.size(), want[i].size());
            for (std::size_t k = 0; k < std::max(got.size(), want[i].size()); ++k)
                std::printf("  %3zu  native %s%llX  page %s%llX\n", k, k < want[i].size() ? "" : "-", k < want[i].size() ? static_cast<unsigned long long>(want[i][k].lo) : 0ull,
                            k < got.size() ? "" : "-", k < got.size() ? static_cast<unsigned long long>(got[k].lo) : 0ull);
        }
        CHECK(same);
        CHECK(want[i].size() >= 8);
    }
    return check::done("test_vif_path3_order");
}
