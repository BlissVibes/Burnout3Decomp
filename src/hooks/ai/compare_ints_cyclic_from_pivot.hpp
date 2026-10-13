// compare_ints_cyclic_from_pivot: a qsort comparator over 32-bit signed ints that orders them
// cyclically, with the current pivot value as the smallest element.
//
// Retail address: 0x0026CBE0 (game code, owner range 0x0024B2A0-0x002AB120, "car and track loading,
// physics and AI registrars, vehicle audio"; a leaf: no callees, no stores, no stack frame, no FPU).
// Evidence: read from the recompiler's translation of the function (two element loads, one load of
// the gp-relative pivot word, six signed compares, three constant results) and from its only caller
// 0x0026CAB0, which forms this address with an immediate pair and hands it to the MSL qsort
// (0x00125338) over an int array at object+0x32980 with element size 4. No data word in the ELF
// holds this address and no other code references it, so qsort's indirect call is the only way in.
// Status: draft (oracle pending).
//
// Fixed globals: reads the signed 32-bit pivot at 0x004E19F8 (gp 0x004E8670 - 27768). The caller
// stores the pivot there just before qsort and overwrites it with -1 just after, so the value is a
// call-time read, not a captured one. The ELF has exactly three references to that word: the two
// stores in the caller and the load in this function. This function writes nothing.
//
// Behaviour, with elements a and b and pivot P (all signed 32-bit):
//   1. a == P                 -> -1   (b is not read in this case)
//   2. b == P                 -> +1
//   3. P < a and b < P        -> -1   (a sits after the pivot, b before it: a wins)
//   4. a < P and P < b        -> +1   (mirror image)
//   5. a < b                  -> -1
//   6. otherwise (b < a) ? +1 : 0
// Equivalently, with key(x) = 0 when x == P, 1 when x > P, 2 when x < P: order by key, then
// ascending within a key. The one wrinkle is the pivot tie: a == b == P returns -1 in both
// directions (rule 1 wins before rule 2 is reached), so the comparator is not antisymmetric on
// pivot-valued pairs. A "return 0 when equal" shortcut is wrong there; equal non-pivot values are
// the only 0 result. Every compare is signed: the pivot is -1 whenever no sort is in progress, so
// negative inputs are real.
#pragma once

#include "ee/guest.hpp"

#include <cstdint>

namespace b3::ai {

// Guest address of the pivot word the comparator reads (gp - 27768).
inline constexpr std::uint32_t kCyclicPivotAddress = 0x004E19F8u;

// Pure form: the six rules above over three plain values. Returns exactly -1, 0 or +1.
std::int32_t compare_ints_cyclic_from_pivot(std::int32_t a, std::int32_t b, std::int32_t pivot);

// The shape qsort calls: reads the pivot from kCyclicPivotAddress, then *a, and *b only when
// *a differs from the pivot, so the guest memory trace matches retail.
std::int32_t compare_ints_cyclic_from_pivot(ee::guest_ptr<std::int32_t> a, ee::guest_ptr<std::int32_t> b);

}  // namespace b3::ai
