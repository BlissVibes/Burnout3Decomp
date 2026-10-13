// event_stamp_within_window: answers whether a timestamped event happened, and happened no longer
// than a caller-given window before the shared clock it is measured against.
//
// Retail address: 0x00156170 (game code, the "main wrapper, loader, front-end core, score and
// boost registrars" range 0x0012EB30-0x00167D10 of game/symbols/SLUS_210.50.owners.tsv). A leaf:
// 16 instruction words, no callees, no stack frame, no stores, no globals, no literal-pool loads.
// Evidence: the recompiler's unit_0015.cpp for 0x00156170-0x001561AC read in full and checked
// against the reader's specification step by step; the three siblings at 0x00156080, 0x001560D0
// and 0x00156120 read the same way and found to differ from this function only in the stamp
// offset (5120, 5760 and 5500 against 5512 here). Call sites (14, all in game-owned code,
// per the reader's jal-word scan, not re-verified here): 0x001408F0, 0x00151E2C, 0x001BD5A8,
// 0x001C16B8, 0x001C16E8, 0x001D1B00, 0x001D1B70, 0x001D1B98, 0x0032F118, 0x0032FA98,
// 0x0032FD04, 0x00331504, 0x00331638, 0x0033197C; the first argument is *(racer+5108) + 4288 at
// the racer-side callers, the window a literal (1.0f, 2.0f, 3.0f), a field or a product.
// Status: draft (oracle pending).
// Fixed globals: none.
//
// Behaviour. The owner object holds, at +200, a guest pointer to a state block. That block holds a
// float reference clock at +4300 and a float event stamp at +5512 (the clock value when the event
// last happened). The function:
//   1. loads the state pointer, then the stamp;
//   2. if the stamp is strictly below +0.0 under the EE's ordering, the event never happened:
//      result 0, and nothing else is read;
//   3. otherwise forms deadline = stamp + window with the EE's single-precision add, loads the
//      clock, and the result is 1 when clock <= deadline under the EE's ordering, else 0.
// Both compares are the EE's: denormals flush to signed zero before comparing, -0 equals +0, and
// there is no NaN or infinity (an exponent field of 255 is an ordinary huge finite value). So a
// stamp of -0.0 or any negative denormal is NOT negative and falls through to the window test,
// while every pattern with the sign bit set and a non-zero exponent (including -1.0f and the all-ones
// word 0xFFFFFFFF) counts as "never". The add truncates instead of rounding, flushes denormal
// operands, saturates to the signed maximal pattern instead of overflowing, and gives a signed zero
// on exact cancellation or underflow. The window test is inclusive (clock == deadline gives 1), and
// the window may be zero or negative; nothing special-cases it. Host float arithmetic must not
// replace any of these steps.
//
// The pointer at +200 is dereferenced without a null or range check (a zero pointer reads guest
// addresses 5512 and 4300); that is a precondition of the caller, not something we guard.
// Which pattern the game stores as the "never" stamp (-1.0f or 0xFFFFFFFF), the units of the clock
// (seconds assumed) and the meaning of each sibling's stamp are not verified.
#pragma once

#include "ee/f32.hpp"
#include "ee/guest.hpp"

#include <cstdint>

namespace b3::score {

// The sub-object callers pass; known only by the one pointer field below. Never stepped, so no
// guest_size.
struct EventStampOwner;

// The state block the owner points at; known only by the clock and stamp offsets below.
struct EventStampState;

inline constexpr std::uint32_t kEventStateBlockOffset = 200;   // guest_ptr<EventStampState> in the owner
inline constexpr std::uint32_t kEventClockOffset = 4300;       // f32: the reference clock in the state block
inline constexpr std::uint32_t kEventStampOffset = 5512;       // f32: the stamp this function (0x00156170) reads

// The stamp offsets the three sibling functions read with otherwise identical code (observed in the
// generated translation; the siblings themselves are not implemented here).
inline constexpr std::uint32_t kEventStampOffsetSibling0x00156080 = 5120;
inline constexpr std::uint32_t kEventStampOffsetSibling0x001560D0 = 5760;
inline constexpr std::uint32_t kEventStampOffsetSibling0x00156120 = 5500;

// The shared core: the decision above with the stamp read at `stamp_offset` from the state block.
// `owner` is the guest address of the owner object (4-byte aligned); `window` is in the clock's
// units. Returns 1 when the stamp is not negative and clock <= stamp + window, else 0. Reads only
// owner+200, then state+stamp_offset, then (if the stamp is not negative) state+4300; writes nothing.
std::int32_t event_stamp_within_window_at(ee::guest_ptr<EventStampOwner> owner, std::uint32_t stamp_offset,
                                          ee::f32 window);

// The function at 0x00156170: the core with the stamp at +5512.
std::int32_t event_stamp_within_window(ee::guest_ptr<EventStampOwner> owner, ee::f32 window);

}  // namespace b3::score
