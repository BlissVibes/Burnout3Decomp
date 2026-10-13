// compare_ints_cyclic_from_pivot (retail 0x0026CBE0): see compare_ints_cyclic_from_pivot.hpp for
// the description, evidence and the fixed global it reads.
// Status: draft (oracle pending).
#include "hooks/ai/compare_ints_cyclic_from_pivot.hpp"

namespace b3::ai {

std::int32_t compare_ints_cyclic_from_pivot(std::int32_t a, std::int32_t b, std::int32_t pivot) {
    // The pivot itself sorts first; a's side is decided before b's is even looked at.
    if (a == pivot) return -1;
    if (b == pivot) return 1;
    // Values after the pivot sort before values that precede it (the cyclic wrap).
    if (pivot < a && b < pivot) return -1;
    if (a < pivot && pivot < b) return 1;
    // Same side of the pivot: plain ascending order; equal values are the only 0 result.
    if (a < b) return -1;
    return (b < a) ? 1 : 0;
}

std::int32_t compare_ints_cyclic_from_pivot(ee::guest_ptr<std::int32_t> a, ee::guest_ptr<std::int32_t> b) {
    // Retail loads *a and the pivot first and returns before touching *b when they are equal, so
    // the pointer form keeps that order rather than loading both elements up front.
    const std::int32_t value_a = *a;
    const std::int32_t pivot = ee::ld<std::int32_t>(kCyclicPivotAddress);
    if (value_a == pivot) return -1;
    return compare_ints_cyclic_from_pivot(value_a, *b, pivot);
}

}  // namespace b3::ai
