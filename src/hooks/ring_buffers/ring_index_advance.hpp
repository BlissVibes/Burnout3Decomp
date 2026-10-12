#pragma once

// ring_index_advance: move a 16-bit position through a front-end ring buffer by a signed step,
// either wrapping around the buffer or clamping at its ends depending on a flag in the buffer header.
//
// Retail address 0x00145E70 (game code, boundary-table row 0x0012EB30-0x00167D10: main wrapper,
// loader, front-end core). Leaf, 32 instructions, no stack, no globals, no float work.
//
// Evidence: the recompiled unit for this address (fork generated/burnout3/unit_0011.cpp) read side by
// side with the reader's specification; the three multimedia words it uses were decoded by hand as a
// signed word max, a signed word min and a zero-extending low-word unpack, and the fork's mmi.hpp was
// checked for the divide and lane semantics. Call sites 0x0014AA14, 0x0014B7FC, 0x0014B814 and
// 0x0014BC08 pass 16-bit indices and sign-extended 16-bit steps and keep 16 bits of the result.
//
// Status: draft (oracle pending). The size-zero behaviour of the wrapping path is the fork's
// divide-by-zero convention, not a hardware observation.

#include <cstdint>

namespace b3::ring_buffers {

// The buffer header the descriptor points at. Only two fields are read; the retail object keeps
// the capacity at byte offset 12 and the wrap flag at byte offset 14, both unsigned.
struct RingHeader {
    std::uint8_t unread_before_capacity[12];
    std::uint16_t capacity;   // number of slots, 0..65535
    std::uint8_t wraps;       // non-zero: positions wrap around; zero: positions clamp to the ends
};

// The descriptor the caller holds. Retail keeps a 32-bit guest pointer to the header at byte offset 4;
// the hooks table translates that address to this host pointer.
struct RingDesc {
    std::uint32_t unread_first_word;
    const RingHeader *header;
};

// Returns the position `delta` slots on from `index`.
//
//   delta == 0   returns `index` without reading the descriptor at all (it may be null).
//   wrapping     result = (index + delta + capacity) mod capacity with a truncating (C) remainder, so
//                one wrap below zero works and a larger negative overshoot gives a negative remainder
//                that comes back masked to 16 bits (for example 0xFFFF). Any positive overshoot is fine.
//                capacity == 0 returns (index + delta) & 0xFFFF, matching the fork's emulated divide.
//   clamping     result = min(max(index + delta, 0), capacity - 1) in signed 32-bit arithmetic, so a
//                capacity of 0 yields 0xFFFF (the upper bound is -1, masked).
// The result is the low 16 bits of the computed position.
std::uint16_t ring_index_advance(const RingDesc *desc, std::uint16_t index, std::int16_t delta);

}
