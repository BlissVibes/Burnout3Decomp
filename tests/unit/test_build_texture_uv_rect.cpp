// Unit test for build_texture_uv_rect (retail 0x003713A0). Standalone: own main(), exit 0 on success.
//
// Every expected value below is derived by hand from the specification of the EE float model
// (truncating add with denormal flush; divide: flush both operands, zero divisor -> sign | 0x7FFFFFFF
// and flag, zero dividend -> signed zero, else double quotient truncated to float; pack clamps
// exponent > 255 to sign | 0x7FFFFFFF and exponent <= 0 to signed zero). The derivation is written
// next to each case. Nothing here was produced by running the game or the recompiled code.
//
// Float bit-pattern notation used in the comments: sign | biased exponent | 23-bit fraction.

#include "check.hpp"
#include "hooks/load_screen_sprites/build_texture_uv_rect.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>

using b3::load_screen_sprites::build_texture_uv_rect;
using b3::load_screen_sprites::kDivideByZeroFlags;

namespace {

// Bit pattern of a host float. Used only for values that are exactly representable (powers of two,
// small integers, quarters), where the host and the EE agree by construction.
std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// Runs the function with guard words around the 4-word output so an out-of-bounds store is caught.
struct Run {
    std::uint32_t uv[4];
    bool flag;
};

Run run(std::uint32_t flags, std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
        std::uint32_t tex_w, std::uint32_t tex_h) {
    std::uint32_t buffer[6] = {0xDEADBEEFu, 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u, 0xDEADBEEFu};
    const bool flag = build_texture_uv_rect(buffer + 1, flags, x, y, w, h, tex_w, tex_h);
    CHECK_EQ(buffer[0], 0xDEADBEEFu);   // no store below the rectangle
    CHECK_EQ(buffer[5], 0xDEADBEEFu);   // no store above the rectangle
    return Run{{buffer[1], buffer[2], buffer[3], buffer[4]}, flag};
}

void expect(const Run &r, std::uint32_t u0, std::uint32_t v0, std::uint32_t u1, std::uint32_t v1, bool flag) {
    CHECK_EQ(r.uv[0], u0);
    CHECK_EQ(r.uv[1], v0);
    CHECK_EQ(r.uv[2], u1);
    CHECK_EQ(r.uv[3], v1);
    CHECK_EQ(r.flag, flag);
}

const std::uint32_t kPosZero = 0x00000000u;
const std::uint32_t kNegZero = 0x80000000u;
const std::uint32_t kPosMax  = 0x7FFFFFFFu;   // the EE's largest finite value, used for div by zero
const std::uint32_t kNegMax  = 0xFFFFFFFFu;

void test_plain_rectangle() {
    // 64x32 texture (the sizes the reader saw at the start-up call site 0x004D7F08), rectangle
    // x=16, y=8, w=32, h=16. All quotients are exact quarters:
    //   u_left = 16/64 = 0.25, u_right = 48/64 = 0.75, v_top = 8/32 = 0.25, v_bottom = 24/32 = 0.75.
    // Layout: +0 u_left, +4 v_top, +8 u_right, +12 v_bottom. No zero divisor, so no flag.
    const Run r = run(0, bits(16.0f), bits(8.0f), bits(32.0f), bits(16.0f), bits(64.0f), bits(32.0f));
    expect(r, bits(0.25f), bits(0.25f), bits(0.75f), bits(0.75f), false);
}

void test_whole_texture() {
    // x=y=0, w=tex_w, h=tex_h: 0/n = +0 (zero dividend, positive sign), n/n = 1.0 exactly.
    const Run r = run(0, kPosZero, kPosZero, bits(128.0f), bits(256.0f), bits(128.0f), bits(256.0f));
    expect(r, kPosZero, kPosZero, bits(1.0f), bits(1.0f), false);
}

void test_flip_flags() {
    const std::uint32_t x = bits(16.0f), y = bits(8.0f), w = bits(32.0f), h = bits(16.0f);
    const std::uint32_t tw = bits(64.0f), th = bits(32.0f);
    const std::uint32_t q1 = bits(0.25f), q3 = bits(0.75f);
    // bit 0: U pair exchanged, V pair untouched.
    expect(run(1, x, y, w, h, tw, th), q3, q1, q1, q3, false);
    // bit 1: V pair exchanged, U pair untouched.
    expect(run(2, x, y, w, h, tw, th), q1, q3, q3, q1, false);
    // both bits: both pairs exchanged.
    expect(run(3, x, y, w, h, tw, th), q3, q3, q1, q1, false);
    // Any other bit is ignored: 4, the top bit, and everything except bits 0 and 1.
    expect(run(4, x, y, w, h, tw, th), q1, q1, q3, q3, false);
    expect(run(0x80000000u, x, y, w, h, tw, th), q1, q1, q3, q3, false);
    expect(run(0xFFFFFFFCu, x, y, w, h, tw, th), q1, q1, q3, q3, false);
    // Other bits set together with bit 0 still give a horizontal mirror only.
    expect(run(0xFFFFFFFDu, x, y, w, h, tw, th), q3, q1, q1, q3, false);
}

void test_flip_after_divide_is_pure_exchange() {
    // The exchange happens after the division, so the mirrored output is exactly the unmirrored
    // output with the two values swapped, even for inexact quotients (1/3 and 2/3 here).
    const Run plain = run(0, bits(1.0f), bits(1.0f), bits(1.0f), bits(1.0f), bits(3.0f), bits(3.0f));
    const Run both  = run(3, bits(1.0f), bits(1.0f), bits(1.0f), bits(1.0f), bits(3.0f), bits(3.0f));
    CHECK_EQ(both.uv[0], plain.uv[2]);
    CHECK_EQ(both.uv[2], plain.uv[0]);
    CHECK_EQ(both.uv[1], plain.uv[3]);
    CHECK_EQ(both.uv[3], plain.uv[1]);
}

void test_division_truncates() {
    // 1/3: the exact binary expansion is 0.010101... ; in double, 1.0/3.0 = 0x3FD5555555555555.
    // from_double keeps the top 23 fraction bits (0x5555555555555 >> 29 = 0x2AAAAA, remainder
    // discarded, i.e. truncated toward zero) and exponent 0x3FD - 1023 + 127 = 125 = 0x7D, so the
    // float is 0 | 0x7D | 0x2AAAAA = 0x3EAAAAAA. IEEE round-to-nearest would give 0x3EAAAAAB, so this
    // case tells the EE model apart from host arithmetic.
    // 2/3 = 0x3FE5555555555555 in double -> exponent 126, fraction 0x2AAAAA -> 0x3F2AAAAA.
    const Run r = run(0, bits(1.0f), bits(1.0f), bits(1.0f), bits(1.0f), bits(3.0f), bits(3.0f));
    expect(r, 0x3EAAAAAAu, 0x3EAAAAAAu, 0x3F2AAAAAu, 0x3F2AAAAAu, false);
}

void test_addition_truncates() {
    // x = 1.0, w = 3 * 2^-24 = 1.5 * 2^-23 (= 0x34400000: exponent -23 -> biased 104 = 0x68, so
    // 0x68 << 23 = 0x34000000, plus fraction 1.5 -> 0x400000).
    // The exact sum 1 + 1.5 * 2^-23 lies between the floats 1 + 2^-23 and 1 + 2^-22; IEEE round-to-
    // nearest picks 1 + 2^-22 (0x3F800002), the EE's truncating add picks 1 + 2^-23 (0x3F800001).
    // Divided by tex_w = 1.0 the value passes through unchanged (the double quotient is exact).
    const Run r = run(0, bits(1.0f), kPosZero, 0x34400000u, kPosZero, bits(1.0f), bits(1.0f));
    expect(r, bits(1.0f), kPosZero, 0x3F800001u, kPosZero, false);
    // y = 1.0, h = 3 * 2^-25 (0x33C00000, exponent 0x67): the exact sum 1 + 0.75 * 2^-23 rounds up
    // to 1 + 2^-23 under IEEE but truncates to exactly 1.0 on the EE.
    const Run s = run(0, kPosZero, bits(1.0f), kPosZero, 0x33C00000u, bits(1.0f), bits(1.0f));
    expect(s, kPosZero, bits(1.0f), kPosZero, bits(1.0f), false);
}

void test_zero_extent() {
    // w = h = 0: x + 0 is exact, so u_right == u_left and v_bottom == v_top bit for bit.
    const Run r = run(0, bits(5.0f), bits(7.0f), kPosZero, kPosZero, bits(64.0f), bits(32.0f));
    CHECK_EQ(r.uv[0], r.uv[2]);
    CHECK_EQ(r.uv[1], r.uv[3]);
    // 5/64 = 0.078125 and 7/32 = 0.21875 are exact.
    expect(r, bits(0.078125f), bits(0.21875f), bits(0.078125f), bits(0.21875f), false);
}

void test_negative_values() {
    // x = -16, w = 32 on a 64-wide texture: u_left = -0.25, x + w = +16 -> u_right = +0.25.
    // y = -8, h = 8 on a 32-high texture: v_top = -0.25; y + h = 0 exactly. For a difference whose
    // mantissa cancels completely the add returns +0 (0x00000000), and 0 / 32 is +0.
    const Run r = run(0, bits(-16.0f), bits(-8.0f), bits(32.0f), bits(8.0f), bits(64.0f), bits(32.0f));
    expect(r, bits(-0.25f), bits(-0.25f), bits(0.25f), kPosZero, false);
    // Negative texture size: 16 / -64 = -0.25; sign is the xor of the operand signs.
    const Run n = run(0, bits(16.0f), bits(16.0f), bits(16.0f), bits(16.0f), bits(-64.0f), bits(-64.0f));
    expect(n, bits(-0.25f), bits(-0.25f), bits(-0.5f), bits(-0.5f), false);
}

void test_fractional_values() {
    // x = 0.5, w = 0.25 on a 2.0-wide texture: u_left = 0.25, u_right = 0.75 / 2 = 0.375 (exact).
    // y = 0.125, h = 0.125 on a 0.5-high texture: v_top = 0.25, v_bottom = 0.25 / 0.5 = 0.5.
    const Run r = run(0, bits(0.5f), bits(0.125f), bits(0.25f), bits(0.125f), bits(2.0f), bits(0.5f));
    expect(r, bits(0.25f), bits(0.25f), bits(0.375f), bits(0.5f), false);
}

void test_zero_dividend_signs() {
    // A zero dividend gives a zero whose sign is the xor of the two signs:
    //   +0 / +64 = +0, -0 / +64 = -0, +0 / -64 = -0.
    // -0 + 0 (w = +0): both flush to zero and the add returns the AND of the two patterns, +0; so
    // u_right for x = -0 is +0 / 64 = +0 while u_left is -0.
    const Run a = run(0, kNegZero, kPosZero, kPosZero, kPosZero, bits(64.0f), bits(-64.0f));
    expect(a, kNegZero, kNegZero, kPosZero, kNegZero, false);
    // -0 + -0 stays -0 (AND of two -0 patterns), so with w = -0 both U values are -0.
    const Run b = run(0, kNegZero, kPosZero, kNegZero, kPosZero, bits(64.0f), bits(64.0f));
    expect(b, kNegZero, kPosZero, kNegZero, kPosZero, false);
}

void test_divide_by_zero_width() {
    // tex_w = 0: both U divisions hit a zero divisor and give sign | 0x7FFFFFFF; the flag is raised.
    // x = 16 (positive): u_left = 0x7FFFFFFF; x + w = 48: u_right = 0x7FFFFFFF.
    // The V pair is unaffected: 8/32 = 0.25, 24/32 = 0.75.
    const Run r = run(0, bits(16.0f), bits(8.0f), bits(32.0f), bits(16.0f), kPosZero, bits(32.0f));
    expect(r, kPosMax, bits(0.25f), kPosMax, bits(0.75f), true);
    // A negative dividend over zero takes the negative sign: -16 / 0 = 0xFFFFFFFF; -16 + 32 = 16 -> +max.
    const Run n = run(0, bits(-16.0f), bits(8.0f), bits(32.0f), bits(16.0f), kPosZero, bits(32.0f));
    expect(n, kNegMax, bits(0.25f), kPosMax, bits(0.75f), true);
    // A negative zero divisor flips the sign: 16 / -0 = 0xFFFFFFFF.
    const Run z = run(0, bits(16.0f), bits(8.0f), bits(32.0f), bits(16.0f), kNegZero, bits(32.0f));
    expect(z, kNegMax, bits(0.25f), kNegMax, bits(0.75f), true);
}

void test_divide_by_zero_height_only() {
    // tex_h = 0 alone: only the V pair becomes +max, the U pair is normal, and the flag is raised.
    const Run r = run(0, bits(16.0f), bits(8.0f), bits(32.0f), bits(16.0f), bits(64.0f), kPosZero);
    expect(r, bits(0.25f), kPosMax, bits(0.75f), kPosMax, true);
    // With a vertical flip the two +max values are exchanged (identical), and the flag stays raised.
    const Run f = run(2, bits(16.0f), bits(8.0f), bits(32.0f), bits(16.0f), bits(64.0f), kPosZero);
    expect(f, bits(0.25f), kPosMax, bits(0.75f), kPosMax, true);
}

void test_zero_over_zero() {
    // 0 / 0: the divisor test comes first, so the result is +max (not a zero, not a NaN) and the
    // flag is raised. -0 / 0 is -max.
    const Run r = run(0, kPosZero, kNegZero, kPosZero, kPosZero, kPosZero, kPosZero);
    // x = +0: u_left = +0/0 = +max; x + w = +0 -> u_right = +max.
    // y = -0: v_top = -0/0 = -max; y + h = -0 + +0 = (AND) +0 -> v_bottom = +max.
    expect(r, kPosMax, kNegMax, kPosMax, kPosMax, true);
}

void test_denormal_inputs() {
    // Denormal dividends flush to a signed zero before the division:
    //   x = 0x00000001 (smallest positive denormal) -> +0 / 64 = +0
    //   y = 0x80000001 (smallest negative denormal) -> -0 / 32 = -0
    // Denormal extents flush too: x + w = +0 + +0 = +0 -> +0; y + h = -0 + -0 = -0 -> -0 / 32 = -0.
    const Run r = run(0, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu, bits(64.0f), bits(32.0f));
    expect(r, kPosZero, kNegZero, kPosZero, kNegZero, false);
    // A denormal divisor counts as zero: 0x00400000 as tex_w gives +max for positive dividends and
    // raises the flag. tex_h = 0x80000001 (negative denormal) gives -max for positive dividends.
    const Run d = run(0, bits(16.0f), bits(8.0f), bits(32.0f), bits(16.0f), 0x00400000u, 0x80000001u);
    expect(d, kPosMax, kNegMax, kPosMax, kNegMax, true);
}

void test_smallest_normal_and_underflow() {
    // x = 2^-126 (0x00800000, the smallest normal) over tex_w = 1.0 passes through: exponent 1 stays 1.
    // The same value over tex_w = 2.0 would be 2^-127: biased exponent 0, which pack turns into a
    // signed zero (underflow flushes, no denormal result). Negative: -2^-126 / 2 = -0.
    const Run r = run(0, 0x00800000u, 0x80800000u, kPosZero, kPosZero, bits(1.0f), bits(2.0f));
    expect(r, 0x00800000u, kNegZero, 0x00800000u, kNegZero, false);
}

void test_large_values_and_overflow() {
    // Biased exponent 255 is an ordinary value in the EE model (pack clamps only above 255), so the
    // largest representable magnitudes are the exponent-255 patterns, up to 0x7FFFFFFF.
    // x = w = 2^127 (0x7F000000): the sum 2^128 has biased exponent 255 and is stored as 0x7F800000
    // (the IEEE infinity pattern, but just a number here). Over tex_w = 1.0 both pass unchanged.
    // y = h = 2^128 (0x7F800000): the sum 2^129 has biased exponent 256 and clamps to +0x7FFFFFFF;
    // over tex_h = 1.0 that pattern converts to double and back with exponent 255, so it survives.
    const Run r = run(0, 0x7F000000u, 0x7F800000u, 0x7F000000u, 0x7F800000u, bits(1.0f), bits(1.0f));
    expect(r, 0x7F000000u, 0x7F800000u, 0x7F800000u, 0x7FFFFFFFu, false);
    // Quotient overflow: 2^127 / 0.25 = 2^129 -> biased exponent 256 -> clamped to +0x7FFFFFFF
    // (w = 0, so u_right is the same). -2^127 / 0.5 = -2^128 is exponent 255 and is not clamped:
    // 0xFF800000.
    const Run q = run(0, 0x7F000000u, 0xFF000000u, kPosZero, kPosZero, bits(0.25f), bits(0.5f));
    expect(q, 0x7FFFFFFFu, 0xFF800000u, 0x7FFFFFFFu, 0xFF800000u, false);
}

void test_nan_and_infinity_patterns() {
    // The model has no NaN or infinity. The quiet-NaN pattern 0x7FC00000 is exponent 255 with
    // fraction 0x400000, i.e. "1.5 * 2^128" as a number.
    //   0x7FC00000 / 1.0: double exponent 1151, back to float exponent 255 -> 0x7FC00000 unchanged.
    //   0x7FC00000 / 2.0: exponent drops to 254 -> 0x7F400000.
    //   0x7FC00000 + 1.0 (x + w with w = 1.0): the exponent gap is 128 > 25 so the small operand
    //   contributes nothing and the sum is 0x7FC00000; over 1.0 it stays 0x7FC00000.
    const Run n = run(0, 0x7FC00000u, 0x7FC00000u, bits(1.0f), kPosZero, bits(1.0f), bits(2.0f));
    expect(n, 0x7FC00000u, 0x7F400000u, 0x7FC00000u, 0x7F400000u, false);
    // The +infinity pattern 0x7F800000 is "2^128": over 0.5 it overflows to +0x7FFFFFFF, and over
    // 1.0 it is unchanged. -infinity 0xFF800000 over 1.0 is unchanged too.
    const Run i = run(0, 0x7F800000u, 0xFF800000u, kPosZero, kPosZero, bits(0.5f), bits(1.0f));
    expect(i, 0x7FFFFFFFu, 0xFF800000u, 0x7FFFFFFFu, 0xFF800000u, false);
}

void test_output_layout_is_interleaved() {
    // Distinct exact quotients in all four slots prove the order u_left, v_top, u_right, v_bottom
    // rather than u_left, u_right, v_top, v_bottom:
    //   x=1,y=2,w=1,h=2 on an 8x8 texture: u_left=0.125, v_top=0.25, u_right=0.25, v_bottom=0.5.
    const Run r = run(0, bits(1.0f), bits(2.0f), bits(1.0f), bits(2.0f), bits(8.0f), bits(8.0f));
    CHECK_EQ(r.uv[0], bits(0.125f));
    CHECK_EQ(r.uv[1], bits(0.25f));
    CHECK_EQ(r.uv[2], bits(0.25f));
    CHECK_EQ(r.uv[3], bits(0.5f));
    CHECK_EQ(kDivideByZeroFlags, 0x30000u);
}

}

int main() {
    test_plain_rectangle();
    test_whole_texture();
    test_flip_flags();
    test_flip_after_divide_is_pure_exchange();
    test_division_truncates();
    test_addition_truncates();
    test_zero_extent();
    test_negative_values();
    test_fractional_values();
    test_zero_dividend_signs();
    test_divide_by_zero_width();
    test_divide_by_zero_height_only();
    test_zero_over_zero();
    test_denormal_inputs();
    test_smallest_normal_and_underflow();
    test_large_values_and_overflow();
    test_nan_and_infinity_patterns();
    test_output_layout_is_interleaved();
    return check::done("test_build_texture_uv_rect");
}
