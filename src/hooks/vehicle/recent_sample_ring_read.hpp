#pragma once

// recent_sample_ring_read: fetch one float back out of an eight-slot ring of recent samples by its
// sequence number, or -1.0f when that sequence number is no longer (or not yet) in the ring.
//
// Retail address 0x0015A750 (game code, boundary-table row 0x0012EB30-0x00167D10). Leaf, 32
// instructions, no stack, no globals, no float arithmetic: the result is a bit pattern copied out of
// the ring, or the constant -1.0f built in an integer register.
//
// Evidence: the recompiled unit for this address (fork generated/burnout3/unit_0016.cpp, label
// L_0015A750) read against the reader's specification, which had decoded the ELF words independently;
// the two agree. Call sites 0x001A2B08, 0x001A2B60 and 0x001A2C48 sit in one vehicle function; the
// first passes `total - 1`, where `total` comes from the sibling leaf 0x0015A7D0 (which computes
// `base + segment * (capacity_minus_one + 1)`, the same stride as here), and the ring object is a
// pointer loaded from the caller's object at +204, so it is a separate structure, not the vehicle.
// The meaning of the counters is inferred from the writer in the same unit (0x0015B4E4-0x0015B5C4),
// which computes the same `total`, stores a float at slot `total mod 8` and then increments the
// per-segment count; that reading is not oracle-verified.
//
// Status: draft (oracle pending). Fixed globals used: none.

#include "ee/f32.hpp"
#include "ee/guest.hpp"

#include <cstdint>

namespace b3::vehicle {

// The ring object, known only by the offsets this function reads. Fields are read at the retail
// offsets through guest memory; the object's full size is unknown, so the tag has no guest_size.
struct SampleRing;

inline constexpr std::uint32_t kSampleRingSegmentIndexOffset = 20;              // s32: segment (lap-like) index
inline constexpr std::uint32_t kSampleRingSamplesInSegmentOffset = 732;         // s32: samples written in the current segment
inline constexpr std::uint32_t kSampleRingSegmentCapacityMinusOneOffset = 736;  // s32: per-segment capacity minus one
inline constexpr std::uint32_t kSampleRingSlotsOffset = 744;                    // f32[8]: the ring itself
inline constexpr std::uint32_t kSampleRingSlotCount = 8;

// The sequence number of the next sample the writer will store:
//   samples_in_segment + segment_index * (segment_capacity_minus_one + 1)
// in wrapping 32-bit arithmetic (the product keeps only its low 32 bits). Reads the three counters in
// the retail order: +736, +20, +732.
std::int32_t recent_sample_ring_next_sequence(ee::guest_ptr<SampleRing> ring);

// Returns the sample with sequence number `seq`, or -1.0f (bit pattern 0xBF800000) when it is not one
// of the seven most recent: a hit needs `seq >= 0`, `seq < next` and `next - seq < 8` with `next` as
// above, i.e. `seq` in `next - 7 .. next - 1`. Sequence number `next - 8` shares its slot with the
// sample about to be written and is rejected. The ring slot is `seq & 7`.
//
// `seq < 0` is answered without touching memory. On a miss the three counters are still read but
// the ring is not. The returned pattern is whatever the slot holds, bit for bit (denormals, NaN
// patterns and -0 included); nothing is written.
ee::f32 recent_sample_ring_read(ee::guest_ptr<SampleRing> ring, std::int32_t seq);

}
