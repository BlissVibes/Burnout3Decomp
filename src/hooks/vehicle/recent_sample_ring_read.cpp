// recent_sample_ring_read (retail 0x0015A750): one float out of an eight-slot ring of recent samples
// by sequence number, -1.0f when it is not among the seven most recent. See the header for the
// description, evidence and status (draft, oracle pending).

#include "hooks/vehicle/recent_sample_ring_read.hpp"

namespace b3::vehicle {

namespace {

// The miss value is built as an integer constant in retail and moved to the float register unchanged.
constexpr ee::f32 kMiss = ee::f32::lit(-1.0f);

// Only the seven sequence numbers below `next` are still readable; the eighth slot is the one the
// next write will take.
constexpr std::uint32_t kWindow = kSampleRingSlotCount;

}

std::int32_t recent_sample_ring_next_sequence(ee::guest_ptr<SampleRing> ring) {
    // Retail read order: capacity first, then the segment index, then the count in the segment.
    const std::uint32_t capacity = static_cast<std::uint32_t>(ring.at<std::int32_t>(kSampleRingSegmentCapacityMinusOneOffset).load()) + 1u;
    const std::uint32_t segment = static_cast<std::uint32_t>(ring.at<std::int32_t>(kSampleRingSegmentIndexOffset).load());
    const std::uint32_t product = segment * capacity;   // low 32 bits of the signed 64-bit product: identical for either sign
    const std::uint32_t in_segment = static_cast<std::uint32_t>(ring.at<std::int32_t>(kSampleRingSamplesInSegmentOffset).load());
    return static_cast<std::int32_t>(in_segment + product);
}

ee::f32 recent_sample_ring_read(ee::guest_ptr<SampleRing> ring, std::int32_t seq) {
    // A negative sequence number is rejected before any memory is read.
    if (seq < 0) return kMiss;

    const std::int32_t next = recent_sample_ring_next_sequence(ring);
    const std::uint32_t age = static_cast<std::uint32_t>(next) - static_cast<std::uint32_t>(seq);   // wraps, then read as signed
    if (!(static_cast<std::int32_t>(age) < static_cast<std::int32_t>(kWindow))) return kMiss;
    if (!(seq < next)) return kMiss;

    // seq is non-negative here, so the slot is a plain mask; retail's negative-remainder fixup after
    // the mask can never fire on this path and is not reproduced.
    const std::uint32_t slot = static_cast<std::uint32_t>(seq) & (kSampleRingSlotCount - 1);
    return ring.at<ee::f32>(kSampleRingSlotsOffset + 4u * slot).load();
}

}
