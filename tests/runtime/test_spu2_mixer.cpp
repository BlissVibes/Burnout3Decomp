// The fork's SPU2 mixer through its register interface: which voices, inputs and cores reach the output,
// what the mixer writes back into SPU2 RAM, and what keeps running while a voice is stopped.
// The few kernel and hardware functions the mixer calls are supplied by spu2_fixture.hpp.
//
// fork-source: runtime/src/iop/spu2.cpp

#include "spu2_fixture.hpp"

#include "ps2/state.hpp"

#include <algorithm>

namespace {

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

// What a sample of core 1's voice 0 at full volumes is at the output.
int through(int sample) { return ((((sample * 0x7FFF) >> 15) * 0x7FFE >> 15) * 0x7FFE) >> 15; }

void a_steady_sample_keeps_its_level_at_every_interpolation_phase() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    reg(1, 0x4, 0x0101);                       // a pitch that visits every phase
    run(140);
    int low = 32767, high = -32768;
    for (std::size_t i = 64; i < g_out.size() / 2; ++i) {
        low = std::min<int>(low, g_out[i * 2]);
        high = std::max<int>(high, g_out[i * 2]);
    }
    // The four weights of a phase add up to 0x7F7F to 0x7F81 of 0x8000 (the published property of the table).
    CHECK(low >= through(16384 * 0x7F7F >> 15) - 4);
    CHECK(high <= through(16384 * 0x7F81 >> 15) + 1);
}

void a_single_sample_comes_out_as_the_four_point_bell() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    for (int i = 1; i < 8; ++i) spu2::ram()[0x5000 + i] = 0;
    spu2::ram()[0x5003] = 0x0700;              // sample 10 of the 28 is 0x7000, the rest are 0
    reg(1, 0x4, 0x0100);                       // sixteen output samples per sample
    run(40);
    std::size_t peak = 0;
    for (std::size_t i = 64; i < 28 * 16 + 64; ++i)
        if (g_out[i * 2] > g_out[peak * 2]) peak = i;
    // The centre, and the weights one sample either side of it: 0x59B3, 0x1307 and 0x12C7 of 0x8000.
    CHECK(about(g_out[peak * 2], through(0x7000 * 0x59B3 >> 15)));
    CHECK(about(g_out[(peak - 16) * 2], through(0x7000 * 0x1307 >> 15)));
    CHECK(about(g_out[(peak + 16) * 2], through(0x7000 * 0x12C7 >> 15)));
    bool bell = true;
    for (std::size_t d = 0; d < 31; ++d) bell = bell && g_out[(peak - d - 1) * 2] <= g_out[(peak - d) * 2] && g_out[(peak + d + 1) * 2] <= g_out[(peak + d) * 2];
    CHECK(bell);
    CHECK(g_out[(peak - 24) * 2] > 0);         // wider than a straight line between two samples would be
    CHECK(g_out[(peak + 24) * 2] > 0);
}

// A block of 28 equal samples (nibble * 0x1000) with the given flags: 1 end, 2 repeat, 4 loop start.
void block(std::uint32_t at, unsigned flags, unsigned nibble) {
    spu2::ram()[at] = static_cast<std::uint16_t>(flags << 8);
    for (int i = 1; i < 8; ++i) spu2::ram()[at + i] = static_cast<std::uint16_t>(nibble * 0x1111u);
}

// A steady sample after the interpolation, at a whole read position (weights 0x12C7, 0x59B3, 0x1307 and -1 of 0x8000).
int steady(int sample) { return (0x12C7 * sample >> 15) + (0x59B3 * sample >> 15) + (0x1307 * sample >> 15) + (-sample >> 15); }

void a_voice_starts_from_silence_behind_its_first_sample() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    run(1);
    // The first sample is the newest of the four, with silence before it; the envelope rises over the same samples.
    const int attack[3] = {0x3800, 0x7000, 0x7FFF};
    const int interpolated[3] = {-16384 >> 15, (0x1307 * 16384 >> 15) + (-16384 >> 15), (0x59B3 * 16384 >> 15) + (0x1307 * 16384 >> 15) + (-16384 >> 15)};
    for (int i = 0; i < 3; ++i) CHECK(about(g_out[i * 2], (((interpolated[i] * attack[i] >> 15) * 0x7FFE >> 15) * 0x7FFE) >> 15));
    CHECK(about(g_out[3 * 2], kCore1Voice));
}

void a_sound_without_a_loop_plays_to_the_end_of_its_last_block() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    block(0x5000, 4, 4);
    block(0x5008, 1, 4);                       // the end, no repeat
    run(1);
    CHECK_EQ(reg(1, 0x340) & 1u, 0u);          // 32 of its 56 samples played: not at the end yet
    CHECK(about(left(), kCore1Voice));
    run(3);
    CHECK_EQ(reg(1, 0x340) & 1u, 1u);
    int sounding = 0, last = 0;
    for (int i = 0; i < 128; ++i)
        if (std::abs(g_out[i * 2]) > 100) {
            ++sounding;
            last = i;
        }
    CHECK_EQ(last, 55);
    CHECK_EQ(sounding, 55);                    // every sample but the first, which has only silence behind it
    CHECK(about(g_out[55 * 2], kCore1Voice));  // at full level to the last sample, then cut
}

void a_loop_of_two_blocks_plays_them_in_turn() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    block(0x5000, 4, 2);
    block(0x5008, 3, 6);                       // the end, repeat
    run(1);
    CHECK_EQ(reg(1, 0x1C0 + 10), 0x500Au);     // NAX: four samples into the second block
    run(7);
    for (int turn = 0; turn < 2; ++turn) {
        CHECK(about(g_out[(turn * 56 + 14) * 2], through(steady(0x2000))));
        CHECK(about(g_out[(turn * 56 + 42) * 2], through(steady(0x6000))));
    }
    CHECK_EQ(reg(1, 0x340) & 1u, 1u);
}

void a_playing_voice_in_a_state_of_an_older_version_keeps_playing() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    run(4);
    ps2::state::Writer old;
    old.version = 5;
    spu2::state(old);
    ps2::state::Reader reader(old.out.data(), old.out.size());
    reader.version = 5;
    spu2::state(reader);
    run(4);
    CHECK(about(left(), kCore1Voice));
}

// Where a mix beyond 16 bits ends up with the master volume the fixture sets (0x3FFF, which is 0x7FFE of 0x8000):
// the mix is clamped first and the master volume applied to that.
constexpr int kTop = (32767 * 0x7FFE) >> 15, kBottom = -((32768 * 0x7FFE) >> 15);

// Voices at the test level, all playing the block at 0x5000; three of them take a core's voice mix past 16 bits.
// With sign -1 that block, and so every voice started so far, plays -16384.
void start_loud_voices(int core, int count, int sign) {
    for (int v = 0; v < count; ++v) start_voice(core, v, 0x5000, 0x3FFF, 0x3FFF);
    if (sign < 0) block(0x5000, 7, 0xC);
}

void a_mix_of_three_times_full_scale_stays_on_its_own_side() {
    for (int sign = 1; sign >= -1; sign -= 2) {
        fresh();
        start_loud_voices(1, 3, sign);             // core 1's voices, clamped to 16 bits
        start_loud_voices(0, 3, sign);             // core 0's output, clamped too, into core 1
        fill(0x2400, sign * 32767);                // and core 1's input
        fill(0x2600, sign * 32767);
        run(4);
        CHECK_EQ(left(), sign > 0 ? kTop : kBottom);
        CHECK_EQ(right(), sign > 0 ? kTop : kBottom);
    }
}

void a_mix_just_past_twice_full_scale_stays_on_its_own_side() {
    // Levels like a crash in a race: the voice mix at the limit, the input at 20400 and core 0 at about 16300.
    for (int sign = 1; sign >= -1; sign -= 2) {
        fresh();
        start_loud_voices(1, 3, sign);
        start_loud_voices(0, 1, sign);
        fill(0x2400, sign * 20400);
        fill(0x2600, sign * 20400);
        run(4);
        CHECK_EQ(left(), sign > 0 ? kTop : kBottom);
        CHECK_EQ(right(), sign > 0 ? kTop : kBottom);
    }
}

void a_mix_is_clamped_before_the_master_volume() {
    fresh();
    for (int core = 0; core < 2; ++core) {
        start_loud_voices(core, 3, 1);
        volume(core, kMvoll, 0x2000);              // half
        volume(core, kMvolr, 0x6000);              // minus half
    }
    reg(1, kMmix, 0x0FF0);                         // core 1 without core 0, so the two cores have the same mix
    for (std::uint32_t area : {0x2000u, 0x2200u, 0x2400u, 0x2600u}) fill(area, 32767);
    run(4);
    // Twice full scale at half volume: half of full scale, as if the mix had been full scale, and not all of it.
    CHECK_EQ(left(), (32767 * 0x4000) >> 15);
    CHECK_EQ(right(), -((32767 * 0x4000) >> 15) - 1);
    CHECK_EQ(ram(0x0800 + last_position()), (32767 * 0x4000) >> 15);   // core 0's output is made the same way
    CHECK_EQ(ram(0x0A00 + last_position()), -((32767 * 0x4000) >> 15) - 1);
}

// The published step of an exponential decrease: the step times the level, taken down 15 bits, which rounds down.
// So the level falls by at least one unit a step and reaches 0.
int exponential_step(int level, int step) { return level + ((step * level) >> 15); }

void an_exponential_release_falls_by_the_published_steps_to_silence() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    reg(1, 0x8, 0x002C);                           // release: exponential, a step of -8 every second sample
    run(4);
    CHECK_EQ(reg(1, 0xA), 0x7FFFu);
    reg32(1, kKeyOff, 1);
    run(1);
    CHECK_EQ(reg(1, 0xA), 0x7FFFu - 16 * 8);       // near the top 8 * level / 0x8000 is just under 8: a step of -8, not -7
    int level = 0x7FFF - 16 * 8, wrong = 0;
    unsigned batches = 1;
    while (level > 0 && batches < 3000) {
        for (int i = 0; i < 16; ++i) level = exponential_step(level, -8);
        run(1);
        ++batches;
        if (reg(1, 0xA) != static_cast<std::uint32_t>(level)) ++wrong;
    }
    CHECK_EQ(wrong, 0);
    CHECK(batches < 1500);                         // within a second
    CHECK_EQ(reg(1, 0xA), 0u);
    CHECK_EQ(left(), 0);
    const std::uint32_t reading = reg(1, 0x1C0 + 10);
    run(3);
    CHECK_EQ(reg(1, 0x1C0 + 10), reading);         // the voice is off: it reads no further
}

void a_slow_exponential_release_ends_too() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    reg(1, 0x8, 0x0030);                           // release: exponential, a step of -8 every 32 samples, one a batch
    run(4);
    reg32(1, kKeyOff, 1);
    int level = 0x7FFF, wrong = 0;
    for (unsigned batch = 0; batch < 15000; ++batch) {
        level = exponential_step(level, -8);
        run(1);
        if (reg(1, 0xA) != static_cast<std::uint32_t>(level)) ++wrong;
    }
    CHECK_EQ(wrong, 0);
    CHECK_EQ(level, 0);                            // ten seconds; the rule is at 0 after 11132 steps, 7.4 s
    CHECK_EQ(left(), 0);
}

void an_exponential_decay_reaches_the_lowest_sustain_level() {
    fresh();
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    reg(1, 0x6, 0x00B0);                           // fastest attack, decay by a step of -8 a sample, sustain level 0x800
    reg(1, 0x8, 0x1FC0);                           // sustain: the slowest increase, as good as held
    run(1500);
    CHECK_EQ(reg(1, 0xA), 0x800u);
}

void an_exponential_sweep_down_reaches_zero() {
    fresh();
    reg(0, 5 * 16 + 0x0, 0x3FFF);
    reg(0, 5 * 16 + 0x0, 0xE02C);                  // sweep down, exponential, a step of -8 a sample
    run(1);
    CHECK_EQ(reg(0, 5 * 16 + 0xC), 0x7FFEu - 32 * 8);
    run(1500);
    CHECK_EQ(reg(0, 5 * 16 + 0xC), 0u);
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
    a_steady_sample_keeps_its_level_at_every_interpolation_phase();
    a_single_sample_comes_out_as_the_four_point_bell();
    a_voice_starts_from_silence_behind_its_first_sample();
    a_sound_without_a_loop_plays_to_the_end_of_its_last_block();
    a_loop_of_two_blocks_plays_them_in_turn();
    a_playing_voice_in_a_state_of_an_older_version_keeps_playing();
    a_mix_of_three_times_full_scale_stays_on_its_own_side();
    a_mix_just_past_twice_full_scale_stays_on_its_own_side();
    a_mix_is_clamped_before_the_master_volume();
    an_exponential_release_falls_by_the_published_steps_to_silence();
    a_slow_exponential_release_ends_too();
    an_exponential_decay_reaches_the_lowest_sustain_level();
    an_exponential_sweep_down_reaches_zero();
    a_volume_sweep_runs_while_the_voice_is_stopped();
    a_sweep_keeps_its_pace_through_a_key_on();
    reading_the_input_area_raises_the_address_interrupt();
    an_auto_dma_stream_plays_every_sample_once_and_in_order();
    after_a_reset_voices_and_inputs_are_routed_to_the_output();
    return check::done("test_spu2_mixer");
}
