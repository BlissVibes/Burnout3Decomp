#pragma once

// build_texture_uv_rect: converts a pixel rectangle on a texture into the four texture coordinates of
// its corners, optionally mirrored horizontally and/or vertically.
//
// Retail address 0x003713A0 (game code, front end: load-screen / sprite block 0x00370000-0x00378000).
// Leaf, no loads, no result register. Evidence: the recompiler's generated C++ for unit 0x009C
// (labels 0x003713A0-0x0037141C) read in full and cross-checked against the reader's specification;
// 49 call sites reported by the reader (11 in the front-end code, 38 in the static-initialiser block
// that bakes constant rectangles at start-up), none analysed here. Status: draft, oracle pending.
//
// Behaviour. With a rectangle (x, y, w, h) in texels on a texture of tex_w by tex_h texels:
//   u_left   = x / tex_w          v_top    = y / tex_h
//   u_right  = (x + w) / tex_w    v_bottom = (y + h) / tex_h
// If bit 0 of flip_flags is set, u_left and u_right are exchanged (horizontal mirror); if bit 1 is set,
// v_top and v_bottom are exchanged (vertical mirror). Both may be set; every other bit is ignored. The
// exchange happens after the division, so a mirrored rectangle holds exactly the same four values as
// the unmirrored one, in the other order. The result is written as four 32-bit floats in the order
// u_left, v_top, u_right, v_bottom, i.e. the corner pair (u, v) of the top-left followed by the
// corner pair of the bottom-right.
//
// Arithmetic is the EE's: the two additions truncate toward zero and flush denormals, and each
// division flushes both operands, gives the signed largest finite value for a zero (or denormal)
// divisor, a signed zero for a zero dividend, and otherwise the truncated quotient; overflow clamps
// to the signed largest finite value and underflow to a signed zero. A zero divisor also sets the
// divide-by-zero sticky flags in the EE's FPU control register; this function reports that condition
// to the caller instead of touching any register. There are no NaN or infinity values: exponent-255
// bit patterns are treated as ordinary large numbers.
//
// The retail routine also copies the four values into a 16-byte temporary in its own stack frame that
// nothing reads back; this reimplementation omits that dead store.

#include <cstdint>

namespace b3::load_screen_sprites {

// The FPU control register bits (D and SD, the divide-by-zero flag and its sticky copy) that the EE
// sets when any of the four divisions has a zero divisor.
inline constexpr std::uint32_t kDivideByZeroFlags = 0x30000u;

// Writes out_uv[0..3] = u_left, v_top, u_right, v_bottom as EE float bit patterns. All float
// parameters are EE float bit patterns too. Returns true when at least one divisor flushed to zero
// (the EE would OR kDivideByZeroFlags into its FPU control register); the out_uv contents are valid
// either way. Only bits 0 and 1 of flip_flags are used.
bool build_texture_uv_rect(std::uint32_t *out_uv, std::uint32_t flip_flags,
                           std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                           std::uint32_t tex_w, std::uint32_t tex_h);

}
