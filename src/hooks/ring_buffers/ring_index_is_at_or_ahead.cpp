// ring_index_is_at_or_ahead, retail 0x001478C0. See the header for the description, evidence and status
// (draft, oracle pending). Pure integer logic: no floating point, no memory writes.

#include "hooks/ring_buffers/ring_index_is_at_or_ahead.hpp"

namespace b3::ring_buffers {

std::int32_t ring_index_is_at_or_ahead(std::uint16_t slot_count, std::uint8_t wraps, std::uint32_t idx_a, std::uint32_t idx_b) {
    // Indices are 16-bit slot numbers; the difference fits comfortably in a signed 32-bit value.
    const std::int32_t d = static_cast<std::int32_t>(idx_a & 0xFFFFu) - static_cast<std::int32_t>(idx_b & 0xFFFFu);
    if (wraps == 0) return d >= 0 ? 1 : 0;

    // Wrapping ring: "ahead" means ahead by less than half the ring, counting wrap-around. The slot
    // count is unsigned and halved by truncation, so half is 0..32767.
    const std::int32_t half = static_cast<std::int32_t>(static_cast<std::uint32_t>(slot_count) >> 1);
    if (d >= 0) return d < half ? 1 : 0;
    return d < -half ? 1 : 0;
}

std::int32_t ring_index_is_at_or_ahead(const RingHeaderView &header, std::uint32_t idx_a, std::uint32_t idx_b) {
    return ring_index_is_at_or_ahead(header.slot_count, header.wraps, idx_a, idx_b);
}

}
