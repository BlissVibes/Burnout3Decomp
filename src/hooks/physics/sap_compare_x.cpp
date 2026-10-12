// sap_compare_x (retail 0x001B9820): see sap_compare_x.hpp for the description and evidence.
// Status: draft (oracle pending).
#include "hooks/physics/sap_compare_x.hpp"

#include "ps2/fpu.hpp"

namespace b3::physics {

std::int32_t sap_compare_x(std::uint32_t key_a_bits, std::uint32_t key_b_bits) {
    // fp::key flushes denormals to signed zero and maps the pattern onto a sign-magnitude
    // integer line, which is the EE's ordered-less-than over every 32-bit pattern.
    return ps2::fp::key(key_a_bits) < ps2::fp::key(key_b_bits) ? -1 : 1;
}

std::int32_t sap_compare_x(const SapRecord *a, const SapRecord *b) {
    return sap_compare_x(a->key, b->key);
}

}  // namespace b3::physics
