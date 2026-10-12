// lowest_set_bit_index, retail 0x00299550: lowest set bit position of a 32-bit mask, 32 when none.
// Description, evidence and status (draft, oracle pending) are in lowest_set_bit_index.hpp.

#include "hooks/audio/lowest_set_bit_index.hpp"

namespace b3::audio {

std::uint8_t lowest_set_bit_index(std::uint32_t mask) {
    // Each position is tested with a freshly formed single-bit mask; nothing accumulates between
    // iterations, so the loop is written exactly as the original behaves rather than as a
    // builtin count-trailing-zeros, whose result for zero is unspecified.
    for (std::uint8_t i = 0; i < 32; ++i) {
        if (mask & (std::uint32_t{1} << i)) return i;
    }
    return 32;
}

}
