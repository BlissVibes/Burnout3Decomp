// ring_index_advance (retail 0x00145E70): stepping a 16-bit ring-buffer position by a signed delta,
// wrapping or clamping as the buffer header's flag says. Standalone: builds with the implementation
// alone, no guest memory needed. Every expected value below is worked out by hand from the
// specification (the comments show the arithmetic); none comes from running the game.

#include "check.hpp"
#include "hooks/ring_buffers/ring_index_advance.hpp"

#include <cstdint>

using namespace b3::ring_buffers;

namespace {

struct Ring {
    RingHeader header{};
    RingDesc desc{};
    Ring(std::uint16_t capacity, std::uint8_t wraps) {
        header.capacity = capacity;
        header.wraps = wraps;
        desc.unread_first_word = 0xDEADBEEFu;
        desc.header = &header;
    }
};

std::uint16_t clamp_ring(std::uint16_t capacity, std::uint16_t index, std::int16_t delta) {
    Ring ring(capacity, 0);
    return ring_index_advance(&ring.desc, index, delta);
}

std::uint16_t wrap_ring(std::uint16_t capacity, std::uint16_t index, std::int16_t delta) {
    Ring ring(capacity, 1);
    return ring_index_advance(&ring.desc, index, delta);
}

// The register contract the hooks table will implement around the pure function: the step is the
// low 16 bits of its register, sign-extended; the index is the low 16 bits of its register; and
// when the step is zero the whole 64-bit index register comes back untouched, otherwise the 16-bit
// result is zero-extended. Written here so the tests can state the register-level edge cases.
std::uint64_t call_like_shim(const RingDesc *desc, std::uint64_t a1, std::uint64_t a2) {
    const std::int16_t delta = static_cast<std::int16_t>(a2 & 0xFFFFu);
    if (delta == 0) return a1;
    return ring_index_advance(desc, static_cast<std::uint16_t>(a1 & 0xFFFFu), delta);
}

void zero_delta_returns_the_index_without_reading_memory() {
    // The descriptor is null: a zero step must never dereference it.
    CHECK_EQ(ring_index_advance(nullptr, 0, 0), 0u);
    CHECK_EQ(ring_index_advance(nullptr, 5, 0), 5u);
    CHECK_EQ(ring_index_advance(nullptr, 0xFFFF, 0), 0xFFFFu);
    // Even a ring that would clamp or wrap the index leaves it alone when the step is zero.
    CHECK_EQ(clamp_ring(10, 50, 0), 50u);
    CHECK_EQ(wrap_ring(10, 50, 0), 50u);
}

void clamp_path_inside_range() {
    CHECK_EQ(clamp_ring(10, 3, 2), 5u);      // 3 + 2 = 5, within [0, 9]
    CHECK_EQ(clamp_ring(10, 3, -2), 1u);     // 3 - 2 = 1
    CHECK_EQ(clamp_ring(10, 0, 9), 9u);      // lands exactly on the last slot
    CHECK_EQ(clamp_ring(10, 3, -3), 0u);     // lands exactly on the first slot
}

void clamp_path_at_the_ends() {
    CHECK_EQ(clamp_ring(10, 3, -5), 0u);     // 3 - 5 = -2 -> max(-2, 0) = 0
    CHECK_EQ(clamp_ring(10, 9, 1), 9u);      // 9 + 1 = 10 -> min(10, 9) = 9
    CHECK_EQ(clamp_ring(10, 8, 1), 9u);      // 8 + 1 = 9, the boundary itself is not clamped
    CHECK_EQ(clamp_ring(10, 0, -1), 0u);     // -1 -> 0
    CHECK_EQ(clamp_ring(1, 0, 7), 0u);       // one slot: 7 -> min(7, 0) = 0
    CHECK_EQ(clamp_ring(1, 0, -7), 0u);      // one slot: -7 -> max(-7, 0) = 0
}

void clamp_path_pulls_out_of_range_indices_down() {
    // An index already past the end is not special: it is clamped like any other position.
    CHECK_EQ(clamp_ring(10, 50, 1), 9u);     // 51 -> 9
    CHECK_EQ(clamp_ring(10, 50, -1), 9u);    // 49 -> 9
    CHECK_EQ(clamp_ring(10, 50, -45), 5u);   // 5, back inside
}

void clamp_path_with_zero_capacity_gives_0xFFFF() {
    // capacity - 1 = -1 as a signed 32-bit value, so min(max(p, 0), -1) = -1, masked to 0xFFFF.
    CHECK_EQ(clamp_ring(0, 0, 1), 0xFFFFu);
    CHECK_EQ(clamp_ring(0, 5, -3), 0xFFFFu);  // 2 -> min(2, -1) = -1
    CHECK_EQ(clamp_ring(0, 5, -9), 0xFFFFu);  // -4 -> 0 -> min(0, -1) = -1
}

void clamp_path_uses_signed_32_bit_bounds() {
    // Largest position: 65535 + 32767 = 98302, clamped against 65534 (capacity 65535 - 1).
    CHECK_EQ(clamp_ring(65535, 65535, 32767), 0xFFFEu);
    // Most negative position: 0 - 32768 = -32768 -> 0, not a 16-bit wraparound to 0x8000.
    CHECK_EQ(clamp_ring(65535, 0, -32768), 0u);
    CHECK_EQ(clamp_ring(10, 0, -32768), 0u);
    // A capacity above 0x7FFF must read as unsigned. With 0x8001 the last slot is 0x8000, so 0 + 1
    // stays 1; had the capacity been sign-extended the bound would be -32768 and the result 0x8000.
    CHECK_EQ(clamp_ring(0x8001, 0, 1), 1u);
    CHECK_EQ(clamp_ring(0x8001, 0x7FFF, 1), 0x8000u);   // 0x8000 <= 0x8000, not clamped
    CHECK_EQ(clamp_ring(0x8001, 0x8000, 1), 0x8000u);   // 0x8001 -> 0x8000
}

void wrap_path_inside_range() {
    CHECK_EQ(wrap_ring(10, 3, 2), 5u);       // (3 + 2 + 10) % 10 = 15 % 10 = 5
    CHECK_EQ(wrap_ring(10, 3, -2), 1u);      // (1 + 10) % 10 = 1
    CHECK_EQ(wrap_ring(10, 0, 9), 9u);       // 19 % 10 = 9
}

void wrap_path_crosses_the_ends() {
    CHECK_EQ(wrap_ring(10, 9, 1), 0u);       // (10 + 10) % 10 = 0
    CHECK_EQ(wrap_ring(10, 0, -1), 9u);      // (-1 + 10) % 10 = 9
    CHECK_EQ(wrap_ring(10, 0, -10), 0u);     // (-10 + 10) % 10 = 0
    CHECK_EQ(wrap_ring(10, 9, 25), 4u);      // (34 + 10) % 10 = 44 % 10 = 4: positive overshoot of any size
    CHECK_EQ(wrap_ring(10, 50, 1), 1u);      // (51 + 10) % 10 = 61 % 10 = 1: index already past the end
    CHECK_EQ(wrap_ring(1, 0, 5), 0u);        // 6 % 1 = 0
    CHECK_EQ(wrap_ring(1, 0, -5), 0u);       // -4 % 1 = 0 (truncating remainder of an exact division)
}

void wrap_path_only_handles_one_wrap_below_zero() {
    // The remainder keeps the dividend's sign, so an overshoot past -capacity comes back negative
    // and the 16-bit mask turns it into a high value rather than a proper modulo.
    CHECK_EQ(wrap_ring(10, 0, -11), 0xFFFFu);   // (-11 + 10) % 10 = -1 % 10 = -1 -> 0xFFFF
    CHECK_EQ(wrap_ring(10, 2, -25), 0xFFFDu);   // (-23 + 10) % 10 = -13 % 10 = -3 -> 0xFFFD
    CHECK_EQ(wrap_ring(10, 0, -20), 0u);        // (-20 + 10) % 10 = -10 % 10 = 0: exact multiples are fine
}

void wrap_path_uses_unsigned_capacity_and_32_bit_arithmetic() {
    // 65535 + 32767 + 65535 = 163837; 163837 - 2 * 65535 = 32767.
    CHECK_EQ(wrap_ring(65535, 65535, 32767), 0x7FFFu);
    // Capacity 0x8001 = 32769: (0 - 1 + 32769) % 32769 = 32768 -> 0x8000. A sign-extended capacity
    // (-32767) would have given (-1 - 32767) % -32767 = -1 -> 0xFFFF.
    CHECK_EQ(wrap_ring(0x8001, 0, -1), 0x8000u);
    // 0 - 32768 + 65535 = 32767; 32767 % 65535 = 32767.
    CHECK_EQ(wrap_ring(65535, 0, -32768), 0x7FFFu);
}

void wrap_path_with_zero_capacity_follows_the_forks_divide() {
    // Hardware leaves the remainder of a divide by zero unpredictable; the fork's emulation keeps
    // the dividend, so the recompiled build returns (index + delta + 0) & 0xFFFF and we match it.
    // Not an oracle case.
    CHECK_EQ(wrap_ring(0, 7, -3), 4u);
    CHECK_EQ(wrap_ring(0, 0, -1), 0xFFFFu);
    CHECK_EQ(wrap_ring(0, 0xFFFF, 1), 0u);     // 0x10000 & 0xFFFF
}

void the_wrap_flag_is_a_byte_tested_against_zero() {
    // Any non-zero byte selects wrapping, including values with the top bit set (an unsigned load).
    const std::uint8_t flags[] = {1, 0x7F, 0x80, 0xFF};
    for (std::uint8_t flag : flags) {
        Ring ring(10, flag);
        CHECK_EQ(ring_index_advance(&ring.desc, 9, 1), 0u);      // wraps: 20 % 10 = 0
    }
    Ring ring(10, 0);
    CHECK_EQ(ring_index_advance(&ring.desc, 9, 1), 9u);          // clamps
}

void register_level_contract() {
    Ring ring(10, 1);
    // Only the low 16 bits of the step register count: 0x10000 is a zero step and returns the whole
    // index register, high bits included; 0x8000 is -32768.
    CHECK_EQ(call_like_shim(&ring.desc, 0xFFFFFFFFFFFF0005ull, 0x10000u), 0xFFFFFFFFFFFF0005ull);
    CHECK_EQ(call_like_shim(nullptr, 0x1234567800000007ull, 0), 0x1234567800000007ull);
    CHECK_EQ(call_like_shim(&ring.desc, 0, 0x8000u), static_cast<std::uint64_t>(wrap_ring(10, 0, -32768)));
    // In the non-zero case only the low 16 bits of the index register count: 0x1_0005 behaves as 5.
    CHECK_EQ(call_like_shim(&ring.desc, 0x10005u, 2), 7u);                        // (5 + 2 + 10) % 10 = 7
    CHECK_EQ(call_like_shim(&ring.desc, 0xFFFFFFFFFFFF0005ull, 2), 7u);          // same, zero-extended result
    // A negative remainder is masked, never sign-extended, in the result register.
    CHECK_EQ(call_like_shim(&ring.desc, 0, static_cast<std::uint64_t>(static_cast<std::int64_t>(-11))), 0xFFFFull);
}

}

int main() {
    zero_delta_returns_the_index_without_reading_memory();
    clamp_path_inside_range();
    clamp_path_at_the_ends();
    clamp_path_pulls_out_of_range_indices_down();
    clamp_path_with_zero_capacity_gives_0xFFFF();
    clamp_path_uses_signed_32_bit_bounds();
    wrap_path_inside_range();
    wrap_path_crosses_the_ends();
    wrap_path_only_handles_one_wrap_below_zero();
    wrap_path_uses_unsigned_capacity_and_32_bit_arithmetic();
    wrap_path_with_zero_capacity_follows_the_forks_divide();
    the_wrap_flag_is_a_byte_tested_against_zero();
    register_level_contract();
    return check::done("test_ring_index_advance");
}
