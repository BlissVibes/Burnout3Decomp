// Unit test for lowest_set_bit_index (retail 0x00299550). Expected values are derived from the
// specification by reasoning, never from running the game: the function tests bit positions
// 0..31 in order and returns the first one set, or 32 when none is. So for a non-zero mask the
// answer is the number of trailing zero bits, and for a mask with exactly one bit set it is that
// bit's position. The reference below is an independent formulation (isolate the lowest bit with
// mask & -mask, then locate it by doubling) so a mistake in the implementation's loop is not
// mirrored in the oracle.

#include "check.hpp"
#include "hooks/audio/lowest_set_bit_index.hpp"

#include <cstdint>

using b3::audio::lowest_set_bit_index;

namespace {

// Independent reference. Zero has no lowest bit and yields 32 by the specification.
std::uint8_t reference(std::uint32_t mask) {
    if (mask == 0) return 32;
    const std::uint32_t lowest = mask & (0u - mask);
    std::uint8_t pos = 0;
    for (std::uint32_t bit = 1; bit != lowest; bit <<= 1) ++pos;
    return pos;
}

void zero_returns_32() {
    // Edge case: all 32 tests fail; the result is one past the highest valid index, not -1 or 0.
    CHECK_EQ(lowest_set_bit_index(0u), 32u);
}

void single_bits_return_their_position() {
    // A one-bit mask at position i is found exactly at test i. Position 31 is the sign bit and the
    // last position tested: 0x80000000 returns 31, not 32.
    for (unsigned i = 0; i < 32; ++i) {
        CHECK_EQ(lowest_set_bit_index(std::uint32_t{1} << i), i);
    }
    CHECK_EQ(lowest_set_bit_index(0x80000000u), 31u);
}

void lowest_bit_wins_when_several_are_set() {
    CHECK_EQ(lowest_set_bit_index(0xFFFFFFFFu), 0u);   // -1 in a signed view: bit 0 is set
    CHECK_EQ(lowest_set_bit_index(0xFFFFFFF0u), 4u);   // the sign-extended byte -16 from the callers
    CHECK_EQ(lowest_set_bit_index(0xFFFFFF80u), 7u);   // sign-extended byte -128
    CHECK_EQ(lowest_set_bit_index(0xFFFFFF00u), 8u);   // -256
    CHECK_EQ(lowest_set_bit_index(0x80000001u), 0u);
    CHECK_EQ(lowest_set_bit_index(0xC0000000u), 30u);
    CHECK_EQ(lowest_set_bit_index(0xFFFF0000u), 16u);
    CHECK_EQ(lowest_set_bit_index(0x00010000u), 16u);
    CHECK_EQ(lowest_set_bit_index(0x0000FFFFu), 0u);
    CHECK_EQ(lowest_set_bit_index(6u), 1u);
    CHECK_EQ(lowest_set_bit_index(12u), 2u);
}

void the_callers_constants() {
    // The reader saw the constants 64, 40 and 48 at most call sites. 64 = bit 6; 40 = 0b101000, lowest
    // bit 3; 48 = 0b110000, lowest bit 4.
    CHECK_EQ(lowest_set_bit_index(64u), 6u);
    CHECK_EQ(lowest_set_bit_index(40u), 3u);
    CHECK_EQ(lowest_set_bit_index(48u), 4u);
    // Callers mask the result with 0xFF and rebuild a 16-bit single-bit mask; for these inputs that
    // round trip reproduces the lowest bit.
    CHECK_EQ((1u << (lowest_set_bit_index(64u) & 0xFFu)) & 0xFFFFu, 64u);
    CHECK_EQ((1u << (lowest_set_bit_index(40u) & 0xFFu)) & 0xFFFFu, 8u);
    CHECK_EQ((1u << (lowest_set_bit_index(48u) & 0xFFu)) & 0xFFFFu, 16u);
}

void result_range_and_width() {
    // The result is 0..32, fits in a byte, and masking with 0xFF (as the callers do) changes nothing.
    for (unsigned i = 0; i < 32; ++i) {
        const std::uint8_t r = lowest_set_bit_index(std::uint32_t{1} << i);
        CHECK(r <= 32);
        CHECK_EQ(r & 0xFFu, r);
    }
    CHECK_EQ(lowest_set_bit_index(0u) & 0xFFu, 32u);
}

void sign_versus_zero_extension_of_the_argument_is_irrelevant() {
    // For any value that is consistently sign- or zero-extended, the upper 32 bits are set only when
    // bit 31 is set, which already answers 31 at the last test, so truncating to 32 bits loses nothing.
    // A signed int argument converts to the same 32-bit pattern.
    const std::int32_t negatives[] = {-1, -2, -16, -128, -256, -65536, INT32_MIN};
    const std::uint8_t expected[] = {0, 1, 4, 7, 8, 16, 31};
    for (unsigned k = 0; k < 7; ++k) {
        CHECK_EQ(lowest_set_bit_index(static_cast<std::uint32_t>(negatives[k])), expected[k]);
    }
}

void agrees_with_the_independent_reference() {
    // Sweep a spread of patterns: every single bit, every pair of adjacent bits, all-ones prefixes
    // and suffixes, and a simple pseudo-random sequence.
    for (unsigned i = 0; i < 32; ++i) {
        const std::uint32_t one = std::uint32_t{1} << i;
        CHECK_EQ(lowest_set_bit_index(one), reference(one));
        if (i < 31) CHECK_EQ(lowest_set_bit_index(one | (one << 1)), reference(one | (one << 1)));
        CHECK_EQ(lowest_set_bit_index(0xFFFFFFFFu << i), reference(0xFFFFFFFFu << i));
        CHECK_EQ(lowest_set_bit_index(0xFFFFFFFFu >> i), reference(0xFFFFFFFFu >> i));
    }
    std::uint32_t x = 0x12345678u;
    for (int n = 0; n < 2000; ++n) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;     // xorshift32, never produces zero from a non-zero seed
        CHECK_EQ(lowest_set_bit_index(x), reference(x));
        CHECK_EQ(lowest_set_bit_index(x & 0xFFFFu), reference(x & 0xFFFFu));
        CHECK_EQ(lowest_set_bit_index(x & 0xFFFF0000u), reference(x & 0xFFFF0000u));
    }
}

}

int main() {
    zero_returns_32();
    single_bits_return_their_position();
    lowest_bit_wins_when_several_are_set();
    the_callers_constants();
    result_range_and_width();
    sign_versus_zero_extension_of_the_argument_is_irrelevant();
    agrees_with_the_independent_reference();
    return check::done("test_lowest_set_bit_index");
}
