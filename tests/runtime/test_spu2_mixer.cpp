// The fork's SPU2 mixer through its register interface: which voices, inputs and cores reach the output,
// what the mixer writes back into SPU2 RAM, and what keeps running while a voice is stopped.
// The few kernel and hardware functions the mixer calls are supplied here.
//
// fork-source: runtime/src/iop/spu2.cpp

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
// The test voice plays a constant 16384; these are what is left of it after each stage (the volumes are a little under 1).
constexpr int kVoice = 16382;        // after the envelope and a voice volume of 0x3FFF
constexpr int kCore1Voice = 16381;   // a core 1 voice at the output
constexpr int kCore0Out = 16381;     // a core 0 voice in core 0's output
constexpr int kCore0Voice = 16379;   // a core 0 voice at the output, through core 1

void reg(int core, std::uint32_t off, std::uint32_t value) { spu2::write(kRegisters + core * 0x400 + off, value, 2); }
std::uint32_t reg(int core, std::uint32_t off) { return spu2::read(kRegisters + core * 0x400 + off, 2); }
void reg32(int core, std::uint32_t off, std::uint32_t value) {
    reg(core, off, value & 0xFFFFu);
    reg(core, off + 2, value >> 16);
}
void volume(int core, std::uint32_t off, std::uint32_t value) { spu2::write(kRegisters + 0x760 + core * 0x28 + off, value, 2); }
int ram(std::uint32_t address) { return static_cast<std::int16_t>(spu2::ram()[address]); }

void run(unsigned batches) {
    for (unsigned i = 0; i < batches; ++i) {
        g_tick({});
        g_time += kBatch * 768;
        ++g_batches;
    }
}

// Where in a write-back ring the last sample of the last batch went.
std::uint32_t last_position() { return ((g_batches - 1) * kBatch) % kRing + kBatch - 1; }
int left() { return g_out[g_out.size() - 2]; }
int right() { return g_out[g_out.size() - 1]; }
bool about(int value, int wanted) { return std::abs(value - wanted) <= 4; }

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
void start_voice(int core, int voice, std::uint32_t block, std::uint32_t voll, std::uint32_t volr) {
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

void fill(std::uint32_t address, int value) {
    for (unsigned i = 0; i < kRing; ++i) spu2::ram()[address + i] = static_cast<std::uint16_t>(value);
}

void a_voice_is_heard_only_on_the_sides_its_dry_switches_allow() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    run(4);
    CHECK(about(left(), kCore1Voice));
    CHECK(about(right(), kCore1Voice));
    reg32(1, kVmixl, 0xFFFFFEu);
    run(1);
    CHECK_EQ(left(), 0);
    CHECK(about(right(), kCore1Voice));
    reg32(1, kVmixr, 0xFFFFFEu);
    run(1);
    CHECK_EQ(left(), 0);
    CHECK_EQ(right(), 0);
}

void the_write_back_areas_hold_the_dry_and_the_effect_send_voice_mixes() {
    fresh();
    reg32(0, kVmixl, 0xFFFFFEu);
    reg32(0, kVmixr, 0xFFFFFEu);
    reg32(0, kVmixel, 0x000003u);
    reg32(0, kVmixer, 0x000003u);
    start_voice(0, 0, 0x5000, 0x3FFF, 0x2000);   // to the effects only
    start_voice(0, 1, 0x5008, 0x1000, 0x0000);   // dry and to the effects, left only
    run(4);
    const std::uint32_t at = last_position();
    CHECK(about(ram(0x1000 + at), kVoice / 4));
    CHECK_EQ(ram(0x1200 + at), 0);
    CHECK(about(ram(0x1400 + at), kVoice + kVoice / 4));
    CHECK(about(ram(0x1600 + at), kVoice / 2));
    CHECK(about(left(), kCore0Voice / 4));        // only the dry part reaches the output
    CHECK_EQ(right(), 0);
}

void core_1_has_its_own_write_back_areas() {
    fresh();
    reg32(1, kVmixel, 0x000001u);
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    run(4);
    const std::uint32_t at = last_position();
    CHECK(about(ram(0x1800 + at), kVoice));
    CHECK(about(ram(0x1A00 + at), kVoice));
    CHECK(about(ram(0x1C00 + at), kVoice));
    CHECK_EQ(ram(0x1E00 + at), 0);
}

void a_core_plays_its_input_area_from_ram_at_the_output_position() {
    fresh();
    for (unsigned i = 0; i < kRing; ++i) {
        spu2::ram()[0x2400 + i] = static_cast<std::uint16_t>(4000 + 8 * i);
        spu2::ram()[0x2600 + i] = static_cast<std::uint16_t>(-4000 - 8 * static_cast<int>(i));
    }
    run(kRing / kBatch + 1);                                   // once round the ring and one batch more
    int wrong = 0;
    for (unsigned i = 0; i < kRing + kBatch; ++i)
        if (!about(g_out[i * 2], 4000 + 8 * (i % kRing)) || !about(g_out[i * 2 + 1], -4000 - 8 * static_cast<int>(i % kRing))) ++wrong;
    CHECK_EQ(wrong, 0);
    fill(0x2400, 0);
    fill(0x2600, 0);
    fill(0x2000, 6000);                                        // core 0's input, heard through core 1
    fill(0x2200, -3000);
    run(1);
    CHECK(about(left(), 6000));
    CHECK(about(right(), -3000));
}

void the_input_volume_scales_the_input() {
    fresh();
    fill(0x2400, 8000);
    fill(0x2600, 8000);
    volume(1, kBvoll, 0x4000);
    volume(1, kBvolr, 0xC000);   // a negative volume inverts
    run(1);
    CHECK(about(left(), 4000));
    CHECK(about(right(), -4000));
}

void the_mix_switches_gate_voices_and_input_per_side() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    fill(0x2400, 1000);
    fill(0x2600, 1000);
    run(4);
    CHECK(about(left(), kCore1Voice + 1000));
    reg(1, kMmix, 0x0800 | 0x0040);   // voices left, input right
    run(1);
    CHECK(about(left(), kCore1Voice));
    CHECK(about(right(), 1000));
    reg(1, kMmix, 0x0400 | 0x0080);   // voices right, input left
    run(1);
    CHECK(about(left(), 1000));
    CHECK(about(right(), kCore1Voice));
    reg(1, kMmix, 0x0300 | 0x0030);   // only the effect-send switches: nothing dry
    run(1);
    CHECK_EQ(left(), 0);
    CHECK_EQ(right(), 0);
}

void core_0_reaches_the_output_through_core_1s_external_input() {
    fresh();
    start_voice(0, 0, 0x5000, 0x3FFF, 0x3FFF);
    run(4);
    CHECK(about(left(), kCore0Voice));
    CHECK(about(right(), kCore0Voice));
    volume(1, kAvoll, 0x4000);
    volume(1, kAvolr, 0x0000);
    run(1);
    CHECK(about(left(), kCore0Voice / 2));
    CHECK_EQ(right(), 0);
    CHECK(about(ram(0x0800 + last_position()), kCore0Out));   // core 0's own output is written back whatever core 1 does with it
    CHECK(about(ram(0x0A00 + last_position()), kCore0Out));
    volume(1, kAvoll, 0x7FFF);
    volume(1, kAvolr, 0x7FFF);
    reg(1, kMmix, 0x0FF0 | 0x0008);   // external input left only
    run(1);
    CHECK(about(left(), kCore0Voice));
    CHECK_EQ(right(), 0);
    reg(1, kMmix, 0x0FF0);
    run(1);
    CHECK_EQ(left(), 0);
    CHECK_EQ(right(), 0);
}

void a_volume_sweep_runs_while_the_voice_is_stopped() {
    fresh();
    reg(0, 5 * 16 + 0x0, 0x3FFF);
    reg(0, 5 * 16 + 0x2, 0x3FFF);
    CHECK_EQ(reg(0, 5 * 16 + 0xC), 0x7FFEu);
    reg(0, 5 * 16 + 0x0, 0xA020);     // sweep down, linear, 64 a sample
    run(2);
    CHECK_EQ(reg(0, 5 * 16 + 0xC), 0x7FFEu - 64 * 64);
    CHECK_EQ(reg(0, 5 * 16 + 0xE), 0x7FFEu);   // the right volume is fixed
    run(16);
    CHECK_EQ(reg(0, 5 * 16 + 0xC), 0u);
}

void a_sweep_keeps_its_pace_through_a_key_on() {
    fresh();
    reg(1, 0x0, 0x3FFF);
    reg(1, 0x0, 0xA020);
    run(1);
    start_voice(1, 0, 0x5000, 0xA020, 0x3FFF);
    run(1);
    CHECK_EQ(reg(1, 0xC), 0x7FFEu - 64 * 64);
    CHECK(about(left(), (kCore1Voice * (0x7FFE - 64 * 64)) >> 15));
}

void reading_the_input_area_raises_the_address_interrupt() {
    fresh();
    reg(1, kIrqa, 0);
    reg(1, kIrqa + 2, 0x2490);
    reg(1, kAttr, 0x0040);
    run(4);                           // output positions 0 to 0x7F
    CHECK(g_interrupts.empty());
    run(1);                           // 0x80 to 0x9F
    CHECK_EQ(g_interrupts.size(), 1u);
    CHECK_EQ(spu2::read(kRegisters + 0x7C2, 2), 8u);
    reg(1, kAttr, 0x0000);
    reg(1, kIrqa + 2, 0x26A0);        // the right half of the area counts too
    reg(1, kAttr, 0x0040);
    run(1);
    CHECK_EQ(g_interrupts.size(), 2u);
}

void an_auto_dma_stream_plays_every_sample_once_and_in_order() {
    fresh();
    constexpr unsigned kBlocks = 4, kFrames = 256;
    constexpr std::uint32_t kAt = 0x10000;
    for (unsigned b = 0; b < kBlocks; ++b)
        for (unsigned i = 0; i < kFrames; ++i) {
            const std::int16_t l = static_cast<std::int16_t>(800 + 8 * (b * kFrames + i)), r = static_cast<std::int16_t>(-l);
            std::memcpy(ps2::iop::g_ram + kAt + b * 1024 + i * 2, &l, 2);
            std::memcpy(ps2::iop::g_ram + kAt + b * 1024 + 512 + i * 2, &r, 2);
        }
    reg(1, kAdmas, 2);
    run(3);
    spu2::dma(1, kAt, kBlocks * 256, true, 7);
    run(80);
    const int failures = check::g_failures;
    std::size_t first = 0;
    while (first < g_out.size() / 2 && g_out[first * 2] == 0) ++first;
    CHECK(first < 3 * kBatch + 2 * kFrames);
    CHECK(first + kBlocks * kFrames <= g_out.size() / 2);
    if (check::g_failures != failures) return;
    int wrong = 0;
    for (unsigned i = 0; i < kBlocks * kFrames; ++i)
        if (!about(g_out[(first + i) * 2], 800 + 8 * static_cast<int>(i)) || !about(g_out[(first + i) * 2 + 1], -800 - 8 * static_cast<int>(i))) ++wrong;
    CHECK_EQ(wrong, 0);
    CHECK_EQ(g_scheduled.size(), 1u);   // the transfer finished: one DMA-done job for its channel
    CHECK_EQ(g_scheduled[0].kind, static_cast<std::uint32_t>(ps2::iop::kernel::kJobDmaDone));
    CHECK_EQ(g_scheduled[0].a, 7u);
}

void after_a_reset_voices_and_inputs_are_routed_to_the_output() {
    spu2::reset();
    g_out.clear();
    g_batches = 0;
    spu2::set_sink([](const std::int16_t *samples, std::size_t frames) { g_out.insert(g_out.end(), samples, samples + frames * 2); });
    CHECK_EQ(reg(0, kMmix), 0xFFCFu);
    CHECK_EQ(reg(1, kVmixl), 0xFFFFu);
    CHECK_EQ(reg(1, kVmixl + 2), 0x00FFu);
    start_voice(0, 0, 0x5000, 0x1000, 0x1000);
    start_voice(1, 0, 0x5000, 0x1000, 0x1000);
    fill(0x2400, 1000);
    run(4);
    CHECK(about(left(), kCore0Voice / 4 + kCore1Voice / 4 + 1000));
    CHECK(about(right(), kCore0Voice / 4 + kCore1Voice / 4));
}

}

int main() {
    a_voice_is_heard_only_on_the_sides_its_dry_switches_allow();
    the_write_back_areas_hold_the_dry_and_the_effect_send_voice_mixes();
    core_1_has_its_own_write_back_areas();
    a_core_plays_its_input_area_from_ram_at_the_output_position();
    the_input_volume_scales_the_input();
    the_mix_switches_gate_voices_and_input_per_side();
    core_0_reaches_the_output_through_core_1s_external_input();
    a_volume_sweep_runs_while_the_voice_is_stopped();
    a_sweep_keeps_its_pace_through_a_key_on();
    reading_the_input_area_raises_the_address_interrupt();
    an_auto_dma_stream_plays_every_sample_once_and_in_order();
    after_a_reset_voices_and_inputs_are_routed_to_the_output();
    return check::done("test_spu2_mixer");
}
