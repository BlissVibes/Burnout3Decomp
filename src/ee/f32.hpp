#pragma once

// ee::f32: a single-precision value as the Emotion Engine's FPU computes with it.
//
// The EE FPU is not IEEE: it has no denormals, infinities or NaNs, flushes tiny results to zero and
// truncates every result toward zero. A host `float` rounds to nearest and keeps denormals, so
// decompiled code written with native floats differs from the game in the last bit. f32 holds the
// raw bit pattern and maps each operator onto the same ps2::fp helper the recompiled code calls
// (fork: runtime/include/ps2/fpu.hpp), so the two give identical bits.
//
//   add.s sub.s mul.s    a + b, a - b, a * b
//   div.s                a / b, or div(c, a, b) to raise the fcr31 flags as the instruction does
//   neg.s abs.s          -a, abs(a)               (sign-bit operations: no flush, no rounding)
//   sqrt.s rsqrt.s       sqrt(a), rsqrt(a, b) = a / sqrt(b), or rsqrt(c, a, b) with the flags
//   max.s min.s          max(a, b), min(a, b)
//   madd.s msub.s        madd(acc, a, b), msub(acc, a, b): two truncations, never fused
//   c.eq.s c.lt.s c.le.s a == b, a < b, a <= b    (> and >= are the same tests with operands swapped)
//   cvt.w.s cvt.s.w      a.to_s32() truncates and saturates; f32::from_s32(n) truncates
//
// Constants: f32::lit(17.8816f) keeps the bit pattern the compiler stored for the literal.

#include "ps2/cpu.hpp"
#include "ps2/fpu.hpp"

#include <bit>
#include <cstdint>

namespace ee {

struct f32 {
    std::uint32_t bits = 0;

    static constexpr f32 from_bits(std::uint32_t raw) { return {raw}; }
    static constexpr f32 lit(float value) { return {std::bit_cast<std::uint32_t>(value)}; }
    static f32 from_s32(std::int32_t value) { return {ps2::fp::cvt_s_w(static_cast<std::uint32_t>(value))}; }

    std::int32_t to_s32() const { return static_cast<std::int32_t>(ps2::fp::cvt_w_s(bits)); }
    // The value as a host float, for printing and logging only: never compute with it.
    constexpr float host() const { return std::bit_cast<float>(bits); }
};

inline f32 operator+(f32 a, f32 b) { return {ps2::fp::add(a.bits, b.bits)}; }
inline f32 operator-(f32 a, f32 b) { return {ps2::fp::sub(a.bits, b.bits)}; }
inline f32 operator*(f32 a, f32 b) { return {ps2::fp::mul(a.bits, b.bits)}; }
inline f32 operator/(f32 a, f32 b) { return {ps2::fp::div(a.bits, b.bits).value}; }
constexpr f32 operator-(f32 a) { return {a.bits ^ ps2::fp::kSign}; }
constexpr f32 abs(f32 a) { return {a.bits & ps2::fp::kAbs}; }

inline f32 &operator+=(f32 &a, f32 b) { return a = a + b; }
inline f32 &operator-=(f32 &a, f32 b) { return a = a - b; }
inline f32 &operator*=(f32 &a, f32 b) { return a = a * b; }

inline f32 sqrt(f32 a) { return {ps2::fp::sqrt(a.bits)}; }
inline f32 rsqrt(f32 a, f32 b) { return {ps2::fp::rsqrt(a.bits, b.bits).value}; }
inline f32 max(f32 a, f32 b) { return {ps2::fp::max(a.bits, b.bits)}; }
inline f32 min(f32 a, f32 b) { return {ps2::fp::min(a.bits, b.bits)}; }
inline f32 madd(f32 acc, f32 a, f32 b) { return acc + a * b; }
inline f32 msub(f32 acc, f32 a, f32 b) { return acc - a * b; }

// The flags div.s and rsqrt.s raise in fcr31 when the divisor is zero (what the runtime's fpu_div sets).
inline constexpr std::uint32_t kDivideFlags = 0x30000u;

inline f32 div(ps2::Cpu &c, f32 a, f32 b) {
    const ps2::fp::Result r = ps2::fp::div(a.bits, b.bits);
    if (r.invalid) c.fcr31 |= kDivideFlags;
    return {r.value};
}

inline f32 rsqrt(ps2::Cpu &c, f32 a, f32 b) {
    const ps2::fp::Result r = ps2::fp::rsqrt(a.bits, b.bits);
    if (r.invalid) c.fcr31 |= kDivideFlags;
    return {r.value};
}

// Comparisons order by sign and magnitude with denormals counted as zero, so +0 == -0 and a denormal
// == 0. Compare .bits when the exact pattern matters.
inline bool operator==(f32 a, f32 b) { return ps2::fp::key(a.bits) == ps2::fp::key(b.bits); }
inline bool operator!=(f32 a, f32 b) { return !(a == b); }
inline bool operator<(f32 a, f32 b) { return ps2::fp::key(a.bits) < ps2::fp::key(b.bits); }
inline bool operator<=(f32 a, f32 b) { return ps2::fp::key(a.bits) <= ps2::fp::key(b.bits); }
inline bool operator>(f32 a, f32 b) { return b < a; }
inline bool operator>=(f32 a, f32 b) { return b <= a; }

}
