// The fork's SPU2 mixer through its register interface: which voices, inputs and cores reach the output,
// what the mixer writes back into SPU2 RAM, and what keeps running while a voice is stopped.
// The few kernel and hardware functions the mixer calls are supplied by spu2_fixture.hpp.
//
// fork-source: runtime/src/iop/spu2.cpp

#include "spu2_fixture.hpp"

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
    a_volume_sweep_runs_while_the_voice_is_stopped();
    a_sweep_keeps_its_pace_through_a_key_on();
    reading_the_input_area_raises_the_address_interrupt();
    an_auto_dma_stream_plays_every_sample_once_and_in_order();
    after_a_reset_voices_and_inputs_are_routed_to_the_output();
    return check::done("test_spu2_mixer");
}
