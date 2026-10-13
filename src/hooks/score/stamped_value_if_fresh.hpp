// stamped_value_if_fresh: returns the word an object stamped with its clock a moment ago, as long
// as that stamp is still within a caller-given window of the object's current clock; zero when the
// slot was never stamped (or was reset) or when the window has elapsed.
//
// Retail address: 0x001553A0 (game code, the "main wrapper, loader, front-end core, score and
// boost registrars" range 0x0012EB30-0x00167D10 of game/symbols/SLUS_210.50.owners.tsv). A leaf:
// 20 instruction words, no callees, no stack frame, no stores, no globals, no literal-pool loads;
// the only constant is an inline -1.0f.
// Evidence: the recompiler's unit_0015.cpp for 0x001553A0-0x001553EC read in full and checked
// against the reader's specification step by step; the writers of the same two fields in that
// unit (around 0x00155814 and 0x00157238: one stores a word to +1516 and then copies the object's
// own +12 float to +1520, the other stores 0 to +1516 and -1.0f to +1520); the three call sites
// 0x0014435C, 0x001D1B18 and 0x001D1BB0, each of which null-tests the result and then reads a
// field of the object it points at (per the reader; not re-verified here).
// Status: draft (oracle pending).
// Fixed globals: none.
//
// Behaviour. The object holds a float clock at +12, a stored word at +1516 and the clock value at
// which that word was stored at +1520 (the "stamp"). The function:
//   1. reads the stamp; if its bit pattern is exactly -1.0f the slot is empty and the result is 0,
//      without reading anything else;
//   2. otherwise reads the clock, forms expiry = stamp + window with the EE's single-precision
//      add, and if expiry is strictly below the clock under the EE's ordering the stamp is stale
//      and the result is 0, again without reading the stored word;
//   3. otherwise reads and returns the stored word.
// Equality of the stamp with -1.0f and the final ordering test are the EE's compares, which flush
// denormals to signed zero, treat -0 and +0 as equal and have no NaN or infinity (an exponent field
// of 255 is an ordinary huge finite value). The add truncates instead of rounding, flushes denormal
// operands, saturates to the signed maximal pattern instead of overflowing to infinity, and yields
// a signed zero on underflow or exact cancellation. Consequences worth naming: expiry == clock is
// fresh; a window of +0, -0 or any denormal acts as zero, so the word is returned only while the
// clock has not moved past the stamp; a negative window can make a just-stamped slot stale; a huge
// window saturates expiry and so always counts as fresh. Host float arithmetic must not replace
// any of these steps.
//
// In the game the stored word is a guest pointer (the writers copy another object's +5108 word into
// it, and the callers dereference the result), so the function returns it as a 32-bit guest address
// and 0 doubles as "none". The retail register holds the sign extension of that 32-bit word.
// The clock's units are not verified (seconds assumed).
#pragma once

#include "ee/f32.hpp"
#include "ee/guest.hpp"

#include <cstdint>

namespace b3::score {

// The object the function reads: a struct we know only by the three offsets below (the writers show
// it is at least 1524 bytes). Never stepped, so it has no guest_size.
struct StampedSlotOwner;

inline constexpr std::uint32_t kClockOffset = 12;          // f32: the object's own running clock
inline constexpr std::uint32_t kStampedValueOffset = 1516;  // u32: the stored word (a guest pointer in practice)
inline constexpr std::uint32_t kStampTimeOffset = 1520;     // f32: the clock value when the word was stored

// The stamp pattern that marks an empty slot: -1.0f exactly (the compare is the EE's, but no
// other pattern is equal to -1.0f under it either).
inline constexpr ee::f32 kNeverStamped = ee::f32::lit(-1.0f);

// `owner` is the guest address of the object (4-byte aligned); `window` is the freshness window in
// the same units as the clock. Returns the stored word at +1516 when the slot was stamped and
// stamp + window is at or above the clock, otherwise 0. Reads only +1520, then (if stamped) +12,
// then (if fresh) +1516; writes nothing.
std::uint32_t stamped_value_if_fresh(ee::guest_ptr<StampedSlotOwner> owner, ee::f32 window);

}  // namespace b3::score
