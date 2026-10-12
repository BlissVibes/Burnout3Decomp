// ring_index_advance (retail 0x00145E70): step a 16-bit ring-buffer position, wrapping or clamping.
// See ring_index_advance.hpp for the description, evidence and status (draft, oracle pending).

#include "hooks/ring_buffers/ring_index_advance.hpp"

namespace b3::ring_buffers {

std::uint16_t ring_index_advance(const RingDesc *desc, std::uint16_t index, std::int16_t delta) {
    // A zero step is answered before the descriptor is touched: the original reads nothing then.
    if (delta == 0) return index;

    const RingHeader *header = desc->header;
    const std::int32_t position = static_cast<std::int32_t>(index) + delta;   // -32768 .. 98302
    const std::int32_t capacity = header->capacity;                            // 0 .. 65535

    std::int32_t result;
    if (header->wraps != 0) {
        // One capacity is added first so a single step below zero lands at the far end; the
        // remainder keeps the dividend's sign, as the EE's integer divide does.
        const std::int32_t shifted = position + capacity;
        result = capacity != 0 ? shifted % capacity : shifted;   // zero capacity: the fork's divide leaves the dividend as the remainder
    } else {
        // Signed 32-bit clamp to [0, capacity - 1]; with capacity 0 the upper bound is -1.
        result = position > 0 ? position : 0;
        const std::int32_t last = capacity - 1;
        if (last < result) result = last;
    }
    return static_cast<std::uint16_t>(result & 0xFFFF);
}

}
