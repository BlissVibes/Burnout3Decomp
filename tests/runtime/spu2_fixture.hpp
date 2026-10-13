#pragma once

// What the SPU2 tests share: the few kernel and hardware functions spu2.cpp calls, and helpers that drive
// the mixer through its registers. Include from a test that names
//     fork-source: runtime/src/iop/spu2.cpp

#include "check.hpp"

#include "hw.hpp"
#include "iop.hpp"
#include "kernel.hpp"
#include "spu2.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

ps2::iop::kernel::JobHandler g_tick = nullptr;
std::uint64_t g_time = 0;
std::vector<ps2::iop::kernel::Job> g_scheduled;   // every job the mixer scheduled except its own tick
std::vector<unsigned> g_interrupts;
std::vector<std::int16_t> g_out;                  // interleaved left, right
unsigned g_batches = 0;

}

namespace ps2::iop {

std::uint8_t g_ram[kRamSize];

namespace kernel {
std::uint64_t now() { return g_time; }
void register_job(std::uint32_t kind, JobHandler handler) {
    if (kind == kJobSpuTick) g_tick = handler;
}
void schedule(std::uint64_t, const Job &job) {
    if (job.kind != kJobSpuTick) g_scheduled.push_back(job);
}
}

namespace hw {
void raise_interrupt(unsigned line) { g_interrupts.push_back(line); }
}

}

namespace {

namespace spu2 = ps2::iop::spu2;

constexpr std::uint32_t kRegisters = 0x1F900000u;
constexpr unsigned kBatch = 32, kRing = 0x200;
// Registers of one core.
constexpr std::uint32_t kVmixl = 0x188, kVmixel = 0x18C, kVmixr = 0x190, kVmixer = 0x194, kMmix = 0x198, kAttr = 0x19A, kIrqa = 0x19C, kKeyOn = 0x1A0,
                        kAdmas = 0x1B0;
// Volumes of one core.
constexpr std::uint32_t kMvoll = 0x0, kMvolr = 0x2, kAvoll = 0x8, kAvolr = 0xA, kBvoll = 0xC, kBvolr = 0xE;
// The test voice plays a constant 16384, which the four-point interpolation passes at 16318 (its four weights
// add up to a little under 1); these are what is left after each stage (the volumes are a little under 1 too).
constexpr int kVoice = 16316;        // after the envelope and a voice volume of 0x3FFF
constexpr int kCore1Voice = 16315;   // a core 1 voice at the output
constexpr int kCore0Out = 16315;     // a core 0 voice in core 0's output
constexpr int kCore0Voice = 16313;   // a core 0 voice at the output, through core 1

void reg(int core, std::uint32_t off, std::uint32_t value) { spu2::write(kRegisters + core * 0x400 + off, value, 2); }
std::uint32_t reg(int core, std::uint32_t off) { return spu2::read(kRegisters + core * 0x400 + off, 2); }
[[maybe_unused]] void reg32(int core, std::uint32_t off, std::uint32_t value) {
    reg(core, off, value & 0xFFFFu);
    reg(core, off + 2, value >> 16);
}
void volume(int core, std::uint32_t off, std::uint32_t value) { spu2::write(kRegisters + 0x760 + core * 0x28 + off, value, 2); }
[[maybe_unused]] int ram(std::uint32_t address) { return static_cast<std::int16_t>(spu2::ram()[address]); }

void run(unsigned batches) {
    for (unsigned i = 0; i < batches; ++i) {
        g_tick({});
        g_time += kBatch * 768;
        ++g_batches;
    }
}

// Where in a write-back ring the last sample of the last batch went.
[[maybe_unused]] std::uint32_t last_position() { return ((g_batches - 1) * kBatch) % kRing + kBatch - 1; }
[[maybe_unused]] int left() { return g_out[g_out.size() - 2]; }
[[maybe_unused]] int right() { return g_out[g_out.size() - 1]; }
[[maybe_unused]] bool about(int value, int wanted) { return std::abs(value - wanted) <= 4; }

// A reset SPU2 with both cores routed the way the game's sound library sets them up: every voice dry and none
// to the effects, voices and input into each core's output, core 0 into core 1.
void fresh() {
    spu2::reset();
    spu2::set_sink([](const std::int16_t *samples, std::size_t frames) { g_out.insert(g_out.end(), samples, samples + frames * 2); });
    std::memset(ps2::iop::g_ram, 0, sizeof ps2::iop::g_ram);
    g_time = 0;
    g_batches = 0;
    g_scheduled.clear();
    g_interrupts.clear();
    g_out.clear();
    for (int core = 0; core < 2; ++core) {
        reg32(core, kVmixl, 0xFFFFFFu);
        reg32(core, kVmixr, 0xFFFFFFu);
        reg32(core, kVmixel, 0);
        reg32(core, kVmixer, 0);
        reg(core, kMmix, core ? 0x0FFCu : 0x0FF0u);
        volume(core, kMvoll, 0x3FFF);
        volume(core, kMvolr, 0x3FFF);
        volume(core, kAvoll, 0x7FFF);
        volume(core, kAvolr, 0x7FFF);
        volume(core, kBvoll, 0x7FFF);
        volume(core, kBvolr, 0x7FFF);
    }
}

// Starts a voice that plays a constant 16384 for ever at full envelope: one ADPCM block that loops on itself.
[[maybe_unused]] void start_voice(int core, int voice, std::uint32_t block, std::uint32_t voll, std::uint32_t volr) {
    spu2::ram()[block] = 0x0700;   // loop start, loop end and repeat; no prediction, no shift
    for (int i = 1; i < 8; ++i) spu2::ram()[block + i] = 0x4444;
    reg(core, 0x1C0 + voice * 12, block >> 16);
    reg(core, 0x1C2 + voice * 12, block & 0xFFFFu);
    reg(core, voice * 16 + 0x0, voll);
    reg(core, voice * 16 + 0x2, volr);
    reg(core, voice * 16 + 0x4, 0x1000);   // pitch: one sample per output sample
    reg(core, voice * 16 + 0x6, 0x00FF);   // fastest attack, straight to sustain
    reg(core, voice * 16 + 0x8, 0x0000);   // sustain held at the top
    reg32(core, kKeyOn, 1u << voice);
}

[[maybe_unused]] void fill(std::uint32_t address, int value) {
    for (unsigned i = 0; i < kRing; ++i) spu2::ram()[address + i] = static_cast<std::uint16_t>(value);
}

}
