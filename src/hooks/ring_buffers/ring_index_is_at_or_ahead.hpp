#pragma once

// ring_index_is_at_or_ahead: tells whether one slot index of a ring buffer is at or ahead of another.
//
// Retail address 0x001478C0 (game code, front-end core / loader ring buffers). Leaf, no stores.
// Evidence: the recompiler's generated C++ for unit 0x0011 (labels 0x001478C0-0x00147948) read in
// full and cross-checked against the reader's specification; eight callers found in units 0x0012
// and 0x0013 (not analysed). Status: draft, oracle pending.
//
// Behaviour. The ring descriptor holds, at byte offset 4, a pointer to a header; the header holds a
// 16-bit slot count at offset 12 and a one-byte "wraps" flag at offset 14. Both indices are reduced
// to their low 16 bits and d = a - b is formed as a signed value (-65535..65535).
//   non-wrapping ring: the answer is 1 when d >= 0.
//   wrapping ring:     with half = count / 2 (truncating), the answer is 1 when 0 <= d < half, or when
//                      d < -half (a has wrapped past b); otherwise 0. Both comparisons are strict, so
//                      a ring of 0 or 1 slots answers 0 for equal indices and 1 for any negative d.
// Any non-zero wraps byte selects wrapping mode. The slot count is read even when it is not used.

#include <cstdint>

namespace b3::ring_buffers {

// The part of the ring header this function reads, at the header's own byte offsets. A host pointer
// onto guest RAM may be cast to this view (the EE and the host are both little-endian).
struct RingHeaderView {
    std::uint8_t  before[12];   // fields this function does not read
    std::uint16_t slot_count;   // offset 12
    std::uint8_t  wraps;        // offset 14: non-zero means indices wrap around the ring
    std::uint8_t  after;        // offset 15, not read
};
static_assert(sizeof(RingHeaderView) == 16);

// Returns 1 when `idx_a` is at or ahead of `idx_b` (see the header comment), else 0. Only the low
// 16 bits of each index are used.
std::int32_t ring_index_is_at_or_ahead(const RingHeaderView &header, std::uint32_t idx_a, std::uint32_t idx_b);

// The same decision from the two header fields, for callers that already hold them.
std::int32_t ring_index_is_at_or_ahead(std::uint16_t slot_count, std::uint8_t wraps, std::uint32_t idx_a, std::uint32_t idx_b);

}
