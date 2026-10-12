// sap_compare_x: the qsort comparator the physics frame uses to order its sweep-and-prune
// records by their float key.
//
// Retail address: 0x001B9820 (game code; a leaf, no callees, no stores, no stack frame).
// Evidence: read from the recompiler's translation of the function (two 32-bit loads at offset 0
// of each record, one EE ordered-less-than compare, two constant results), and from the physics
// frame at 0x001B8F00, which passes this address to qsort (0x00125338) with an element size of 8.
// Status: draft (oracle pending).
//
// Behaviour: returns -1 when the first record's key is strictly less than the second's under
// EE single-precision ordering, otherwise +1. There is no zero result: equal keys compare as
// +1 in either order, so the comparator is deliberately not antisymmetric for ties and our
// version keeps that, or qsort would order tied records differently from retail.
//
// The EE FPU has no NaN or infinity: an exponent field of 0xFF is just a very large finite
// magnitude, and an exponent field of 0 (denormal) is flushed to a signed zero before the
// compare, so -0 and +0 are equal. ps2::fp::key gives exactly that ordering, which is why the
// keys are taken as raw bit patterns rather than host floats.
#pragma once

#include <cstdint>

namespace b3::physics {

// One element of the array the physics frame sorts. Only `key` is read by the comparator; the
// meaning of the trailing word is not verified (probably an index or pointer).
struct SapRecord {
    std::uint32_t key;      // float bit pattern, the sweep axis position
    std::uint32_t payload;  // untouched by the comparator
};

// Pure form on bit patterns: -1 if key_a_bits orders strictly below key_b_bits, else +1.
std::int32_t sap_compare_x(std::uint32_t key_a_bits, std::uint32_t key_b_bits);

// Record form, the shape qsort calls: reads the key of each record and nothing else.
std::int32_t sap_compare_x(const SapRecord *a, const SapRecord *b);

}  // namespace b3::physics
