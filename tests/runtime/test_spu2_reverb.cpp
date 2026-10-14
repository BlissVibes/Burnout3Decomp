// The fork's SPU2 reverb through the register interface, against a plain transcription of the published
// reverb formula (the PlayStation SPU's: a half-rate network of two reflection filters, four combs and two
// all-pass filters per side, working in sound RAM), and the rules around it: what feeds it, what it
// writes, the effect volume, the address interrupt and saved states. Last, the arithmetic that places the
// network's taps in the work area (spu2_work_area.hpp, beside spu2.cpp) against the formula it replaced.
// The preset and the signals are made up for the test; nothing here comes from a game.
//
// fork-source: runtime/src/iop/spu2.cpp

#include "spu2_fixture.hpp"

#include "ps2/state.hpp"
#include "spu2_work_area.hpp"

#include <algorithm>
#include <utility>

namespace {

constexpr std::uint32_t kEsa = 0x2E0, kEea = 0x33C, kAddresses = 0x2E4;
constexpr std::uint32_t kEvoll = 0x4, kEvolr = 0x6, kCoefficients = 0x14;
constexpr std::uint32_t kStart = 0xFFC00, kSize = 0x400;   // the work area of core 1 in these tests

// Address registers in register order, by the names of the published formula.
enum { dAPF1, dAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF,
       mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, kAddressCount };
// What a filter writes at its destination is found at lower offsets later, until the next destination
// below overwrites it. So, going down the area: left same and its taps, right same and its taps, left
// diff, right diff, then the four all-pass buffers, each source inside the stretch its side wrote.
constexpr std::uint32_t kPreset[kAddressCount] = {0x40,  0x30,  0x3F0, 0x360, 0x3C0, 0x330, 0x370, 0x2E0, 0x390, 0x300, 0x2D0,
                                                  0x240, 0x2A0, 0x210, 0x270, 0x1E0, 0x250, 0x1C0, 0x1B0, 0x160, 0x110, 0xD0};
// Coefficient registers in register order.
enum { vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, vLIN, vRIN, kCoefficientCount };
constexpr std::uint16_t kGains[kCoefficientCount] = {0x5800, 0x4800, 0x4400, 0xB000, 0xB400, 0xC800, 0x5000, 0x4C00, 0x8000, 0x7000};

constexpr int kTaps = 39;
constexpr int kFilter[kTaps] = {-1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0, 1332, 0, -2960, 0, 10246, 16384,
                                10246, 0, -2960, 0, 1332, 0, -616, 0, 266, 0, -103, 0, 35, 0, -10, 0, 2, 0, -1};

int clamp(long long value) { return static_cast<int>(std::clamp<long long>(value, -32768, 32767)); }
long long mul(long long a, long long b) { return (a * b) >> 15; }

// The formula, written the slow and obvious way: whole histories, one array for the work area.
struct Reference {
    const std::uint32_t *preset;
    std::vector<std::int16_t> ram;
    std::vector<int> in[2], up[2];
    std::uint64_t clock = 0;
    bool enabled = true;

    explicit Reference(const std::uint32_t *preset = kPreset, std::uint32_t size = kSize) : preset(preset), ram(size) {}

    static long long filter(const std::vector<int> &history) {
        long long sum = 0;
        for (int i = 0; i < kTaps; ++i) {
            const long long index = static_cast<long long>(history.size()) - kTaps + i;
            if (index >= 0) sum += static_cast<long long>(kFilter[i]) * history[static_cast<std::size_t>(index)];
        }
        return sum;
    }
    std::int16_t &at(long long offset) {
        const long long size = static_cast<long long>(ram.size());
        return ram[static_cast<std::size_t>((((static_cast<long long>(clock >> 1) + offset) % size) + size) % size)];
    }
    std::pair<int, int> step(int left, int right) {
        in[0].push_back(clamp(left));
        in[1].push_back(clamp(right));
        const int side = static_cast<int>(clock & 1);
        const long long gain = static_cast<std::int16_t>(kGains[vLIN + side]);
        const long long input = mul(gain, clamp(filter(in[side]) >> 15));
        const long long iir = static_cast<std::int16_t>(kGains[vIIR]), wall = static_cast<std::int16_t>(kGains[vWALL]);
        const long long same_dst = preset[mLSAME + side], diff_dst = preset[mLDIFF + side];
        const long long same = mul(iir, input + mul(wall, at(preset[dLSAME + side])) - at(same_dst - 1)) + at(same_dst - 1);
        const long long diff = mul(iir, input + mul(wall, at(preset[dLDIFF + (1 - side)])) - at(diff_dst - 1)) + at(diff_dst - 1);
        long long out = 0;
        for (int n = 0; n < 4; ++n) {
            static const int kSource[4] = {mLCOMB1, mLCOMB2, mLCOMB3, mLCOMB4};
            out += mul(static_cast<std::int16_t>(kGains[vCOMB1 + n]), at(preset[kSource[n] + side]));
        }
        const long long apf1_dst = preset[mLAPF1 + side], apf1_src = apf1_dst - preset[dAPF1];
        const long long apf2_dst = preset[mLAPF2 + side], apf2_src = apf2_dst - preset[dAPF2];
        const long long g1 = static_cast<std::int16_t>(kGains[vAPF1]), g2 = static_cast<std::int16_t>(kGains[vAPF2]);
        const long long apf1 = out - mul(g1, at(apf1_src));
        out = at(apf1_src) + mul(g1, apf1);
        const long long apf2 = out - mul(g2, at(apf2_src));
        out = at(apf2_src) + mul(g2, apf2);
        if (enabled) {
            at(same_dst) = static_cast<std::int16_t>(clamp(same));
            at(diff_dst) = static_cast<std::int16_t>(clamp(diff));
            at(apf1_dst) = static_cast<std::int16_t>(clamp(apf1));
            at(apf2_dst) = static_cast<std::int16_t>(clamp(apf2));
        }
        up[side].push_back(clamp(out));
        up[1 - side].push_back(0);
        ++clock;
        return {clamp(filter(up[0]) >> 14), clamp(filter(up[1]) >> 14)};
    }
};

// Core 1 with the test preset, effects enabled, and nothing but its input area switched to the effects.
void fresh_with_reverb(std::uint32_t evol = 0x4000, std::uint32_t start = kStart, const std::uint32_t *preset = kPreset) {
    fresh();
    reg(1, kEsa, start >> 16);
    reg(1, kEsa + 2, start & 0xFFFFu);
    reg(1, kEea, 0x000F);
    for (int i = 0; i < kAddressCount; ++i) {
        reg(1, kAddresses + i * 4, preset[i] >> 16);
        reg(1, kAddresses + i * 4 + 2, preset[i] & 0xFFFFu);
    }
    for (int i = 0; i < kCoefficientCount; ++i) volume(1, kCoefficients + i * 2, kGains[i]);
    volume(1, kEvoll, evol);
    volume(1, kEvolr, evol);
    reg(1, kMmix, 0x0030);
    reg(1, kAttr, 0x0080);
}

// A made-up signal in core 1's input area: two different sequences, left and right.
int signal(unsigned i, int side) {
    std::uint32_t x = (i + 1) * 2654435761u + static_cast<std::uint32_t>(side) * 0x9E3779B9u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return static_cast<int>(x % 24001u) - 12000;
}
void fill_signal() {
    for (unsigned i = 0; i < kRing; ++i) {
        spu2::ram()[0x2400 + i] = static_cast<std::uint16_t>(signal(i, 0));
        spu2::ram()[0x2600 + i] = static_cast<std::uint16_t>(signal(i, 1));
    }
}

// What the mixer should put out for the reference's next sample: input volume, reverb, effect volume, master volume.
std::pair<int, int> expected(Reference &reference, unsigned sample, bool silent, int evol = 0x4000) {
    const int l = silent ? 0 : static_cast<int>(mul(signal(sample % kRing, 0), 0x7FFF));
    const int r = silent ? 0 : static_cast<int>(mul(signal(sample % kRing, 1), 0x7FFF));
    const auto [wet_l, wet_r] = reference.step(l, r);
    return {clamp(mul(mul(wet_l, evol), 0x7FFE)), clamp(mul(mul(wet_r, evol), 0x7FFE))};
}

int mismatches(Reference &reference, unsigned first, unsigned count, bool silent) {
    int wrong = 0;
    for (unsigned i = first; i < first + count; ++i) {
        const auto [l, r] = expected(reference, i, silent);
        if (g_out[i * 2] != l || g_out[i * 2 + 1] != r) ++wrong;
    }
    return wrong;
}

int work_area_differences(const Reference &reference, std::uint32_t start = kStart) {
    int wrong = 0;
    for (unsigned i = 0; i < reference.ram.size(); ++i)
        if (ram(start + i) != reference.ram[i]) ++wrong;
    return wrong;
}

bool heard(unsigned first, unsigned count) {
    for (unsigned i = first * 2; i < (first + count) * 2; ++i)
        if (g_out[i] != 0) return true;
    return false;
}

void the_reverb_follows_the_published_formula() {
    fresh_with_reverb();
    fill_signal();
    run(200);
    Reference reference;
    CHECK(heard(0, 200 * kBatch));
    CHECK_EQ(mismatches(reference, 0, 200 * kBatch, false), 0);
    CHECK_EQ(work_area_differences(reference), 0);
}

void only_the_work_area_is_written() {
    fresh_with_reverb();
    fill_signal();
    const std::vector<std::uint16_t> before(spu2::ram(), spu2::ram() + (1u << 20));
    run(200);
    int changed = 0;
    for (std::uint32_t a = 0x2800; a < kStart; ++a)   // above the mixer's write-back and input areas, below the work area
        if (spu2::ram()[a] != before[a]) ++changed;
    CHECK_EQ(changed, 0);
}

void with_effects_disabled_nothing_is_written_and_what_is_in_the_work_area_still_plays() {
    fresh_with_reverb();
    fill_signal();
    run(100);
    Reference reference;
    CHECK_EQ(mismatches(reference, 0, 100 * kBatch, false), 0);
    reg(1, kAttr, 0x0000);
    reference.enabled = false;
    run(100);
    CHECK(heard(100 * kBatch, 100 * kBatch));
    CHECK_EQ(mismatches(reference, 100 * kBatch, 100 * kBatch, false), 0);
    CHECK_EQ(work_area_differences(reference), 0);
}

void with_effects_disabled_and_no_effect_volume_the_reverb_does_not_run() {
    fresh_with_reverb(0);
    reg(1, kAttr, 0x0000);
    fill_signal();
    run(50);
    CHECK(!heard(0, 50 * kBatch));
    int written = 0;
    for (unsigned i = 0; i < kSize; ++i)
        if (ram(kStart + i) != 0) ++written;
    CHECK_EQ(written, 0);
}

void after_a_reset_the_reverb_is_silent_and_writes_nothing() {
    fresh();
    start_voice(0, 0, 0x5000, 0x3FFF, 0x3FFF);
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    reg32(0, kVmixel, 0xFFFFFFu);
    reg32(1, kVmixel, 0xFFFFFFu);
    reg(0, kMmix, 0x0FF0);
    reg(1, kMmix, 0x0FFF);
    run(50);
    int written = 0;
    for (std::uint32_t a = 0xE0000; a < 0x100000; ++a)
        if (spu2::ram()[a] != 0) ++written;
    CHECK_EQ(written, 0);
    CHECK(about(left(), kCore0Voice + kCore1Voice));
}

void the_effect_switches_choose_what_feeds_the_reverb() {
    // Voices by their effect-send switches and MMIX bits 9 and 8.
    fresh_with_reverb();
    reg32(1, kVmixl, 0);
    reg32(1, kVmixr, 0);
    reg32(1, kVmixel, 0x000001u);
    reg32(1, kVmixer, 0x000001u);
    start_voice(1, 0, 0x5000, 0x3FFF, 0x3FFF);
    run(60);
    CHECK(!heard(0, 60 * kBatch));                 // MMIX sends only the input, which is silent
    reg(1, kMmix, 0x0300);
    run(60);
    CHECK(heard(60 * kBatch, 60 * kBatch));
    // Core 0's output by MMIX bits 1 and 0.
    fresh_with_reverb();
    start_voice(0, 0, 0x5000, 0x3FFF, 0x3FFF);
    reg(1, kMmix, 0x0000);
    run(60);
    CHECK(!heard(0, 60 * kBatch));
    reg(1, kMmix, 0x0002);
    run(60);
    CHECK(heard(60 * kBatch, 60 * kBatch));
}

void the_effect_volume_scales_and_can_invert_the_return() {
    fresh_with_reverb(0x4000);
    fill_signal();
    run(100);
    const std::vector<std::int16_t> half = g_out;
    fresh_with_reverb(0xC000);
    fill_signal();
    run(100);
    int wrong = 0;
    for (std::size_t i = 0; i < half.size(); ++i)
        if (std::abs(g_out[i] + half[i]) > 2) ++wrong;
    CHECK_EQ(wrong, 0);
    fresh_with_reverb(0x0000);
    fill_signal();
    run(100);
    CHECK(!heard(0, 100 * kBatch));
}

void the_reverb_is_added_to_the_dry_mix() {
    fresh_with_reverb();
    fill_signal();
    reg(1, kMmix, 0x00F0);                          // the input dry and to the effects
    run(100);
    Reference reference;
    int wrong = 0;
    for (unsigned i = 0; i < 100 * kBatch; ++i) {
        const int l = static_cast<int>(mul(signal(i % kRing, 0), 0x7FFF)), r = static_cast<int>(mul(signal(i % kRing, 1), 0x7FFF));
        const auto [wet_l, wet_r] = reference.step(l, r);
        if (g_out[i * 2] != clamp(mul(l + mul(wet_l, 0x4000), 0x7FFE)) || g_out[i * 2 + 1] != clamp(mul(r + mul(wet_r, 0x4000), 0x7FFE))) ++wrong;
    }
    CHECK_EQ(wrong, 0);
}

void an_address_interrupt_in_the_work_area_fires_while_effects_are_enabled() {
    fresh_with_reverb();
    reg(1, kAttr, 0x0000);
    reg(0, kIrqa, kStart >> 16);
    reg(0, kIrqa + 2, (kStart + 0x200) & 0xFFFFu);
    reg(0, kAttr, 0x0040);
    run(80);                                        // the network passes every address of the area in 2048 samples
    CHECK(g_interrupts.empty());
    reg(1, kAttr, 0x0080);
    run(80);
    CHECK_EQ(g_interrupts.size(), 1u);
    CHECK_EQ(spu2::read(kRegisters + 0x7C2, 2), 4u);
}

void a_saved_state_carries_the_reverb() {
    fresh_with_reverb();
    fill_signal();
    run(60);
    ps2::state::Writer saved;
    spu2::state(saved);
    run(60);
    const std::vector<std::int16_t> first(g_out.begin() + 60 * kBatch * 2, g_out.end());
    fill(0x2400, 0);                                // make sure the restore brings everything back
    volume(1, kCoefficients, 0);
    ps2::state::Reader reader(saved.out.data(), saved.out.size());
    spu2::state(reader);
    g_out.clear();
    run(60);
    CHECK(g_out == first);
}

void a_state_of_the_previous_version_loads_with_the_reverb_cleared() {
    fresh_with_reverb();
    fill_signal();
    run(60);
    ps2::state::Writer old;
    old.version = 4;
    spu2::state(old);
    std::uint32_t mark = 0x600DF00Du;
    old.pod(mark);                                  // what follows the SPU2 section in a state file
    ps2::state::Writer current;
    spu2::state(current);
    CHECK(old.out.size() - 4 < current.out.size());
    ps2::state::Reader reader(old.out.data(), old.out.size());
    reader.version = 4;
    spu2::state(reader);                            // must consume exactly the old layout
    std::uint32_t next = 0;
    reader.pod(next);
    CHECK_EQ(next, mark);
    CHECK_EQ(spu2::read(kRegisters + 0x760 + 0x28 + kCoefficients, 2), 0u);   // the coefficients were not in old states
    CHECK_EQ(reg(1, kAttr), 0x0080u);               // the rest of the state came back
}

// Offsets the hall presets never use: at and beyond the size of the area (up to the largest a register holds), a
// reflection destination at 0 (the filter reads the halfword before it), all-pass destinations below their sizes.
constexpr std::uint32_t kOddStart = 0xFFC2B, kOddSize = 0x3D5;
constexpr std::uint32_t kOddPreset[kAddressCount] = {0x60,  0xFFFFFFF0, 0x0,     0x12345, 0x150, 0x2A0,      0x3D4,      0x3D5, 0x7A9, 0x80000000, 0x3D5,
                                                     0x3D4, 0x10000,    0x1FFFF, 0x250,   0x333, 0x7FFFFFFF, 0xFFFFFFFE, 0x20,  0x30,  0x100,      0xFFFFFFFF};

void offsets_outside_the_work_area_wrap_into_it() {
    fresh_with_reverb(0x4000, kOddStart, kOddPreset);
    fill_signal();
    const std::vector<std::uint16_t> before(spu2::ram(), spu2::ram() + (1u << 20));
    run(200);                                       // the origin goes round the area three times
    Reference reference(kOddPreset, kOddSize);
    CHECK(heard(0, 200 * kBatch));
    CHECK_EQ(mismatches(reference, 0, 200 * kBatch, false), 0);
    CHECK_EQ(work_area_differences(reference, kOddStart), 0);
    int changed = 0;
    for (std::uint32_t a = 0x2800; a < kOddStart; ++a)
        if (spu2::ram()[a] != before[a]) ++changed;
    CHECK_EQ(changed, 0);
}

void the_smallest_work_area_holds_the_whole_network() {
    constexpr std::uint32_t kTinyStart = 0xFFFFE;   // two halfwords
    fresh_with_reverb(0x4000, kTinyStart, kOddPreset);
    fill_signal();
    run(100);
    Reference reference(kOddPreset, 2);
    CHECK(heard(0, 100 * kBatch));
    CHECK_EQ(mismatches(reference, 0, 100 * kBatch, false), 0);
    CHECK_EQ(work_area_differences(reference, kTinyStart), 0);
    CHECK_EQ(ram(kTinyStart - 1), 0);
}

void a_reverb_that_was_not_run_for_a_while_goes_on_from_its_histories() {
    fresh_with_reverb();
    fill_signal();
    run(50);
    Reference reference;
    CHECK_EQ(mismatches(reference, 0, 50 * kBatch, false), 0);
    for (unsigned gap : {1u, 3u}) {
        reg(1, kAttr, 0x0000);                      // effects disabled and no effect volume: the network is not run
        volume(1, kEvoll, 0);
        volume(1, kEvolr, 0);
        run(gap);
        CHECK(!heard(static_cast<unsigned>(g_out.size() / 2) - gap * kBatch, gap * kBatch));
        reference.clock += gap * kBatch;            // the origin of the work area moved on; the filters heard nothing
        reg(1, kAttr, 0x0080);
        volume(1, kEvoll, 0x4000);
        volume(1, kEvolr, 0x4000);
        const unsigned first = static_cast<unsigned>(g_out.size() / 2);
        run(50);
        CHECK_EQ(mismatches(reference, first, 50 * kBatch, false), 0);
    }
    CHECK_EQ(work_area_differences(reference), 0);
}

namespace work_area = spu2::work_area;

// The address of a tap as the mixer used to work it out, with two 64-bit divisions each time: the origin of the
// area plus an offset that may be negative or far beyond the area, wrapped into the area. Kept as the reference
// for the arithmetic in spu2_work_area.hpp, which wraps each offset once and then only adds and compares.
std::uint32_t division_address(std::uint32_t start, std::int64_t size, std::int64_t origin, std::int64_t relative) {
    return start + static_cast<std::uint32_t>((((origin + relative) % size) + size) % size);
}

// The three kinds of address the network forms, the new way: at an offset register, at the halfword before one, and
// at one register less another (an all-pass filter's source: its destination less its size).
struct WorkArea {
    std::uint32_t start, size, origin;

    WorkArea(std::uint32_t start, std::uint32_t size, std::uint32_t clock) : start(start), size(size), origin(work_area::wrap(clock >> 1, size)) {}
    std::uint32_t at(std::uint32_t offset) const { return start + work_area::add(origin, work_area::wrap(offset, size), size); }
    std::uint32_t before(std::uint32_t offset) const { return start + work_area::add(origin, work_area::sub(work_area::wrap(offset, size), 1, size), size); }
    std::uint32_t less(std::uint32_t offset, std::uint32_t by) const {
        return start + work_area::add(origin, work_area::sub(work_area::wrap(offset, size), work_area::wrap(by, size), size), size);
    }
};

std::uint32_t g_random = 0x2545F491u;
std::uint32_t random32() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

struct Compared {
    unsigned long long addresses = 0, different = 0;

    void check(std::uint32_t address, std::uint32_t wanted) {
        ++addresses;
        if (address != wanted) ++different;
    }
    // One area and sample clock, the two ways, over the given offsets: each, the halfword before each, and each
    // less `pairs` of the others (all of them when pairs is the count).
    void area(std::uint32_t start, std::uint32_t size, std::uint32_t clock, const std::uint32_t *offsets, std::size_t count, std::size_t pairs) {
        const WorkArea area(start, size, clock);
        const std::int64_t origin = clock >> 1;
        for (std::size_t i = 0; i < count; ++i) {
            const std::int64_t offset = offsets[i];
            check(area.at(offsets[i]), division_address(start, size, origin, offset));
            check(area.before(offsets[i]), division_address(start, size, origin, offset - 1));
            for (std::size_t j = 0; j < pairs; ++j) {
                const std::uint32_t by = offsets[(i + 1 + j * 3) % count];
                check(area.less(offsets[i], by), division_address(start, size, origin, offset - static_cast<std::int64_t>(by)));
            }
        }
    }
};

void the_work_area_arithmetic_gives_the_addresses_of_the_division_formula() {
    // A work area is ESA to the end of a 64K block, so every size from 2 halfwords to the whole of sound RAM can
    // be set; an offset register holds any 32-bit value; the sample clock is 32 bits. The start of the area is
    // only added on at the end, so in the first two loops it is the one that puts the area's end at the top of RAM.
    constexpr std::uint32_t kTop = 1u << 20;
    Compared compared;
    // Every size: offsets at the edges of the area and of 32 bits and two at random, at a clock from the edges
    // and one at random.
    for (std::uint32_t size = 2; size <= kTop; ++size) {
        const std::uint32_t offsets[] = {0, 1, size - 1, size, size + 1, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, random32(), random32() % (4 * size)};
        const std::uint32_t edges[] = {0, 2 * size - 2, 2 * size - 1, 2 * size + 1, 0xFFFFFFFEu, 0xFFFFFFFFu};
        compared.area(kTop - size, size, edges[size % std::size(edges)], offsets, std::size(offsets), 2);
        compared.area(kTop - size, size, random32(), offsets, std::size(offsets), 2);
    }
    // Small areas: every offset and every clock of the first few turns, each offset less every other.
    for (std::uint32_t size = 2; size <= 24; ++size) {
        std::uint32_t offsets[3 * 24 + 2];
        const std::size_t count = 3 * size + 2;
        for (std::uint32_t i = 0; i < count; ++i) offsets[i] = i;
        for (std::uint32_t clock = 0; clock < 4 * size + 4; ++clock) compared.area(kTop - size, size, clock, offsets, count, count);
    }
    // The sixteen areas of whole blocks, one of 0x56F0 halfwords (a hall preset's, times four) and sizes at
    // random, anywhere in RAM: offsets and clocks at random, each offset less every other.
    for (unsigned n = 0; n < 2000; ++n) {
        const std::uint32_t size = n < 16 ? (n + 1) << 16 : n == 16 ? 0x56F0u : 2 + random32() % (kTop - 1);
        const std::uint32_t start = n < 17 ? kTop - size : random32() % (kTop - size + 1);
        std::uint32_t offsets[22];
        for (std::uint32_t &offset : offsets) offset = random32() >> (random32() % 28);
        for (int i = 0; i < 4; ++i) compared.area(start, size, random32(), offsets, std::size(offsets), std::size(offsets));
    }
    CHECK_EQ(compared.different, 0u);
    CHECK(compared.addresses > 80'000'000ull);
}

}

int main() {
    the_reverb_follows_the_published_formula();
    only_the_work_area_is_written();
    with_effects_disabled_nothing_is_written_and_what_is_in_the_work_area_still_plays();
    with_effects_disabled_and_no_effect_volume_the_reverb_does_not_run();
    after_a_reset_the_reverb_is_silent_and_writes_nothing();
    the_effect_switches_choose_what_feeds_the_reverb();
    the_effect_volume_scales_and_can_invert_the_return();
    the_reverb_is_added_to_the_dry_mix();
    an_address_interrupt_in_the_work_area_fires_while_effects_are_enabled();
    a_saved_state_carries_the_reverb();
    a_state_of_the_previous_version_loads_with_the_reverb_cleared();
    offsets_outside_the_work_area_wrap_into_it();
    the_smallest_work_area_holds_the_whole_network();
    a_reverb_that_was_not_run_for_a_while_goes_on_from_its_histories();
    the_work_area_arithmetic_gives_the_addresses_of_the_division_formula();
    return check::done("test_spu2_reverb");
}
