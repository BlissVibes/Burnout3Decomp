// Standalone test for b3::score::stamped_value_if_fresh (retail 0x001553A0).
//
// Expected values are derived from the specification, never from running the game. The rule:
//   result = 0                     when the stamp (+1520) is the bit pattern -1.0f;
//   result = 0                     when stamp + window (EE add) is strictly below the clock (+12)
//                                  under EE ordering (sign-magnitude, denormals flushed, -0 == +0,
//                                  exponent 255 is a huge finite value);
//   result = the word at +1516     otherwise (expiry == clock counts as fresh).
// Each case says which rule produces its expected value. The EE add truncates (no rounding),
// flushes denormal operands, saturates to sign|0x7FFFFFFF instead of overflowing, and yields a
// signed zero on underflow or exact cancellation; the cases that depend on those properties spell
// out the hand derivation.
//
// Guest memory comes from tests/unit/support (ps2::g_ram); the object is written with ee::st and
// the function reads it through ee::guest_ptr.

#include "hooks/score/stamped_value_if_fresh.hpp"

#include "check.hpp"
#include "ee/f32.hpp"
#include "ee/guest.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using b3::score::kClockOffset;
using b3::score::kStampedValueOffset;
using b3::score::kStampTimeOffset;
using b3::score::StampedSlotOwner;
using b3::score::stamped_value_if_fresh;
using ee::f32;
using ee::u32;

namespace {

constexpr std::uint32_t kObject = 0x00200000u;        // where the object sits in guest RAM
constexpr std::uint32_t kValue = 0x01A3C840u;          // a plausible guest pointer, non-zero
constexpr std::uint32_t kSentinel = 0xBF800000u;       // -1.0f
constexpr std::uint32_t kEeMax = 0x7FFFFFFFu;          // the EE's saturated positive result

constexpr std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }

void clear() {
    std::memset(ps2::g_ram, 0, ps2::kRamSize);
    std::memset(ps2::g_spr, 0, ps2::kScratchpadSize);
}

// Writes the three fields at `object` and calls the function with `window`.
std::uint32_t run_at(std::uint32_t object, std::uint32_t stamp_bits, std::uint32_t clock_bits,
                     std::uint32_t window_bits, std::uint32_t value) {
    ee::st<u32>(object + kStampTimeOffset, stamp_bits);
    ee::st<u32>(object + kClockOffset, clock_bits);
    ee::st<u32>(object + kStampedValueOffset, value);
    return stamped_value_if_fresh(ee::guest_ptr<StampedSlotOwner>{object}, f32::from_bits(window_bits));
}

std::uint32_t run(std::uint32_t stamp_bits, std::uint32_t clock_bits, std::uint32_t window_bits,
                  std::uint32_t value = kValue) {
    return run_at(kObject, stamp_bits, clock_bits, window_bits, value);
}

// Rule 1: the -1.0f stamp means "empty", whatever the clock, window and stored word hold.
void sentinel_stamp_returns_zero() {
    clear();
    CHECK_EQ(run(kSentinel, bits(0.0f), bits(0.0f)), 0u);
    CHECK_EQ(run(kSentinel, bits(100.0f), bits(1000.0f)), 0u);  // would be fresh if tested
    CHECK_EQ(run(kSentinel, 0x80000000u, kEeMax), 0u);          // saturating window, -0 clock
    CHECK_EQ(run(kSentinel, 0xFFFFFFFFu, 0xFFFFFFFFu), 0u);
    CHECK_EQ(run(kSentinel, bits(1.0f), bits(1.0f), 0xFFFFFFFFu), 0u);  // stored word ignored
    // The fields the sentinel path never reads may hold anything, including the sentinel itself.
    CHECK_EQ(run(kSentinel, kSentinel, kSentinel, kSentinel), 0u);
}

// Rule 1 is an exact-pattern test: neighbours of -1.0f, +1.0f and -1.0f's flushed relatives are
// ordinary stamps. With a zero window and a clock equal to the stamp (rule 3, expiry == clock) the
// word comes back, so a false sentinel match would show as 0.
void near_sentinel_patterns_are_ordinary_stamps() {
    clear();
    for (std::uint32_t stamp : {0xBF800001u, 0xBF7FFFFFu, 0x3F800000u, 0xBF000000u, 0xBFC00000u,
                                0x3F800001u, 0x7F800000u, 0xFF800000u}) {
        CHECK_EQ(run(stamp, stamp, bits(0.0f)), kValue);
    }
}

// Rules 2 and 3 on ordinary values: expiry = stamp + window compared with the clock.
void ordinary_fresh_and_stale() {
    clear();
    // stamp 10, window 5 -> expiry 15.
    CHECK_EQ(run(bits(10.0f), bits(12.0f), bits(5.0f)), kValue);   // 15 >= 12: fresh
    CHECK_EQ(run(bits(10.0f), bits(16.0f), bits(5.0f)), 0u);       // 15 < 16: stale
    CHECK_EQ(run(bits(10.0f), bits(15.0f), bits(5.0f)), kValue);   // 15 == 15: fresh (not <)
    CHECK_EQ(run(bits(10.0f), 0x41700001u, bits(5.0f)), 0u);       // clock one ulp above 15: stale
    CHECK_EQ(run(bits(10.0f), 0x416FFFFFu, bits(5.0f)), kValue);   // clock one ulp below 15: fresh
    CHECK_EQ(run(bits(10.0f), bits(0.0f), bits(5.0f)), kValue);    // clock before the stamp: fresh
    CHECK_EQ(run(bits(10.0f), bits(-3.0f), bits(5.0f)), kValue);   // negative clock: fresh
    // Fractions: 0.25 + 0.5 = 0.75 exactly in binary.
    CHECK_EQ(run(bits(0.25f), bits(0.75f), bits(0.5f)), kValue);
    CHECK_EQ(run(bits(0.25f), bits(0.8f), bits(0.5f)), 0u);
    // The stored word is whatever is there, including zero and the all-ones pattern.
    CHECK_EQ(run(bits(10.0f), bits(12.0f), bits(5.0f), 0u), 0u);
    CHECK_EQ(run(bits(10.0f), bits(12.0f), bits(5.0f), 0xFFFFFFFFu), 0xFFFFFFFFu);
    CHECK_EQ(run(bits(10.0f), bits(12.0f), bits(5.0f), 1u), 1u);
}

// Negative stamps other than -1.0f are ordinary: stamp -2, window 1 -> expiry -1 (the sentinel
// value as an expiry is not special, only the stamp is tested against it).
void negative_stamps_and_expiries() {
    clear();
    CHECK_EQ(run(bits(-2.0f), bits(-1.0f), bits(1.0f)), kValue);   // -1 == -1: fresh
    CHECK_EQ(run(bits(-2.0f), bits(-0.5f), bits(1.0f)), 0u);       // -1 < -0.5: stale
    CHECK_EQ(run(bits(-2.0f), bits(-1.5f), bits(1.0f)), kValue);   // -1 >= -1.5: fresh
    CHECK_EQ(run(bits(-2.0f), bits(0.0f), bits(1.0f)), 0u);        // -1 < 0: stale
    CHECK_EQ(run(bits(0.0f), bits(-1.0f), bits(-1.0f)), kValue);   // expiry -1 == clock -1
    CHECK_EQ(run(bits(0.0f), bits(-0.0f), bits(-1.0f)), 0u);       // -1 < -0 (== +0): stale
    CHECK_EQ(run(bits(-100.0f), bits(-100.0f), bits(0.0f)), kValue);
    CHECK_EQ(run(bits(-100.0f), 0xC2C7FFFFu, bits(0.0f)), 0u);     // clock one ulp above -100
}

// A negative window pulls the expiry below the stamp, so a slot stamped at the current clock
// is already stale: stamp 10, window -3 -> expiry 7.
void negative_window() {
    clear();
    CHECK_EQ(run(bits(10.0f), bits(10.0f), bits(-3.0f)), 0u);      // 7 < 10: stale
    CHECK_EQ(run(bits(10.0f), bits(7.0f), bits(-3.0f)), kValue);   // 7 == 7: fresh
    CHECK_EQ(run(bits(10.0f), bits(6.0f), bits(-3.0f)), kValue);   // 7 >= 6: fresh
    CHECK_EQ(run(bits(10.0f), 0x40E00001u, bits(-3.0f)), 0u);      // clock one ulp above 7: stale
    // Window more negative than the stamp: 10 + (-20) = -10.
    CHECK_EQ(run(bits(10.0f), bits(-10.0f), bits(-20.0f)), kValue);
    CHECK_EQ(run(bits(10.0f), bits(-9.0f), bits(-20.0f)), 0u);
}

// A zero window of either sign leaves expiry == stamp, so the word is returned only while the
// clock has not moved past the stamp. A denormal window is flushed to zero by the add and acts
// the same way. Zero + zero: the sum is -0 only when both operands are negative, and -0 == +0
// under EE ordering, so the sign of a zero expiry never changes the outcome.
void zero_and_denormal_windows() {
    clear();
    for (std::uint32_t window : {0x00000000u, 0x80000000u, 0x00000001u, 0x007FFFFFu, 0x80000001u,
                                 0x807FFFFFu}) {
        CHECK_EQ(run(bits(10.0f), bits(10.0f), window), kValue);   // expiry 10 == clock 10
        CHECK_EQ(run(bits(10.0f), 0x41200001u, window), 0u);       // clock one ulp above 10
        CHECK_EQ(run(bits(10.0f), 0x411FFFFFu, window), kValue);   // clock one ulp below 10
        // Zero stamp: expiry is a zero; every zero and denormal clock compares equal to it.
        CHECK_EQ(run(bits(0.0f), bits(0.0f), window), kValue);
        CHECK_EQ(run(bits(0.0f), 0x80000000u, window), kValue);
        CHECK_EQ(run(bits(0.0f), 0x00000001u, window), kValue);    // denormal clock flushes to 0
        CHECK_EQ(run(bits(0.0f), 0x807FFFFFu, window), kValue);
        CHECK_EQ(run(bits(0.0f), 0x00800000u, window), 0u);        // smallest normal clock: stale
        CHECK_EQ(run(bits(0.0f), 0x80800000u, window), kValue);    // smallest negative normal: fresh
        CHECK_EQ(run(0x80000000u, bits(0.0f), window), kValue);    // -0 stamp, +0 clock
    }
    // A denormal stamp is flushed by the add as well: expiry = 0 + window.
    CHECK_EQ(run(0x00000001u, bits(1.0f), bits(1.0f)), kValue);    // 0 + 1 == 1
    CHECK_EQ(run(0x00000001u, 0x3F800001u, bits(1.0f)), 0u);
    CHECK_EQ(run(0x807FFFFFu, bits(0.0f), bits(0.0f)), kValue);    // -denormal + 0 -> zero
}

// The EE add truncates the aligned mantissa instead of rounding to nearest. 1.0 + 1.5 * 2^-24:
// IEEE rounds up to 1 + 2^-23 (0x3F800001); the EE drops the shifted-out bits and keeps 1.0
// (0x3F800000). With the clock at 0x3F800001 the EE expiry is strictly below it (stale), where
// host float arithmetic would call the slot fresh.
void add_truncates() {
    clear();
    constexpr std::uint32_t kOneAndHalfUlpBelow = 0x33C00000u;  // 1.5 * 2^-24
    CHECK_EQ(run(bits(1.0f), 0x3F800001u, kOneAndHalfUlpBelow), 0u);
    CHECK_EQ(run(bits(1.0f), bits(1.0f), kOneAndHalfUlpBelow), kValue);   // expiry 1.0 == clock 1.0
    // 1.0 + 2^-23 is exact (0x3F800001) and so meets a clock of 0x3F800001.
    CHECK_EQ(run(bits(1.0f), 0x3F800001u, 0x34000000u), kValue);
    CHECK_EQ(run(bits(1.0f), 0x3F800002u, 0x34000000u), 0u);
    // Operands more than 25 exponent steps apart: the smaller vanishes. 2^100 + 1.0 == 2^100.
    CHECK_EQ(run(0x71800000u, 0x71800000u, bits(1.0f)), kValue);
    CHECK_EQ(run(0x71800000u, 0x71800001u, bits(1.0f)), 0u);
}

// Large magnitudes. 2^100 + 2^100 = 2^101 exactly (0x72000000). Beyond the top exponent the add
// saturates to 0x7FFFFFFF instead of producing infinity, and nothing orders above that pattern,
// so a huge window is always fresh. An exponent-255 operand is finite on the EE: 1.0 + 0x7F800000
// keeps 0x7F800000 (the 1.0 is shifted out), which equals a clock of 0x7F800000 and is below
// 0x7F800001.
void large_values_and_saturation() {
    clear();
    CHECK_EQ(run(0x71800000u, 0x72000000u, 0x71800000u), kValue);   // 2^101 == 2^101
    CHECK_EQ(run(0x71800000u, 0x72000001u, 0x71800000u), 0u);       // one ulp above 2^101
    CHECK_EQ(run(0x7F7FFFFFu, kEeMax, 0x7F7FFFFFu), kValue);        // saturated expiry == max clock
    CHECK_EQ(run(0x7F7FFFFFu, 0x7F800000u, 0x7F7FFFFFu), kValue);   // IEEE "infinity" clock is below max
    CHECK_EQ(run(bits(1.0f), 0x7F800000u, 0x7F800000u), kValue);    // expiry 0x7F800000 == clock
    CHECK_EQ(run(bits(1.0f), 0x7F800001u, 0x7F800000u), 0u);        // clock one pattern higher: stale
    CHECK_EQ(run(bits(1.0f), kEeMax, 0x7F800000u), 0u);
    CHECK_EQ(run(bits(1.0f), bits(1.0f), kEeMax), kValue);          // saturating window, small clock
    // True overflow: 2^128 + 2^128 would need exponent 256, so the EE saturates to 0x7FFFFFFF.
    CHECK_EQ(run(0x7F800000u, kEeMax, 0x7F800000u), kValue);        // saturated expiry == max clock
    CHECK_EQ(run(0x7F800000u, 0x7FFFFFFEu, 0x7F800000u), kValue);
    CHECK_EQ(run(0xFF800000u, 0xFFFFFFFFu, 0xFF800000u), kValue);   // negative overflow: 0xFFFFFFFF
    CHECK_EQ(run(0xFF800000u, 0xFFFFFFFEu, 0xFF800000u), 0u);       // one pattern less negative: stale
    // Negative saturation: -max + -max = 0xFFFFFFFF, below every other pattern.
    CHECK_EQ(run(0xFF7FFFFFu, 0xFFFFFFFFu, 0xFF7FFFFFu), kValue);   // equal to the saturated pattern
    CHECK_EQ(run(0xFF7FFFFFu, 0xFF7FFFFFu, 0xFF7FFFFFu), 0u);       // any larger clock: stale
}

// Patterns a host would call NaN (exponent 255, non-zero mantissa) are huge finite values on the
// EE. As stamps they are never the sentinel; as expiries and clocks they order by magnitude.
void host_nan_patterns_are_finite() {
    clear();
    CHECK_EQ(run(0x7FC00000u, bits(0.0f), bits(0.0f)), kValue);     // huge positive expiry, zero clock
    CHECK_EQ(run(0x7FC00000u, 0x7FC00000u, bits(0.0f)), kValue);    // equal patterns: fresh
    CHECK_EQ(run(0x7FC00000u, 0x7FC00001u, bits(0.0f)), 0u);        // one pattern higher: stale
    CHECK_EQ(run(0xFFFFFFFFu, kSentinel, bits(0.0f)), 0u);          // -huge < -1.0: stale
    CHECK_EQ(run(0xFFFFFFFFu, 0xFFFFFFFFu, bits(0.0f)), kValue);    // equal: fresh
    CHECK_EQ(run(0xFFC00000u, 0xFFFFFFFFu, bits(0.0f)), kValue);    // -huge >= -(huger): fresh
    // A NaN-pattern window behaves like a huge finite addend: 1.0 + 0x7FC00000 keeps 0x7FC00000.
    CHECK_EQ(run(bits(1.0f), 0x7FC00000u, 0x7FC00000u), kValue);
    CHECK_EQ(run(bits(1.0f), 0x7FC00001u, 0x7FC00000u), 0u);
    // A NaN-pattern clock is just a very large clock: everything ordinary is stale against it.
    CHECK_EQ(run(bits(1.0f), 0x7FC00000u, bits(1.0f)), 0u);
    CHECK_EQ(run(bits(1.0f), 0xFFC00000u, bits(1.0f)), kValue);     // very negative clock: fresh
}

// Exact cancellation yields +0, and a difference too small for a normal result underflows to a
// signed zero; either way the expiry equals every zero and denormal clock.
void cancellation_and_underflow() {
    clear();
    CHECK_EQ(run(bits(5.0f), bits(0.0f), bits(-5.0f)), kValue);     // 5 - 5 = +0 == +0
    CHECK_EQ(run(bits(5.0f), 0x80000000u, bits(-5.0f)), kValue);    // +0 == -0
    CHECK_EQ(run(bits(5.0f), 0x00000001u, bits(-5.0f)), kValue);    // denormal clock flushes to 0
    CHECK_EQ(run(bits(5.0f), 0x00800000u, bits(-5.0f)), 0u);        // smallest normal clock: stale
    CHECK_EQ(run(bits(-5.0f), bits(0.0f), bits(5.0f)), kValue);
    // 0x00800001 - 0x00800000 = 2^-149, below the smallest normal: the EE gives +0.
    CHECK_EQ(run(0x00800001u, bits(0.0f), 0x80800000u), kValue);
    CHECK_EQ(run(0x00800001u, 0x80000000u, 0x80800000u), kValue);
    CHECK_EQ(run(0x00800001u, 0x00800000u, 0x80800000u), 0u);
    // The mirror image gives -0, which still equals +0.
    CHECK_EQ(run(0x80800001u, bits(0.0f), 0x00800000u), kValue);
    CHECK_EQ(run(0x80800001u, 0x80800000u, 0x00800000u), kValue);   // -0 >= -smallest normal
}

// The stored word is returned as a 32-bit guest address; the shim sign-extends it into the
// 64-bit result register, so a word with bit 31 set comes back with the upper half all ones
// (not an EE user address, but the register contract must hold for any word).
void return_value_width() {
    clear();
    const std::uint32_t word = run(bits(1.0f), bits(1.0f), bits(0.0f), 0x80000000u);
    CHECK_EQ(word, 0x80000000u);
    const auto as_register = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(word)));
    CHECK_EQ(as_register, 0xFFFFFFFF80000000ull);
    const std::uint32_t small = run(bits(1.0f), bits(1.0f), bits(0.0f), 0x7FFFFFFFu);
    CHECK_EQ(static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(small))), 0x7FFFFFFFull);
}

// The object may sit anywhere in guest RAM, including address 0 (a null owner reads the low page,
// which is ordinary memory in the fork) and the scratchpad.
void object_placement() {
    clear();
    CHECK_EQ(run_at(0u, bits(2.0f), bits(2.0f), bits(0.0f), kValue), kValue);
    CHECK_EQ(run_at(0u, kSentinel, bits(2.0f), bits(0.0f), kValue), 0u);
    CHECK_EQ(run_at(0x70000000u, bits(2.0f), bits(3.0f), bits(1.0f), kValue), kValue);
    CHECK_EQ(run_at(0x70000000u, bits(2.0f), bits(3.5f), bits(1.0f), kValue), 0u);
    CHECK_EQ(run_at(ps2::kRamSize - 1524u, bits(2.0f), bits(2.0f), bits(0.0f), kValue), kValue);
    // Two objects side by side do not disturb each other's fields.
    ee::st<u32>(kObject + 2048u + kStampTimeOffset, kSentinel);
    CHECK_EQ(run(bits(1.0f), bits(1.0f), bits(0.0f)), kValue);
    CHECK_EQ(run_at(kObject + 2048u, bits(1.0f), bits(1.0f), bits(0.0f), 7u), 7u);
}

// The function writes nothing: every byte of the object and its surroundings is as we left it.
void no_writes() {
    clear();
    std::memset(ps2::g_ram + kObject - 64, 0xA5, 1600 + 128);
    ee::st<u32>(kObject + kStampTimeOffset, bits(3.0f));
    ee::st<u32>(kObject + kClockOffset, bits(3.0f));
    ee::st<u32>(kObject + kStampedValueOffset, kValue);
    std::uint8_t before[1600 + 128];
    std::memcpy(before, ps2::g_ram + kObject - 64, sizeof before);
    CHECK_EQ(stamped_value_if_fresh(ee::guest_ptr<StampedSlotOwner>{kObject}, f32::lit(0.0f)), kValue);
    CHECK_EQ(stamped_value_if_fresh(ee::guest_ptr<StampedSlotOwner>{kObject}, f32::lit(-1.0f)), 0u);
    ee::st<u32>(kObject + kStampTimeOffset, kSentinel);
    before[64 + kStampTimeOffset + 0] = 0x00;
    before[64 + kStampTimeOffset + 1] = 0x00;
    before[64 + kStampTimeOffset + 2] = 0x80;
    before[64 + kStampTimeOffset + 3] = 0xBF;
    CHECK_EQ(stamped_value_if_fresh(ee::guest_ptr<StampedSlotOwner>{kObject}, f32::lit(0.0f)), 0u);
    CHECK(std::memcmp(before, ps2::g_ram + kObject - 64, sizeof before) == 0);
}

// An independent transcription of the rule on the fork's fp primitives (the spec names exactly
// these three operations), swept over a pool of special patterns in every role. This is a
// consistency check of our C++ against the written rule, not against the game.
std::uint32_t model(std::uint32_t stamp, std::uint32_t clock, std::uint32_t window, std::uint32_t value) {
    namespace fp = ps2::fp;
    if (fp::key(0xBF800000u) == fp::key(stamp)) return 0;
    const std::uint32_t expiry = fp::add(stamp, window);
    if (fp::key(expiry) < fp::key(clock)) return 0;
    return value;
}

void pool_sweep_against_model() {
    clear();
    constexpr std::uint32_t pool[] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu,
        0x00800000u, 0x80800000u, 0x00800001u, 0x80800001u, 0x33C00000u, 0x34000000u,
        0x3F800000u, 0xBF800000u, 0x3F800001u, 0xBF800001u, 0x3F7FFFFFu, 0xBF7FFFFFu,
        0x40000000u, 0xC0000000u, 0x40A00000u, 0xC0A00000u, 0x41200000u, 0xC1200000u,
        0x41200001u, 0x411FFFFFu, 0x41700000u, 0x42C80000u, 0xC2C80000u, 0x71800000u,
        0x72000000u, 0x72000001u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u,
        0x7F800001u, 0x7FC00000u, 0xFFC00000u, 0x7FFFFFFFu, 0xFFFFFFFFu,
    };
    int mismatches = 0;
    for (std::uint32_t stamp : pool) {
        for (std::uint32_t clock : pool) {
            for (std::uint32_t window : pool) {
                const std::uint32_t want = model(stamp, clock, window, kValue);
                const std::uint32_t got = run(stamp, clock, window, kValue);
                if (got != want) {
                    if (mismatches < 10) {
                        std::printf("mismatch stamp=%08X clock=%08X window=%08X: got %08X want %08X\n",
                                    stamp, clock, window, got, want);
                    }
                    ++mismatches;
                }
            }
        }
    }
    CHECK_EQ(mismatches, 0);
    // The pool exercises both outcomes: count them so a degenerate pool cannot pass silently.
    int fresh = 0, stale = 0;
    for (std::uint32_t stamp : pool)
        for (std::uint32_t clock : pool)
            for (std::uint32_t window : pool) (model(stamp, clock, window, kValue) ? fresh : stale)++;
    CHECK(fresh > 1000);
    CHECK(stale > 1000);
}

}  // namespace

int main() {
    sentinel_stamp_returns_zero();
    near_sentinel_patterns_are_ordinary_stamps();
    ordinary_fresh_and_stale();
    negative_stamps_and_expiries();
    negative_window();
    zero_and_denormal_windows();
    add_truncates();
    large_values_and_saturation();
    host_nan_patterns_are_finite();
    cancellation_and_underflow();
    return_value_width();
    object_placement();
    no_writes();
    pool_sweep_against_model();
    return check::done("test_stamped_value_if_fresh");
}
