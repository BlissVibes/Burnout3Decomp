#pragma once

// lowest_set_bit_index: position of the lowest set bit of a 32-bit mask, or 32 when the mask is zero.
//
// Retail address 0x00299550 (game code, SLUS-21050; boundary-table row "car and track loading,
// physics, AI, audio, RWA client, effects", 0x0024B2A0-0x002C26D0). Leaf: no callees, no stack
// frame, no memory access, no globals, no floating point. Evidence: the recompiler's translation
// (unit_0066.cpp, labels 0x00299550-0x00299580) read in full against the reader's specification;
// the two agree. The reader found 126 call sites in 0x00297C7C-0x002B38D4, most passing the
// constants 64, 40 or 48 or a sign-extended byte, and masking the result to 8 bits before turning
// it back into a 16-bit single-bit mask; that reading of the callers is not verified here.
// Status: draft, oracle pending.
//
// Behaviour: the bit positions 0..31 are tested in order against the mask and the first position
// whose bit is set is returned. When no bit is set, all 32 tests fail and the result is 32, one past
// the highest valid position. The loop counter is an 8-bit value in the original, which has no
// observable effect because it never exceeds 32. The result is always 0..32 and is never negative.
// The retail test at position 31 is a 64-bit compare against a sign-extended one-bit mask, so a
// register whose low word is zero while its high word is not would answer 31; that state cannot be
// produced by any 32-bit caller and is not modelled, the parameter being a plain 32-bit value.

#include <cstdint>

namespace b3::audio {

// Returns the index (0..31) of the lowest set bit of `mask`, or 32 when `mask` is zero.
std::uint8_t lowest_set_bit_index(std::uint32_t mask);

}
