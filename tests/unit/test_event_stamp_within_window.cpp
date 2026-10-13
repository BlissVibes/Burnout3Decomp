// Standalone test for b3::score::event_stamp_within_window (retail 0x00156170).
//
// Expected values are derived from the specification, never from running the game. The function
// loads the state pointer at owner+200, the stamp at state+5512, returns 0 when stamp < +0.0 under
// the EE ordering, else forms deadline = stamp + window with the EE add and returns 1 when the
// clock at state+4300 is <= deadline under the EE ordering, else 0.
//
// Derivation rules used below (all from the spec's description of the EE float model):
//  * Ordering is sign-magnitude on flushed patterns: a zero exponent field flushes to a signed zero
//    first, so -0, +0 and every denormal are equal to 0 and none is "negative"; a sign bit with any
//    non-zero exponent is negative; exponent 255 is an ordinary huge magnitude (0x7FFFFFFF is the
//    largest value, 0xFFFFFFFF the smallest).
//  * The add flushes both operands, then adds exactly and truncates toward zero. Sums of small
//    integers, halves and quarters are exactly representable, so host float literals give the
//    expected bits for them. An exponent overflow saturates to sign|0x7F7FFFFF; exact cancellation
//    gives +0; an operand more than 25 exponent steps below the other contributes nothing.
//  * Truncation made visible: 1.0 + 1.5*2^-24 (0x33C00000) truncates to exactly 1.0 (IEEE nearest
//    would give 1 + 2^-23); 1.0 + 1.5*2^-23 (0x34400000) truncates to 1 + 2^-23 = 0x3F800001
//    (IEEE ties-to-even would give 0x3F800002).
//  * Guest memory: the owner and state are placed at arbitrary RAM addresses; nothing is written
//    by the function, so every word we wrote reads back unchanged.

#include "check.hpp"
#include "ee/guest.hpp"
#include "hooks/score/event_stamp_within_window.hpp"

#include <cstdint>
#include <cstring>

using b3::score::event_stamp_within_window;
using b3::score::event_stamp_within_window_at;
using b3::score::EventStampOwner;
using b3::score::kEventClockOffset;
using b3::score::kEventStampOffset;
using b3::score::kEventStateBlockOffset;
using ee::f32;
using ee::u32;

namespace {

constexpr u32 kOwner = 0x00410000u;  // where the owner object sits in the test RAM
constexpr u32 kState = 0x00620000u;  // where the state block sits

void clear() {
    std::memset(ps2::g_ram, 0, ps2::kRamSize);
    std::memset(ps2::g_spr, 0, ps2::kScratchpadSize);
}

// Lays out owner -> state, writes the stamp and clock, calls the function with `window`.
std::int32_t run(u32 stamp_bits, u32 window_bits, u32 clock_bits, u32 owner = kOwner, u32 state = kState) {
    ee::st<u32>(owner + kEventStateBlockOffset, state);
    ee::st<u32>(state + kEventStampOffset, stamp_bits);
    ee::st<u32>(state + kEventClockOffset, clock_bits);
    return event_stamp_within_window(ee::guest_ptr<EventStampOwner>{owner}, f32::from_bits(window_bits));
}

std::int32_t run(float stamp, float window, float clock) {
    return run(f32::lit(stamp).bits, f32::lit(window).bits, f32::lit(clock).bits);
}

void plain_values() {
    clear();
    // stamp 10, window 2 -> deadline 12 exactly.
    CHECK_EQ(run(10.0f, 2.0f, 11.0f), 1);
    CHECK_EQ(run(10.0f, 2.0f, 12.0f), 1);   // inclusive at the deadline
    CHECK_EQ(run(10.0f, 2.0f, 12.5f), 0);
    CHECK_EQ(run(10.0f, 2.0f, 0.0f), 1);    // clock behind the stamp still counts
    CHECK_EQ(run(10.0f, 2.0f, -100.0f), 1);
    // one ulp above the deadline is already outside
    CHECK_EQ(run(f32::lit(10.0f).bits, f32::lit(2.0f).bits, f32::lit(12.0f).bits + 1u), 0);
    CHECK_EQ(run(f32::lit(10.0f).bits, f32::lit(2.0f).bits, f32::lit(12.0f).bits - 1u), 1);
    // the HUD literals 1, 2, 3
    CHECK_EQ(run(100.0f, 1.0f, 101.0f), 1);
    CHECK_EQ(run(100.0f, 1.0f, 101.25f), 0);
    CHECK_EQ(run(100.0f, 3.0f, 103.0f), 1);
    CHECK_EQ(run(100.0f, 3.0f, 103.5f), 0);
    // fractional, exactly representable
    CHECK_EQ(run(0.25f, 0.5f, 0.75f), 1);
    CHECK_EQ(run(0.25f, 0.5f, 0.875f), 0);
}

void zero_stamp_and_zero_window() {
    clear();
    CHECK_EQ(run(0.0f, 0.0f, 0.0f), 1);       // 0 is not negative; 0 <= 0
    CHECK_EQ(run(0.0f, 0.0f, -0.0f), 1);      // -0 == +0 under the EE compare
    CHECK_EQ(run(0.0f, 0.0f, 1.0f), 0);
    CHECK_EQ(run(0.0f, 0.0f, -1.0f), 1);
    CHECK_EQ(run(5.0f, 0.0f, 5.0f), 1);       // zero window: only clock <= stamp passes
    CHECK_EQ(run(5.0f, 0.0f, 5.0f + 0.0f), 1);
    CHECK_EQ(run(f32::lit(5.0f).bits, 0u, f32::lit(5.0f).bits + 1u), 0);
    CHECK_EQ(run(5.0f, -0.0f, 5.0f), 1);      // -0 window acts as zero
    // a denormal window flushes to zero: deadline == stamp
    CHECK_EQ(run(f32::lit(5.0f).bits, 0x00000001u, f32::lit(5.0f).bits), 1);
    CHECK_EQ(run(f32::lit(5.0f).bits, 0x007FFFFFu, f32::lit(5.0f).bits + 1u), 0);
}

void negative_stamp_returns_zero_whatever_the_rest() {
    clear();
    // -1.0f, the likely never-sentinel, with every kind of clock and window
    CHECK_EQ(run(-1.0f, 2.0f, 0.0f), 0);
    CHECK_EQ(run(-1.0f, 1000.0f, -1000.0f), 0);  // clock far below the sum: still 0, the sum is never formed
    CHECK_EQ(run(f32::lit(-1.0f).bits, 0x7FFFFFFFu, 0xFFFFFFFFu), 0);
    // the smallest negative normal
    CHECK_EQ(run(0x80800000u, f32::lit(1.0f).bits, 0u), 0);
    // large negative
    CHECK_EQ(run(-1.0e30f, 1.0e30f, 0.0f), 0);
    // exponent-255 negative patterns: IEEE -inf and NaNs, the integer -1 word; all huge negatives here
    CHECK_EQ(run(0xFF800000u, f32::lit(1.0f).bits, 0u), 0);
    CHECK_EQ(run(0xFFC00000u, f32::lit(1.0f).bits, 0u), 0);
    CHECK_EQ(run(0xFFFFFFFFu, f32::lit(1.0f).bits, 0u), 0);
    CHECK_EQ(run(0xFF800001u, 0x7F800000u, 0xFF800000u), 0);
    // the clock is irrelevant on this path: two different clocks, same answer
    CHECK_EQ(run(-2.0f, 5.0f, -3.0f), 0);
    CHECK_EQ(run(-2.0f, 5.0f, 1.0e9f), 0);
}

void minus_zero_and_negative_denormals_are_not_negative() {
    clear();
    // -0.0: flushed pattern is -0, which is equal to +0, not below it; deadline = -0 + 1 = 1
    CHECK_EQ(run(0x80000000u, f32::lit(1.0f).bits, f32::lit(0.5f).bits), 1);
    CHECK_EQ(run(0x80000000u, f32::lit(1.0f).bits, f32::lit(1.0f).bits), 1);
    CHECK_EQ(run(0x80000000u, f32::lit(1.0f).bits, f32::lit(1.5f).bits), 0);
    // negative denormals flush to -0 before the compare: same as above (a host `< 0.0f` would say 0)
    CHECK_EQ(run(0x80000001u, f32::lit(1.0f).bits, f32::lit(0.5f).bits), 1);
    CHECK_EQ(run(0x80000001u, f32::lit(1.0f).bits, f32::lit(1.5f).bits), 0);
    CHECK_EQ(run(0x807FFFFFu, f32::lit(1.0f).bits, f32::lit(0.5f).bits), 1);
    CHECK_EQ(run(0x807FFFFFu, f32::lit(1.0f).bits, f32::lit(1.5f).bits), 0);
    CHECK_EQ(run(0x80400000u, f32::lit(1.0f).bits, f32::lit(1.0f).bits), 1);
    // and the flushed stamp contributes nothing to the sum: -denormal + 1.0 is exactly 1.0
    CHECK_EQ(run(0x807FFFFFu, f32::lit(1.0f).bits, f32::lit(1.0f).bits + 1u), 0);
    // positive denormals likewise act as +0
    CHECK_EQ(run(0x00000001u, f32::lit(1.0f).bits, f32::lit(1.0f).bits), 1);
    CHECK_EQ(run(0x007FFFFFu, f32::lit(1.0f).bits, f32::lit(1.0f).bits + 1u), 0);
    CHECK_EQ(run(0x00000001u, 0x00000001u, 0x80000000u), 1);  // 0 + 0 = +0; clock -0 <= +0
    CHECK_EQ(run(0x00000001u, 0x00000001u, 0x00800000u), 0);  // smallest normal is above +0
}

void negative_window() {
    clear();
    // stamp 5, window -2 -> deadline 3
    CHECK_EQ(run(5.0f, -2.0f, 3.0f), 1);
    CHECK_EQ(run(5.0f, -2.0f, 3.25f), 0);
    CHECK_EQ(run(5.0f, -2.0f, -7.0f), 1);
    // exact cancellation gives +0: clock 0, -0 and denormals pass, the smallest normal does not
    CHECK_EQ(run(3.0f, -3.0f, 0.0f), 1);
    CHECK_EQ(run(3.0f, -3.0f, -0.0f), 1);
    CHECK_EQ(run(f32::lit(3.0f).bits, f32::lit(-3.0f).bits, 0x00000001u), 1);
    CHECK_EQ(run(f32::lit(3.0f).bits, f32::lit(-3.0f).bits, 0x80000001u), 1);
    CHECK_EQ(run(f32::lit(3.0f).bits, f32::lit(-3.0f).bits, 0x00800000u), 0);
    CHECK_EQ(run(f32::lit(3.0f).bits, f32::lit(-3.0f).bits, 0x80800000u), 1);
    // window more negative than the stamp: deadline negative
    CHECK_EQ(run(1.0f, -4.0f, -3.0f), 1);
    CHECK_EQ(run(1.0f, -4.0f, -2.5f), 0);
    CHECK_EQ(run(1.0f, -4.0f, 0.0f), 0);
}

void truncating_add() {
    clear();
    const u32 one = f32::lit(1.0f).bits;              // 0x3F800000
    const u32 one_plus_ulp = one + 1u;                // 1 + 2^-23
    const u32 one_plus_2ulp = one + 2u;               // 1 + 2^-22
    // 1.0 + 1.5*2^-24: the small operand lands below the result's last bit, truncated away -> 1.0
    CHECK_EQ(run(one, 0x33C00000u, one), 1);
    CHECK_EQ(run(one, 0x33C00000u, one_plus_ulp), 0);   // host IEEE rounding would make this 1
    // 1.0 + 1.5*2^-23: truncates to 1 + 2^-23 (IEEE ties-to-even would give 1 + 2^-22)
    CHECK_EQ(run(one, 0x34400000u, one_plus_ulp), 1);
    CHECK_EQ(run(one, 0x34400000u, one_plus_2ulp), 0);  // host IEEE rounding would make this 1
    // the same with operands swapped in magnitude order: stamp tiny, window 1.0
    CHECK_EQ(run(0x34400000u, one, one_plus_ulp), 1);
    CHECK_EQ(run(0x34400000u, one, one_plus_2ulp), 0);
    // more than 25 exponent steps apart: the small operand is dropped entirely
    CHECK_EQ(run(one, 0x30000000u, one), 1);            // 1.0 + 2^-31 = 1.0
    CHECK_EQ(run(one, 0x30000000u, one_plus_ulp), 0);
    // two numbers whose exact sum is representable: 1.5 + 2.25 = 3.75
    CHECK_EQ(run(1.5f, 2.25f, 3.75f), 1);
    CHECK_EQ(run(f32::lit(1.5f).bits, f32::lit(2.25f).bits, f32::lit(3.75f).bits + 1u), 0);
    // a carry out of the mantissa add: 1.5 + 1.5 = 3.0 (exponent step)
    CHECK_EQ(run(1.5f, 1.5f, 3.0f), 1);
    CHECK_EQ(run(f32::lit(1.5f).bits, f32::lit(1.5f).bits, f32::lit(3.0f).bits + 1u), 0);
}

void exponent_255_patterns_are_huge_finite_values() {
    clear();
    const u32 huge = 0x7F800000u;         // IEEE +inf: on the EE, 2^128
    const u32 max_pattern = 0x7FFFFFFFu;  // the largest EE value
    const u32 flt_max = 0x7F7FFFFFu;      // the largest pattern below the exponent-255 range
    // stamp 2^128, window 2.0: 2.0 is 127 exponent steps below, contributes nothing -> deadline 2^128
    CHECK_EQ(run(huge, f32::lit(2.0f).bits, huge), 1);
    CHECK_EQ(run(huge, f32::lit(2.0f).bits, flt_max), 1);
    CHECK_EQ(run(huge, f32::lit(2.0f).bits, huge + 1u), 0);
    CHECK_EQ(run(huge, f32::lit(2.0f).bits, max_pattern), 0);
    // stamp + window overflow the exponent: the add saturates to the maximal pattern 0x7FFFFFFF
    // (ps2::fp::pack), which is at or above every other pattern, so no clock is outside the window
    CHECK_EQ(run(max_pattern, max_pattern, flt_max), 1);
    CHECK_EQ(run(max_pattern, max_pattern, huge), 1);
    CHECK_EQ(run(max_pattern, max_pattern, max_pattern), 1);
    CHECK_EQ(run(huge, huge, flt_max), 1);           // 2^128 + 2^128 saturates too
    CHECK_EQ(run(huge, huge, huge), 1);
    CHECK_EQ(run(huge, huge, max_pattern), 1);
    // FLT_MAX + FLT_MAX carries into exponent 255 with an all-ones mantissa: 0x7FFFFFFF again
    CHECK_EQ(run(flt_max, flt_max, flt_max), 1);
    CHECK_EQ(run(flt_max, flt_max, flt_max + 1u), 1);
    CHECK_EQ(run(flt_max, flt_max, max_pattern), 1);
    // a huge positive clock is above any ordinary deadline
    CHECK_EQ(run(f32::lit(1.0f).bits, f32::lit(1.0f).bits, huge), 0);
    CHECK_EQ(run(f32::lit(1.0f).bits, f32::lit(1.0f).bits, 0x7FC00000u), 0);  // IEEE NaN pattern
    CHECK_EQ(run(f32::lit(1.0f).bits, f32::lit(1.0f).bits, max_pattern), 0);
    // a huge negative clock is below any deadline
    CHECK_EQ(run(f32::lit(1.0f).bits, f32::lit(1.0f).bits, 0xFF800000u), 1);
    CHECK_EQ(run(f32::lit(1.0f).bits, f32::lit(1.0f).bits, 0xFFFFFFFFu), 1);
    CHECK_EQ(run(0u, 0u, 0xFFC00000u), 1);
    // a huge negative window pulls the deadline down to a huge negative value
    CHECK_EQ(run(f32::lit(1.0f).bits, 0xFF800000u, 0xFF800000u), 1);  // 1 - 2^128 truncates to -2^128
    CHECK_EQ(run(f32::lit(1.0f).bits, 0xFF800000u, 0xFF7FFFFFu), 0);
    // a maximal window makes the deadline the maximal pattern (1 is too small to change it): every clock passes
    CHECK_EQ(run(f32::lit(1.0f).bits, 0x7FFFFFFFu, 0x7F7FFFFFu), 1);
    CHECK_EQ(run(f32::lit(1.0f).bits, 0x7FFFFFFFu, 0x7F800000u), 1);
    CHECK_EQ(run(f32::lit(1.0f).bits, 0x7FFFFFFFu, 0x7FFFFFFFu), 1);
}

void stamp_offset_parameter() {
    clear();
    // The core reads the stamp at the given offset; the siblings' offsets are checked by writing a
    // passing stamp at one offset and a "never" stamp at the others.
    const ee::guest_ptr<EventStampOwner> owner{kOwner};
    ee::st<u32>(kOwner + kEventStateBlockOffset, kState);
    ee::st<f32>(kState + kEventClockOffset, f32::lit(10.0f));
    for (u32 offset : {5120u, 5500u, 5512u, 5760u}) ee::st<f32>(kState + offset, f32::lit(-1.0f));
    ee::st<f32>(kState + 5500u, f32::lit(9.0f));
    CHECK_EQ(event_stamp_within_window_at(owner, 5500u, f32::lit(1.0f)), 1);
    CHECK_EQ(event_stamp_within_window_at(owner, 5120u, f32::lit(1.0f)), 0);
    CHECK_EQ(event_stamp_within_window_at(owner, 5512u, f32::lit(1.0f)), 0);
    CHECK_EQ(event_stamp_within_window_at(owner, 5760u, f32::lit(1.0f)), 0);
    CHECK_EQ(event_stamp_within_window(owner, f32::lit(1.0f)), 0);
    ee::st<f32>(kState + kEventStampOffset, f32::lit(9.5f));
    CHECK_EQ(event_stamp_within_window(owner, f32::lit(0.5f)), 1);
    CHECK_EQ(event_stamp_within_window(owner, f32::lit(0.25f)), 0);
    CHECK_EQ(event_stamp_within_window_at(owner, kEventStampOffset, f32::lit(0.5f)), 1);
}

void pointer_indirection_and_placement() {
    clear();
    // The state pointer is read from the owner, not assumed: two owners pointing at two states.
    const u32 owner_a = 0x00010000u, state_a = 0x00300000u;
    const u32 owner_b = 0x00010200u, state_b = 0x01F00000u;  // near the top of 32 MB
    CHECK_EQ(run(f32::lit(1.0f).bits, f32::lit(1.0f).bits, f32::lit(2.0f).bits, owner_a, state_a), 1);
    CHECK_EQ(run(f32::lit(1.0f).bits, f32::lit(1.0f).bits, f32::lit(3.0f).bits, owner_b, state_b), 0);
    // swap the pointers: each owner now sees the other's state
    ee::st<u32>(owner_a + kEventStateBlockOffset, state_b);
    ee::st<u32>(owner_b + kEventStateBlockOffset, state_a);
    CHECK_EQ(event_stamp_within_window(ee::guest_ptr<EventStampOwner>{owner_a}, f32::lit(1.0f)), 0);
    CHECK_EQ(event_stamp_within_window(ee::guest_ptr<EventStampOwner>{owner_b}, f32::lit(1.0f)), 1);
    // a zero state pointer reads low RAM: addresses 5512 and 4300 (precondition, not guarded)
    clear();
    ee::st<u32>(kOwner + kEventStateBlockOffset, 0u);
    ee::st<f32>(kEventStampOffset, f32::lit(4.0f));
    ee::st<f32>(kEventClockOffset, f32::lit(4.5f));
    CHECK_EQ(event_stamp_within_window(ee::guest_ptr<EventStampOwner>{kOwner}, f32::lit(0.5f)), 1);
    CHECK_EQ(event_stamp_within_window(ee::guest_ptr<EventStampOwner>{kOwner}, f32::lit(0.25f)), 0);
    // owner at address 0 is also just an address
    clear();
    ee::st<u32>(0u + kEventStateBlockOffset, kState);
    ee::st<f32>(kState + kEventStampOffset, f32::lit(1.0f));
    ee::st<f32>(kState + kEventClockOffset, f32::lit(1.0f));
    CHECK_EQ(event_stamp_within_window(ee::guest_ptr<EventStampOwner>{0u}, f32::lit(0.0f)), 1);
}

void nothing_is_written() {
    clear();
    // Fill the owner and state neighbourhoods with a pattern, run both paths, and check that every
    // byte we can see is unchanged apart from the inputs we wrote.
    for (u32 a = kOwner; a < kOwner + 0x200u; ++a) ps2::g_ram[a] = static_cast<std::uint8_t>(a * 7u);
    for (u32 a = kState; a < kState + 0x1800u; ++a) ps2::g_ram[a] = static_cast<std::uint8_t>(a * 13u);
    ee::st<u32>(kOwner + kEventStateBlockOffset, kState);
    ee::st<f32>(kState + kEventStampOffset, f32::lit(2.0f));
    ee::st<f32>(kState + kEventClockOffset, f32::lit(2.5f));
    std::uint8_t owner_copy[0x200], state_copy[0x1800];
    std::memcpy(owner_copy, ps2::g_ram + kOwner, sizeof owner_copy);
    std::memcpy(state_copy, ps2::g_ram + kState, sizeof state_copy);
    CHECK_EQ(event_stamp_within_window(ee::guest_ptr<EventStampOwner>{kOwner}, f32::lit(1.0f)), 1);
    CHECK_EQ(event_stamp_within_window(ee::guest_ptr<EventStampOwner>{kOwner}, f32::lit(0.25f)), 0);
    ee::st<f32>(kState + kEventStampOffset, f32::lit(-1.0f));
    std::memcpy(state_copy + kEventStampOffset, ps2::g_ram + kState + kEventStampOffset, 4);
    CHECK_EQ(event_stamp_within_window(ee::guest_ptr<EventStampOwner>{kOwner}, f32::lit(1.0f)), 0);
    CHECK(std::memcmp(owner_copy, ps2::g_ram + kOwner, sizeof owner_copy) == 0);
    CHECK(std::memcmp(state_copy, ps2::g_ram + kState, sizeof state_copy) == 0);
}

void result_is_exactly_zero_or_one() {
    clear();
    // The retail register is 0 or 1 with every upper bit clear; a 32-bit 0/1 widens to the same.
    const std::int32_t yes = run(1.0f, 1.0f, 1.0f);
    const std::int32_t no = run(1.0f, 1.0f, 3.0f);
    CHECK_EQ(yes, 1);
    CHECK_EQ(no, 0);
    CHECK_EQ(static_cast<std::uint64_t>(static_cast<std::int64_t>(yes)), 1ull);
    CHECK_EQ(static_cast<std::uint64_t>(static_cast<std::uint32_t>(yes)), 1ull);
    CHECK_EQ(static_cast<std::uint64_t>(static_cast<std::int64_t>(no)), 0ull);
}

// Independent derivation over integer-valued inputs: integer sums up to a few thousand are exact
// in single precision, so the EE add equals the integer add and both compares are plain numeric.
void integer_grid_against_arithmetic() {
    clear();
    const int values[] = {-2000, -17, -3, -2, -1, 0, 1, 2, 3, 17, 2000};
    for (int stamp : values) {
        for (int window : values) {
            for (int clock : values) {
                const std::int32_t expected = stamp < 0 ? 0 : (clock <= stamp + window ? 1 : 0);
                CHECK_EQ(run(static_cast<float>(stamp), static_cast<float>(window), static_cast<float>(clock)), expected);
            }
        }
    }
}

}  // namespace

int main() {
    plain_values();
    zero_stamp_and_zero_window();
    negative_stamp_returns_zero_whatever_the_rest();
    minus_zero_and_negative_denormals_are_not_negative();
    negative_window();
    truncating_add();
    exponent_255_patterns_are_huge_finite_values();
    stamp_offset_parameter();
    pointer_indirection_and_placement();
    nothing_is_written();
    result_is_exactly_zero_or_one();
    integer_grid_against_arithmetic();
    return check::done("test_event_stamp_within_window");
}
