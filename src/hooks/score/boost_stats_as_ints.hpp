// boost_stats_as_ints: read three floats from the record an object points at and hand them back
// as three truncated 32-bit ints through three out pointers.
//
// Retail address 0x0014E7E0 (game code, boundary-table row 0x0012EB30-0x00167D10: main wrapper,
// loader, front-end core, score and boost registrars; high confidence). Leaf: 18 instruction words
// up to and including the return's delay slot (0x48 bytes, then two padding words before the
// sibling at 0x0014E830). Straight-line, no stack frame, no globals, no callees.
//
// Evidence: the recompiler's translation of the function (fork generated/burnout3/unit_0013.cpp,
// label L_0014E7E0) read against the reader's specification, which was itself decoded from the raw
// instruction words; the two agree on every load, conversion and store and on their order. The
// conversion is the EE's cvt.w.s, checked against the fork's ps2::fp::to_fixed. Call sites
// 0x00155AF8 (score/boost window, stack slots), 0x0038C588 (HUD, three int globals 0x004A4FA8,
// 0x004A4FAC, 0x004A4FB0) and 0x0039C9DC (HUD, stack slots) all pass the fixed object 0x006481A0
// as the first argument. The sibling 0x0014E830 stores the same three floats unconverted.
//
// Status: draft (oracle pending). "Boost" is a label taken from the neighbourhood (the boost bar
// registrar at 0x0014E670 is the nearest named routine); what the three floats mean is unverified.
//
// Fixed globals: none inside the function. It only dereferences its arguments. The object address
// every caller passes is recorded below for the harness and the shim, not used here.
//
// Behaviour, in retail order:
//   1. P = 32-bit guest pointer at object + 1380;  *out_first  = cvt(float at P + 936)
//   2. P = 32-bit guest pointer at object + 1380;  *out_second = cvt(float at P + 932)
//   3. P = 32-bit guest pointer at object + 1380;  *out_third  = cvt(float at P + 928)
// The field offsets descend while the out pointers ascend. The pointer is re-read before every
// field and each store lands before the next pointer read, so an out pointer that aliases the
// pointer field or a later field changes what the following steps read, exactly as in retail.
// Every access is a 32-bit word; retail needs 4-byte alignment on all four pointers and performs
// no null checks (preconditions, not special-cased here).
//
// cvt is the EE float-to-int conversion (ee::f32::to_s32): zero and denormals give 0; any |x| < 1
// gives 0 (so -0.99 -> 0, not -1); otherwise truncation toward zero (-1.5 -> -1, 2.9 -> 2);
// |x| >= 2^31 saturates to 0x7FFFFFFF when positive and 0x80000000 when negative, and every
// exponent-255 pattern (IEEE infinity or NaN) is a huge finite value on the EE that saturates the
// same way. A host (int)float cast differs on all of these cases.
#pragma once

#include "ee/guest.hpp"

#include <cstdint>

namespace b3::score {

// The object every known caller passes as the first argument (lui/addiu pair at all three sites).
inline constexpr std::uint32_t kBoostStatsObjectAddress = 0x006481A0u;

// Layout used by this getter: the record pointer inside the object and the three floats in the record.
inline constexpr std::uint32_t kBoostStatsRecordPointerOffset = 1380u;
inline constexpr std::uint32_t kBoostStatsFirstFieldOffset = 936u;    // -> out_first
inline constexpr std::uint32_t kBoostStatsSecondFieldOffset = 932u;   // -> out_second
inline constexpr std::uint32_t kBoostStatsThirdFieldOffset = 928u;    // -> out_third

// Writes the three converted ints to out_first, out_second and out_third, in that order. No result.
void boost_stats_as_ints(ee::guest_ptr<void> object,
                         ee::guest_ptr<std::int32_t> out_first,
                         ee::guest_ptr<std::int32_t> out_second,
                         ee::guest_ptr<std::int32_t> out_third);

}  // namespace b3::score
