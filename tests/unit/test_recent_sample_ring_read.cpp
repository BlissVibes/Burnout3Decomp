// recent_sample_ring_read (retail 0x0015A750): reading one float back out of an eight-slot ring of
// recent samples by sequence number. The function reads guest memory, so the ring object is laid out
// in the test RAM from tests/unit/support with ee::st and the function is called with its guest
// address. Every expected value below is worked out by hand from the specification (the comments
// show the arithmetic): next = in_segment + segment * (capacity_minus_one + 1) in wrapping 32-bit
// arithmetic, a hit needs seq >= 0, seq < next and next - seq < 8, and the slot is seq & 7. None of
// the values comes from running the game.

#include "check.hpp"
#include "ee/guest.hpp"
#include "hooks/vehicle/recent_sample_ring_read.hpp"

#include <cstdint>
#include <cstring>

using namespace b3::vehicle;
using ee::f32;

namespace {

constexpr std::uint32_t kMissBits = 0xBF800000u;   // -1.0f
constexpr std::uint32_t kObjectExtent = kSampleRingSlotsOffset + 4u * kSampleRingSlotCount;   // 776 bytes: +0 .. +775

// Eight distinct patterns for the ring slots. They include the awkward ones a float path could
// corrupt: a NaN pattern, a denormal, a negative zero, all ones, the exponent-255 maximum, and the
// miss value itself (a hit on that slot must still come from the slot).
constexpr std::uint32_t kSlotPatterns[kSampleRingSlotCount] = {
    0x3F800000u,   // 1.0f
    0x7FC00000u,   // a quiet-NaN pattern
    0x00000001u,   // the smallest denormal
    0x80000000u,   // -0.0f
    0xFFFFFFFFu,   // all ones
    0x7F7FFFFFu,   // largest finite magnitude
    0xBF800000u,   // the miss value, as a stored sample
    0x42F60000u,   // 123.0f
};

void clear() {
    std::memset(ps2::g_ram, 0, ps2::kRamSize);
    std::memset(ps2::g_spr, 0, ps2::kScratchpadSize);
}

// Lays out a ring object at `base` with the three counters and the eight slot patterns, and fills
// the rest of the extent of the object, plus a margin on both sides, with a byte pattern that no
// field should be confused with.
ee::guest_ptr<SampleRing> make_ring(std::uint32_t base, std::int32_t segment_index, std::int32_t in_segment,
                                    std::int32_t capacity_minus_one) {
    for (std::uint32_t i = 0; i < kObjectExtent + 64u; ++i) ee::st<std::uint8_t>(base - 32u + i, 0xA5);
    ee::st<std::int32_t>(base + kSampleRingSegmentIndexOffset, segment_index);
    ee::st<std::int32_t>(base + kSampleRingSamplesInSegmentOffset, in_segment);
    ee::st<std::int32_t>(base + kSampleRingSegmentCapacityMinusOneOffset, capacity_minus_one);
    for (std::uint32_t i = 0; i < kSampleRingSlotCount; ++i) {
        ee::st<std::uint32_t>(base + kSampleRingSlotsOffset + 4u * i, kSlotPatterns[i]);
    }
    return ee::guest_ptr<SampleRing>{base};
}

std::uint32_t read_bits(ee::guest_ptr<SampleRing> ring, std::int32_t seq) {
    return recent_sample_ring_read(ring, seq).bits;
}

// The expected pattern for a hit on sequence number `seq`: the slot is seq & 7.
std::uint32_t slot_bits(std::int32_t seq) {
    return kSlotPatterns[static_cast<std::uint32_t>(seq) & 7u];
}

// An independent model of the specification in 64-bit arithmetic, reduced to 32 bits where the EE
// is 32-bit, used by the sweep below to cross-check the implementation on many counter values.
std::uint32_t model(std::int32_t segment_index, std::int32_t in_segment, std::int32_t capacity_minus_one, std::int32_t seq) {
    if (seq < 0) return kMissBits;
    const std::int64_t product = static_cast<std::int64_t>(segment_index) * (static_cast<std::int64_t>(capacity_minus_one) + 1);
    const std::int32_t next = static_cast<std::int32_t>(static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(in_segment) + static_cast<std::uint64_t>(product)));
    const std::int32_t age = static_cast<std::int32_t>(static_cast<std::uint32_t>(next) - static_cast<std::uint32_t>(seq));
    if (age >= 8) return kMissBits;
    if (seq >= next) return kMissBits;
    return slot_bits(seq);
}

void negative_sequence_numbers_miss_without_reading_the_object() {
    clear();
    // The object is deliberately not laid out: RAM is all zero and the pointer is arbitrary. The
    // function must still answer -1.0f, and must do so even for a null pointer, since it reads nothing.
    CHECK_EQ(read_bits(ee::guest_ptr<SampleRing>{0x00400000u}, -1), kMissBits);
    CHECK_EQ(read_bits(ee::guest_ptr<SampleRing>{0x00400000u}, INT32_MIN), kMissBits);
    CHECK_EQ(read_bits(ee::guest_ptr<SampleRing>{0}, -1), kMissBits);
    CHECK_EQ(read_bits(ee::guest_ptr<SampleRing>{0}, -8), kMissBits);
    CHECK_EQ(read_bits(ee::guest_ptr<SampleRing>{0}, -0x7FFFFFFF), kMissBits);

    // A fully populated ring where seq would be in the window if it were non-negative: still a miss.
    const auto ring = make_ring(0x00100000u, 0, 3, 9);   // next = 3
    CHECK_EQ(read_bits(ring, -1), kMissBits);
    CHECK_EQ(read_bits(ring, -5), kMissBits);
}

void the_window_is_the_seven_entries_below_next() {
    clear();
    // segment 2, capacity_minus_one 9 (stride 10), 3 samples in the segment: next = 3 + 2 * 10 = 23.
    const auto ring = make_ring(0x00100000u, 2, 3, 9);
    CHECK_EQ(recent_sample_ring_next_sequence(ring), 23);

    // Hits: 16 .. 22 (next - 7 .. next - 1), slot = seq & 7 = 0 .. 6.
    for (std::int32_t seq = 16; seq <= 22; ++seq) CHECK_EQ(read_bits(ring, seq), slot_bits(seq));
    CHECK_EQ(read_bits(ring, 16), kSlotPatterns[0]);
    CHECK_EQ(read_bits(ring, 17), kSlotPatterns[1]);
    CHECK_EQ(read_bits(ring, 22), kSlotPatterns[6]);

    // Misses: next itself (not yet written), next - 8 (shares the slot with the next write), and
    // everything further away on either side.
    CHECK_EQ(read_bits(ring, 23), kMissBits);   // age 0, seq == next
    CHECK_EQ(read_bits(ring, 15), kMissBits);   // age 8
    CHECK_EQ(read_bits(ring, 14), kMissBits);   // age 9
    CHECK_EQ(read_bits(ring, 24), kMissBits);   // age -1 passes "< 8" but seq >= next
    CHECK_EQ(read_bits(ring, 0), kMissBits);
    CHECK_EQ(read_bits(ring, INT32_MAX), kMissBits);   // age = 23 - INT32_MAX wraps to a negative value, but seq >= next

    // next = 24 moves the window to 17 .. 23, slots 1 .. 7; 16 (slot 0) drops out.
    const auto ring24 = make_ring(0x00100000u, 2, 4, 9);
    CHECK_EQ(recent_sample_ring_next_sequence(ring24), 24);
    CHECK_EQ(read_bits(ring24, 16), kMissBits);
    for (std::int32_t seq = 17; seq <= 23; ++seq) CHECK_EQ(read_bits(ring24, seq), slot_bits(seq));
    CHECK_EQ(read_bits(ring24, 23), kSlotPatterns[7]);
    CHECK_EQ(read_bits(ring24, 24), kMissBits);
}

void small_rings_and_empty_rings() {
    clear();
    // Fewer than eight samples written: only 0 .. next - 1 qualify. next = 5 + 0 * 10 = 5.
    const auto five = make_ring(0x00100000u, 0, 5, 9);
    for (std::int32_t seq = 0; seq <= 4; ++seq) CHECK_EQ(read_bits(five, seq), slot_bits(seq));
    CHECK_EQ(read_bits(five, 5), kMissBits);
    CHECK_EQ(read_bits(five, 6), kMissBits);
    CHECK_EQ(read_bits(five, 7), kMissBits);
    CHECK_EQ(read_bits(five, 8), kMissBits);

    // Exactly one sample: only sequence number 0.
    const auto one = make_ring(0x00100000u, 0, 1, 9);
    CHECK_EQ(read_bits(one, 0), kSlotPatterns[0]);
    CHECK_EQ(read_bits(one, 1), kMissBits);

    // Nothing written yet: next = 0, every non-negative sequence number misses.
    const auto empty = make_ring(0x00100000u, 0, 0, 9);
    CHECK_EQ(recent_sample_ring_next_sequence(empty), 0);
    CHECK_EQ(read_bits(empty, 0), kMissBits);   // age 0 passes "< 8", but 0 < 0 fails
    CHECK_EQ(read_bits(empty, 1), kMissBits);
    CHECK_EQ(read_bits(empty, 7), kMissBits);

    // Eight samples: next = 8, window 1 .. 7; sequence number 0 is the slot about to be overwritten.
    const auto eight = make_ring(0x00100000u, 0, 8, 9);
    CHECK_EQ(read_bits(eight, 0), kMissBits);
    for (std::int32_t seq = 1; seq <= 7; ++seq) CHECK_EQ(read_bits(eight, seq), slot_bits(seq));
}

void slot_patterns_pass_through_bit_for_bit() {
    clear();
    // next = 8 so that every slot 1 .. 7 is readable, then next = 9 for slot 0 (seq 8).
    const auto ring8 = make_ring(0x00100000u, 0, 8, 0);
    CHECK_EQ(read_bits(ring8, 1), 0x7FC00000u);   // NaN pattern, unchanged
    CHECK_EQ(read_bits(ring8, 2), 0x00000001u);   // denormal, not flushed
    CHECK_EQ(read_bits(ring8, 3), 0x80000000u);   // -0.0f keeps its sign bit
    CHECK_EQ(read_bits(ring8, 4), 0xFFFFFFFFu);
    CHECK_EQ(read_bits(ring8, 5), 0x7F7FFFFFu);
    CHECK_EQ(read_bits(ring8, 6), 0xBF800000u);   // a stored -1.0f reads back as -1.0f: indistinguishable from a miss, by design
    CHECK_EQ(read_bits(ring8, 7), 0x42F60000u);
    const auto ring9 = make_ring(0x00100000u, 0, 9, 0);
    CHECK_EQ(read_bits(ring9, 8), 0x3F800000u);   // seq 8 maps to slot 0
    CHECK_EQ(read_bits(ring9, 1), kMissBits);     // age 8: slot 1 is now out of the window

    // The returned value compares equal to -1.0f under the EE compare only when the bits say so.
    CHECK(recent_sample_ring_read(ring8, 0) == f32::lit(-1.0f));   // a miss
    CHECK(recent_sample_ring_read(ring8, 6) == f32::lit(-1.0f));   // a hit on the stored -1.0f
    CHECK(recent_sample_ring_read(ring8, 7) == f32::lit(123.0f));
}

void segments_multiply_by_capacity_plus_one() {
    clear();
    // segment 3, capacity_minus_one 4 (stride 5), 2 in the segment: next = 2 + 3 * 5 = 17, window 10 .. 16.
    const auto ring = make_ring(0x00100000u, 3, 2, 4);
    CHECK_EQ(recent_sample_ring_next_sequence(ring), 17);
    CHECK_EQ(read_bits(ring, 9), kMissBits);
    for (std::int32_t seq = 10; seq <= 16; ++seq) CHECK_EQ(read_bits(ring, seq), slot_bits(seq));
    CHECK_EQ(read_bits(ring, 17), kMissBits);

    // capacity_minus_one 0 is a stride of 1: next = in_segment + segment. 4 + 6 = 10, window 3 .. 9.
    const auto stride1 = make_ring(0x00100000u, 6, 4, 0);
    CHECK_EQ(recent_sample_ring_next_sequence(stride1), 10);
    CHECK_EQ(read_bits(stride1, 2), kMissBits);
    CHECK_EQ(read_bits(stride1, 3), slot_bits(3));
    CHECK_EQ(read_bits(stride1, 9), slot_bits(9));
    CHECK_EQ(read_bits(stride1, 10), kMissBits);

    // capacity_minus_one -1 is a stride of 0: the segment index does not matter. next = 6.
    const auto stride0 = make_ring(0x00100000u, 12345, 6, -1);
    CHECK_EQ(recent_sample_ring_next_sequence(stride0), 6);
    CHECK_EQ(read_bits(stride0, 5), slot_bits(5));
    CHECK_EQ(read_bits(stride0, 6), kMissBits);
    const auto stride0_neg = make_ring(0x00100000u, -98765, 6, -1);
    CHECK_EQ(recent_sample_ring_next_sequence(stride0_neg), 6);
    CHECK_EQ(read_bits(stride0_neg, 5), slot_bits(5));
}

void arithmetic_wraps_at_32_bits() {
    clear();
    // The product keeps only its low 32 bits: 0x10000 * 0x10000 = 2^32 -> 0, so next = in_segment = 5.
    const auto product_wraps = make_ring(0x00100000u, 0x10000, 5, 0xFFFF);
    CHECK_EQ(recent_sample_ring_next_sequence(product_wraps), 5);
    CHECK_EQ(read_bits(product_wraps, 4), slot_bits(4));
    CHECK_EQ(read_bits(product_wraps, 5), kMissBits);

    // segment 2 with capacity_minus_one 0x7FFFFFFE (stride 0x7FFFFFFF): product 0xFFFFFFFE = -2, so
    // next = 10 - 2 = 8 and the window is 1 .. 7. 64-bit arithmetic would give 2^32 + 8 instead.
    const auto product_negative = make_ring(0x00100000u, 2, 10, 0x7FFFFFFE);
    CHECK_EQ(recent_sample_ring_next_sequence(product_negative), 8);
    CHECK_EQ(read_bits(product_negative, 0), kMissBits);
    CHECK_EQ(read_bits(product_negative, 1), slot_bits(1));
    CHECK_EQ(read_bits(product_negative, 7), slot_bits(7));
    CHECK_EQ(read_bits(product_negative, 8), kMissBits);

    // capacity_minus_one 0x7FFFFFFF gives a stride of 0x80000000 (INT32_MIN). segment 1, 5 in the
    // segment: next = 0x80000005, negative as a signed word, so no non-negative seq is below it.
    // seq = INT32_MAX passes the age test (0x80000005 - 0x7FFFFFFF = 6) but fails seq < next: a miss.
    // (The specification lists this case as a hit; both compares must pass, so it is not.)
    const auto negative_next = make_ring(0x00100000u, 1, 5, 0x7FFFFFFF);
    CHECK_EQ(recent_sample_ring_next_sequence(negative_next), static_cast<std::int32_t>(0x80000005u));
    CHECK_EQ(read_bits(negative_next, INT32_MAX), kMissBits);
    CHECK_EQ(read_bits(negative_next, INT32_MAX - 1), kMissBits);
    CHECK_EQ(read_bits(negative_next, 0), kMissBits);
    CHECK_EQ(read_bits(negative_next, 4), kMissBits);
    // segment 2 doubles the stride back to 0: next = 5.
    const auto two_halves = make_ring(0x00100000u, 2, 5, 0x7FFFFFFF);
    CHECK_EQ(recent_sample_ring_next_sequence(two_halves), 5);
    CHECK_EQ(read_bits(two_halves, 4), slot_bits(4));

    // The sum wraps too: in_segment 0x7FFFFFFF plus stride 1 * segment 1 = 0x80000000, negative: all miss.
    const auto sum_wraps = make_ring(0x00100000u, 1, 0x7FFFFFFF, 0);
    CHECK_EQ(recent_sample_ring_next_sequence(sum_wraps), INT32_MIN);
    CHECK_EQ(read_bits(sum_wraps, INT32_MAX), kMissBits);
    CHECK_EQ(read_bits(sum_wraps, 0), kMissBits);

    // Negative counters are plain arithmetic: in_segment -3 with segment 1 and stride 10 gives next = 7,
    // window 0 .. 6.
    const auto negative_count = make_ring(0x00100000u, 1, -3, 9);
    CHECK_EQ(recent_sample_ring_next_sequence(negative_count), 7);
    CHECK_EQ(read_bits(negative_count, 0), slot_bits(0));
    CHECK_EQ(read_bits(negative_count, 6), slot_bits(6));
    CHECK_EQ(read_bits(negative_count, 7), kMissBits);
    // A negative next with a non-negative seq always misses: in_segment -3, segment 0: next = -3.
    const auto negative_only = make_ring(0x00100000u, 0, -3, 9);
    CHECK_EQ(read_bits(negative_only, 0), kMissBits);   // age -3 passes "< 8", 0 < -3 fails
    CHECK_EQ(read_bits(negative_only, INT32_MAX), kMissBits);
}

void the_largest_sequence_numbers() {
    clear();
    // next = INT32_MAX (in_segment INT32_MAX, segment 0): window INT32_MAX - 7 .. INT32_MAX - 1.
    const auto top = make_ring(0x00100000u, 0, INT32_MAX, 9);
    CHECK_EQ(recent_sample_ring_next_sequence(top), INT32_MAX);
    CHECK_EQ(read_bits(top, INT32_MAX), kMissBits);                                 // age 0
    CHECK_EQ(read_bits(top, INT32_MAX - 1), kSlotPatterns[6]);                      // 0x7FFFFFFE & 7 = 6
    CHECK_EQ(read_bits(top, INT32_MAX - 7), kSlotPatterns[0]);                      // 0x7FFFFFF8 & 7 = 0
    CHECK_EQ(read_bits(top, INT32_MAX - 8), kMissBits);                             // age 8
    CHECK_EQ(read_bits(top, 0), kMissBits);                                         // age INT32_MAX
    CHECK_EQ(read_bits(top, 8), kMissBits);
}

void nothing_is_written_and_the_object_can_sit_anywhere() {
    clear();
    // Objects at an odd-looking but 4-aligned address, and near the top of RAM: the extent is
    // +0 .. +775, and make_ring fills a 32-byte margin on each side, so the last base keeps that
    // margin inside RAM.
    const std::uint32_t bases[] = {0x00000040u, 0x0012345Cu, 0x01F00004u, ps2::kRamSize - kObjectExtent - 32u};
    for (const std::uint32_t base : bases) {
        const auto ring = make_ring(base, 1, 4, 9);   // next = 14, window 7 .. 13
        CHECK_EQ(recent_sample_ring_next_sequence(ring), 14);
        CHECK_EQ(read_bits(ring, 6), kMissBits);
        CHECK_EQ(read_bits(ring, 7), kSlotPatterns[7]);
        CHECK_EQ(read_bits(ring, 8), kSlotPatterns[0]);
        CHECK_EQ(read_bits(ring, 13), kSlotPatterns[5]);
        CHECK_EQ(read_bits(ring, 14), kMissBits);
    }

    // The function is read-only: RAM after a run of hits and misses equals RAM before it.
    clear();
    const auto ring = make_ring(0x00200000u, 2, 3, 9);
    static std::uint8_t snapshot[ps2::kRamSize];
    std::memcpy(snapshot, ps2::g_ram, ps2::kRamSize);
    for (std::int32_t seq = -3; seq <= 30; ++seq) (void)read_bits(ring, seq);
    (void)read_bits(ring, INT32_MAX);
    (void)read_bits(ring, INT32_MIN);
    CHECK(std::memcmp(snapshot, ps2::g_ram, ps2::kRamSize) == 0);
}

// A deterministic sweep of counter values (including the extremes) against the 64-bit model of the
// specification, with sequence numbers around the computed next and at the extremes.
void sweep_against_the_model() {
    clear();
    const std::int32_t interesting[] = {0, 1, 2, 7, 8, 9, 15, 16, 100, -1, -2, -8, -100, 0x7FFF, 0x10000,
                                        0x7FFFFFFE, INT32_MAX, INT32_MIN, static_cast<std::int32_t>(0x80000001u)};
    const std::int32_t seq_offsets[] = {-9, -8, -7, -4, -1, 0, 1, 2, 8};
    const std::int32_t fixed_seqs[] = {0, 1, 7, 8, INT32_MAX, INT32_MAX - 1, -1, INT32_MIN};
    std::uint32_t state = 0x12345678u;
    int compared = 0;
    for (const std::int32_t segment : interesting) {
        for (const std::int32_t in_segment : interesting) {
            for (const std::int32_t capacity_minus_one : interesting) {
                const auto ring = make_ring(0x00300000u, segment, in_segment, capacity_minus_one);
                const std::int32_t next = recent_sample_ring_next_sequence(ring);
                for (const std::int32_t offset : seq_offsets) {
                    const std::int32_t seq = static_cast<std::int32_t>(static_cast<std::uint32_t>(next) + static_cast<std::uint32_t>(offset));
                    CHECK_EQ(read_bits(ring, seq), model(segment, in_segment, capacity_minus_one, seq));
                    ++compared;
                }
                for (const std::int32_t seq : fixed_seqs) {
                    CHECK_EQ(read_bits(ring, seq), model(segment, in_segment, capacity_minus_one, seq));
                    ++compared;
                }
                // Two pseudo-random sequence numbers per counter triple (a small linear congruential step).
                for (int i = 0; i < 2; ++i) {
                    state = state * 1664525u + 1013904223u;
                    const std::int32_t seq = static_cast<std::int32_t>(state);
                    CHECK_EQ(read_bits(ring, seq), model(segment, in_segment, capacity_minus_one, seq));
                    ++compared;
                }
            }
        }
    }
    CHECK(compared > 100000);
}

}

int main() {
    negative_sequence_numbers_miss_without_reading_the_object();
    the_window_is_the_seven_entries_below_next();
    small_rings_and_empty_rings();
    slot_patterns_pass_through_bit_for_bit();
    segments_multiply_by_capacity_plus_one();
    arithmetic_wraps_at_32_bits();
    the_largest_sequence_numbers();
    nothing_is_written_and_the_object_can_sit_anywhere();
    sweep_against_the_model();
    return check::done("test_recent_sample_ring_read");
}
