// ring_index_is_at_or_ahead (retail 0x001478C0) against its specification. Every expected value below
// is worked out by hand from the rule in the header comment, never from running the game:
//   d = (a & 0xFFFF) - (b & 0xFFFF), signed;
//   non-wrapping: 1 iff d >= 0;
//   wrapping, half = count / 2 truncating: 1 iff (d >= 0 and d < half) or d < -half.

#include "check.hpp"
#include "hooks/ring_buffers/ring_index_is_at_or_ahead.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

using b3::ring_buffers::RingHeaderView;
using b3::ring_buffers::ring_index_is_at_or_ahead;

namespace {

RingHeaderView header(std::uint16_t slot_count, std::uint8_t wraps) {
    RingHeaderView h;
    std::memset(&h, 0xA5, sizeof h);   // the unread bytes must not matter
    h.slot_count = slot_count;
    h.wraps = wraps;
    return h;
}

// Reads the header the way the game lays it out in memory: count at byte 12, flag at byte 14.
void header_view_matches_the_guest_layout() {
    unsigned char raw[16] = {};
    raw[12] = 0x10; raw[13] = 0x00;   // count 16, little-endian
    raw[14] = 0x01;                    // wraps
    RingHeaderView h;
    std::memcpy(&h, raw, sizeof h);
    CHECK_EQ(h.slot_count, 16u);
    CHECK_EQ(h.wraps, 1u);
    CHECK_EQ(ring_index_is_at_or_ahead(h, 3u, 0u), 1);    // d = 3 < 8
    CHECK_EQ(ring_index_is_at_or_ahead(h, 11u, 0u), 0);   // d = 11 >= 8
}

void non_wrapping_is_a_plain_signed_comparison() {
    const RingHeaderView h = header(16, 0);
    CHECK_EQ(ring_index_is_at_or_ahead(h, 0u, 0u), 1);          // d = 0
    CHECK_EQ(ring_index_is_at_or_ahead(h, 5u, 3u), 1);          // d = 2
    CHECK_EQ(ring_index_is_at_or_ahead(h, 3u, 5u), 0);          // d = -2
    CHECK_EQ(ring_index_is_at_or_ahead(h, 1u, 0u), 1);          // d = 1
    CHECK_EQ(ring_index_is_at_or_ahead(h, 0u, 1u), 0);          // d = -1
    CHECK_EQ(ring_index_is_at_or_ahead(h, 0xFFFFu, 0u), 1);     // d = 65535, the largest possible
    CHECK_EQ(ring_index_is_at_or_ahead(h, 0u, 0xFFFFu), 0);     // d = -65535, the smallest possible
    CHECK_EQ(ring_index_is_at_or_ahead(h, 0xFFFFu, 0xFFFFu), 1);
    // The slot count plays no part: a 0-slot and a 1-slot ring answer the same.
    CHECK_EQ(ring_index_is_at_or_ahead(header(0, 0), 7u, 7u), 1);
    CHECK_EQ(ring_index_is_at_or_ahead(header(1, 0), 7u, 8u), 0);
    CHECK_EQ(ring_index_is_at_or_ahead(header(0xFFFF, 0), 0x8000u, 0x7FFFu), 1);
}

void only_the_low_sixteen_bits_of_an_index_count() {
    // Garbage or sign extension above bit 15 is discarded before the subtraction.
    const RingHeaderView plain = header(16, 0);
    CHECK_EQ(ring_index_is_at_or_ahead(plain, 0xFFFF0005u, 3u), 1);        // 5 - 3 = 2
    CHECK_EQ(ring_index_is_at_or_ahead(plain, 5u, 0x12340007u), 0);        // 5 - 7 = -2
    CHECK_EQ(ring_index_is_at_or_ahead(plain, 0x00010000u, 1u), 0);        // 0 - 1 = -1, not 65536 - 1
    CHECK_EQ(ring_index_is_at_or_ahead(plain, 0x80000000u, 0u), 1);        // 0 - 0 = 0: no sign extension
    const RingHeaderView wrap = header(16, 1);
    CHECK_EQ(ring_index_is_at_or_ahead(wrap, 0x00010000u, 0u), 1);         // d = 0 < 8
    CHECK_EQ(ring_index_is_at_or_ahead(wrap, 0xFFFF0000u, 0xABCD0009u), 1); // 0 - 9 = -9 < -8
    CHECK_EQ(ring_index_is_at_or_ahead(wrap, 0xFFFF0000u, 0xABCD0008u), 0); // 0 - 8 = -8, not < -8
}

void any_non_zero_wraps_byte_selects_wrapping_mode() {
    // Under the wrapping rule d = 12 with half = 8 answers 0; under the plain rule it answers 1.
    CHECK_EQ(ring_index_is_at_or_ahead(header(16, 0), 12u, 0u), 1);
    CHECK_EQ(ring_index_is_at_or_ahead(header(16, 1), 12u, 0u), 0);
    CHECK_EQ(ring_index_is_at_or_ahead(header(16, 2), 12u, 0u), 0);
    CHECK_EQ(ring_index_is_at_or_ahead(header(16, 0x80), 12u, 0u), 0);
    CHECK_EQ(ring_index_is_at_or_ahead(header(16, 0xFF), 12u, 0u), 0);
}

void wrapping_boundaries_are_strict_on_both_sides() {
    // 16 slots, half = 8. Expected: 1 for 0 <= d <= 7 and for d <= -9, else 0.
    const RingHeaderView h = header(16, 1);
    const std::uint32_t base = 100u;
    CHECK_EQ(ring_index_is_at_or_ahead(h, base, base), 1);          // d = 0
    CHECK_EQ(ring_index_is_at_or_ahead(h, base + 1u, base), 1);     // d = 1
    CHECK_EQ(ring_index_is_at_or_ahead(h, base + 7u, base), 1);     // d = 7 = half - 1
    CHECK_EQ(ring_index_is_at_or_ahead(h, base + 8u, base), 0);     // d = 8 = half
    CHECK_EQ(ring_index_is_at_or_ahead(h, base + 9u, base), 0);     // d = 9
    CHECK_EQ(ring_index_is_at_or_ahead(h, base, base + 1u), 0);     // d = -1
    CHECK_EQ(ring_index_is_at_or_ahead(h, base, base + 7u), 0);     // d = -7
    CHECK_EQ(ring_index_is_at_or_ahead(h, base, base + 8u), 0);     // d = -8 = -half
    CHECK_EQ(ring_index_is_at_or_ahead(h, base, base + 9u), 1);     // d = -9 = -half - 1
    CHECK_EQ(ring_index_is_at_or_ahead(h, base, base + 16u), 1);    // d = -16
    CHECK_EQ(ring_index_is_at_or_ahead(h, 0u, 0xFFFFu), 1);         // d = -65535
    CHECK_EQ(ring_index_is_at_or_ahead(h, 0xFFFFu, 0u), 0);         // d = 65535
    // An odd count truncates: 17 slots also give half = 8, so the same boundaries hold.
    const RingHeaderView odd = header(17, 1);
    CHECK_EQ(ring_index_is_at_or_ahead(odd, base + 7u, base), 1);
    CHECK_EQ(ring_index_is_at_or_ahead(odd, base + 8u, base), 0);
    CHECK_EQ(ring_index_is_at_or_ahead(odd, base, base + 8u), 0);
    CHECK_EQ(ring_index_is_at_or_ahead(odd, base, base + 9u), 1);
}

void degenerate_rings_have_half_zero() {
    // 0 or 1 slots: half = 0, so 0 <= d < 0 never holds and equal indices answer 0; any d < 0 answers 1.
    const std::uint16_t degenerate[] = {0, 1};
    for (std::uint16_t count : degenerate) {
        const RingHeaderView h = header(count, 1);
        CHECK_EQ(ring_index_is_at_or_ahead(h, 0u, 0u), 0);        // d = 0
        CHECK_EQ(ring_index_is_at_or_ahead(h, 1u, 0u), 0);        // d = 1
        CHECK_EQ(ring_index_is_at_or_ahead(h, 0u, 1u), 1);        // d = -1
        CHECK_EQ(ring_index_is_at_or_ahead(h, 0xFFFFu, 0u), 0);   // d = 65535
        CHECK_EQ(ring_index_is_at_or_ahead(h, 0u, 0xFFFFu), 1);   // d = -65535
    }
    // 2 or 3 slots: half = 1, so only d = 0 answers 1 on the non-negative side.
    const RingHeaderView two = header(2, 1);
    CHECK_EQ(ring_index_is_at_or_ahead(two, 0u, 0u), 1);
    CHECK_EQ(ring_index_is_at_or_ahead(two, 1u, 0u), 0);
    CHECK_EQ(ring_index_is_at_or_ahead(two, 0u, 1u), 0);   // d = -1 = -half
    CHECK_EQ(ring_index_is_at_or_ahead(two, 0u, 2u), 1);   // d = -2 < -half
}

void large_slot_counts_stay_unsigned() {
    // 0xFFFF slots: half = 32767 (an unsigned halving; a signed 16-bit reading would give -1).
    const RingHeaderView full = header(0xFFFF, 1);
    CHECK_EQ(ring_index_is_at_or_ahead(full, 32766u, 0u), 1);     // d = 32766 < 32767
    CHECK_EQ(ring_index_is_at_or_ahead(full, 32767u, 0u), 0);     // d = 32767 = half
    CHECK_EQ(ring_index_is_at_or_ahead(full, 0u, 32767u), 0);     // d = -32767 = -half
    CHECK_EQ(ring_index_is_at_or_ahead(full, 0u, 32768u), 1);     // d = -32768 < -half
    CHECK_EQ(ring_index_is_at_or_ahead(full, 0xFFFFu, 0u), 0);    // d = 65535
    CHECK_EQ(ring_index_is_at_or_ahead(full, 0u, 0xFFFFu), 1);    // d = -65535
    // 0x8000 slots: half = 16384; bit 15 of the count must not read as a sign.
    const RingHeaderView big = header(0x8000, 1);
    CHECK_EQ(ring_index_is_at_or_ahead(big, 16383u, 0u), 1);
    CHECK_EQ(ring_index_is_at_or_ahead(big, 16384u, 0u), 0);
    CHECK_EQ(ring_index_is_at_or_ahead(big, 0u, 16384u), 0);
    CHECK_EQ(ring_index_is_at_or_ahead(big, 0u, 16385u), 1);
    // 0xFFFE slots: half = 32767 as well (truncating halving of an even count).
    const RingHeaderView even = header(0xFFFE, 1);
    CHECK_EQ(ring_index_is_at_or_ahead(even, 32766u, 0u), 1);
    CHECK_EQ(ring_index_is_at_or_ahead(even, 32767u, 0u), 0);
}

void the_field_overload_agrees_with_the_view() {
    const RingHeaderView h = header(24, 1);
    for (std::uint32_t a = 0; a < 48; ++a) {
        for (std::uint32_t b = 0; b < 48; ++b) {
            CHECK_EQ(ring_index_is_at_or_ahead(h, a, b), ring_index_is_at_or_ahead(std::uint16_t{24}, std::uint8_t{1}, a, b));
        }
    }
}

// Sweeps the whole difference range for a few counts against the rule written out independently of
// the implementation, as a plain restatement of the specification.
void every_difference_follows_the_rule() {
    const std::uint16_t counts[] = {0, 1, 2, 3, 16, 17, 255, 256, 0x7FFF, 0x8000, 0xFFFE, 0xFFFF};
    for (std::uint16_t count : counts) {
        const std::int32_t half = count / 2;
        for (std::int32_t d = -65535; d <= 65535; ++d) {
            // Pick indices with the required difference inside 0..0xFFFF.
            const std::uint32_t a = d >= 0 ? static_cast<std::uint32_t>(d) : 0u;
            const std::uint32_t b = d >= 0 ? 0u : static_cast<std::uint32_t>(-d);
            const std::int32_t wrapped = (d >= 0 ? d < half : d < -half) ? 1 : 0;
            const std::int32_t plain = d >= 0 ? 1 : 0;
            if (ring_index_is_at_or_ahead(count, std::uint8_t{1}, a, b) != wrapped ||
                ring_index_is_at_or_ahead(count, std::uint8_t{0}, a, b) != plain) {
                std::printf("count %u d %d: expected wrap %d plain %d\n", count, d, wrapped, plain);
                CHECK(false);
                return;
            }
        }
    }
    CHECK(true);
}

}

int main() {
    header_view_matches_the_guest_layout();
    non_wrapping_is_a_plain_signed_comparison();
    only_the_low_sixteen_bits_of_an_index_count();
    any_non_zero_wraps_byte_selects_wrapping_mode();
    wrapping_boundaries_are_strict_on_both_sides();
    degenerate_rings_have_half_zero();
    large_slot_counts_stay_unsigned();
    the_field_overload_agrees_with_the_view();
    every_difference_follows_the_rule();
    return check::done("test_ring_index_is_at_or_ahead");
}
