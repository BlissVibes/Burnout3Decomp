// select_gear_under_rev_limit: walks a vehicle's gear ratio table upwards from first gear and
// picks the first gear whose implied engine speed is at or under the rev limit.
//
// Retail address: 0x001C6F30 (game code, physics window; a leaf: no callees, no stack frame,
// one 32-bit store). Next function starts at 0x001C6F90.
// Evidence: the recompiler's unit_0031.cpp for 0x001C6F30-0x001C6F88 read in full; the gp-relative
// constant at 0x004E0D80 read from the ELF (0x3DD67750, the nearest single to pi/30); the three
// call sites 0x001BEEA4, 0x001C0A68 and 0x001D1770 (each passes vehicle+0x1448, the transmission
// block of docs/research/decomp-groundwork.md section 6, stores the midpoint of two scaled config
// values to +84 beforehand and the returned index to +128 afterwards). The reader's specification
// was checked line by line against the generated code before this was written.
// Status: draft (oracle pending).
//
// Behaviour. The block is a run of 32-bit words. Word 33 (byte +132) is the signed entry count.
// The function forms r = dividend / divisor from its two float arguments, and the limit as
// (pi/30) * word 10 (byte +40; a rev limit in rpm turned into rad/s is the plausible reading,
// unverified). Then for i = 1, 2, ... while i < count it reads ratio = word 1+i (byte +4+4i) and
// mult = word 8 (byte +32, re-read every pass), computes value = mult * (r * ratio) as two
// separate truncating EE multiplies in exactly that grouping, and stops at the first value that
// is at or under the limit under EE ordering. On exit the last computed value is stored to word
// 21 (byte +84) unconditionally and i is returned: the index of the chosen entry, or count when
// no entry qualified (one past the last examined entry, which callers store unchecked).
//
// When count <= 1 (including zero and negative counts) no entry is examined and the retail code
// stores whatever float register value the caller happened to leave behind. A host function
// cannot see that register, so the caller passes it in as `inherited_value_bits`; tests use a
// sentinel there. Entry 0 (word 1) is never read.
//
// Indices are not bounds-checked, so a count of 8 or more reads the scalars after the table as
// if they were ratios (word 8 is both the multiplier and "entry 7"); working on the word array
// reproduces that aliasing without special cases. The reads of the count, the constant and the
// rev limit, the divide and the limit multiply all happen before the first loop test, so a
// divide by zero is flagged even when the loop never runs.
//
// EE float semantics (through ps2::fp): the divide flushes denormals, goes through double and
// truncates; x/0 (including 0/0) yields the sign with all magnitude bits set and raises the
// FPU's invalid flags, which the function leaves set (reported through `divide_by_zero`).
// Multiplies truncate, flush denormals to a signed zero and saturate to sign|0x7FFFFFFF instead
// of producing an infinity; there is no NaN, an exponent field of 255 is just a huge finite
// value. The comparison is the sign-magnitude ordering with denormals flushed, so -0 <= +0 and
// +0 <= -0 both hold. Host float arithmetic must not be substituted for any of these steps.
#pragma once

#include <bit>
#include <cstdint>
#include <numbers>

namespace b3::physics {

// Word indices into the transmission block (byte offset = 4 * index).
inline constexpr int kGearRatioWordBase = 1;   // entry i lives at word 1 + i; entry 0 is unused here
inline constexpr int kGearMultiplierWord = 8;  // +32: multiplies every r * ratio product (final drive?)
inline constexpr int kRevLimitWord = 10;       // +40: scaled by pi/30 to form the limit
inline constexpr int kEngineSpeedWord = 21;    // +84: receives the last computed value
inline constexpr int kGearCountWord = 33;      // +132: signed entry count

// The nearest single-precision value to pi/30 (rpm to rad/s), which is what the retail constant
// holds; derived here from pi rather than written down (the .cpp pins the resulting pattern).
inline constexpr std::uint32_t kRadiansPerSecondPerRpmBits =
    std::bit_cast<std::uint32_t>(static_cast<float>(std::numbers::pi / 30.0));

// `block` is the transmission block viewed as 32-bit words (byte +0 at index 0). `dividend_bits`
// and `divisor_bits` are the two float arguments as raw bit patterns (r = dividend / divisor).
// `inherited_value_bits` is stored to word 21 when no entry is examined (count <= 1). When
// `divide_by_zero` is given it receives whether the divide was by (a flushed) zero, which on the
// EE also sets the FPU invalid flags. Returns the selected 1-based index, or count when none
// qualified; always >= 1.
std::int32_t select_gear_under_rev_limit(std::uint32_t *block, std::uint32_t divisor_bits,
                                         std::uint32_t dividend_bits,
                                         std::uint32_t inherited_value_bits,
                                         bool *divide_by_zero = nullptr);

}  // namespace b3::physics
