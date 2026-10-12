// ee::f32 against the fork's ps2::fp helpers: every operator must give the bits the recompiled code gives,
// and the EE edge cases (denormals flushed, results truncated, conversions saturated) must hold.

#include "check.hpp"
#include "ee/f32.hpp"

#include <bit>
#include <cstdint>

using ee::f32;
namespace fp = ps2::fp;

namespace {

constexpr std::uint32_t kEdges[] = {
    0x00000000u, 0x80000000u,                 // +0, -0
    0x00000001u, 0x007FFFFFu, 0x80000001u,    // denormals
    0x00800000u, 0x00800001u, 0x80800000u,    // smallest normals
    0x33FFFFFFu, 0x34000000u,                 // just under and exactly 2^-23
    0x3EAAAAAAu, 0x3EAAAAABu,                 // either side of one third
    0x3F000000u, 0x3F7FFFFFu, 0x3F800000u, 0x3F800001u, 0xBF800000u,
    0x3FC00000u, 0x40400000u, 0x3DCCCCCDu, 0x3F8CCCCDu,
    0x4B800000u, 0x4B800001u,                 // 2^24 and the next float
    0x4F000000u, 0xCF000000u, 0x4F32D05Eu,    // 2^31, -2^31, 3e9
    0x7F7FFFFFu, 0x7F800000u, 0x7FFFFFFFu, 0xFF800000u, 0xFFFFFFFFu,   // the top of the range (the EE has no inf or NaN)
};

float host(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t bits_of(float value) { return std::bit_cast<std::uint32_t>(value); }

void operators_match_the_runtime() {
    for (std::uint32_t a : kEdges) {
        const f32 x = f32::from_bits(a);
        CHECK_EQ((-x).bits, a ^ fp::kSign);
        CHECK_EQ(abs(x).bits, a & fp::kAbs);
        CHECK_EQ(sqrt(x).bits, fp::sqrt(a));
        CHECK_EQ(static_cast<std::uint32_t>(x.to_s32()), fp::cvt_w_s(a));
        CHECK_EQ(f32::from_s32(static_cast<std::int32_t>(a)).bits, fp::cvt_s_w(a));
        for (std::uint32_t b : kEdges) {
            const f32 y = f32::from_bits(b);
            CHECK_EQ((x + y).bits, fp::add(a, b));
            CHECK_EQ((x - y).bits, fp::sub(a, b));
            CHECK_EQ((x * y).bits, fp::mul(a, b));
            CHECK_EQ((x / y).bits, fp::div(a, b).value);
            CHECK_EQ(rsqrt(x, y).bits, fp::rsqrt(a, b).value);
            CHECK_EQ(max(x, y).bits, fp::max(a, b));
            CHECK_EQ(min(x, y).bits, fp::min(a, b));
            CHECK_EQ(x == y, fp::key(a) == fp::key(b));
            CHECK_EQ(x != y, fp::key(a) != fp::key(b));
            CHECK_EQ(x < y, fp::key(a) < fp::key(b));
            CHECK_EQ(x <= y, fp::key(a) <= fp::key(b));
            CHECK_EQ(x > y, fp::key(b) < fp::key(a));
            CHECK_EQ(x >= y, fp::key(b) <= fp::key(a));
            f32 z = x;
            z += y;
            CHECK_EQ(z.bits, fp::add(a, b));
            z = x;
            z -= y;
            CHECK_EQ(z.bits, fp::sub(a, b));
            z = x;
            z *= y;
            CHECK_EQ(z.bits, fp::mul(a, b));
            for (std::uint32_t acc : {0x00000000u, 0x3F800000u, 0xBF7FFFFFu, 0x7F7FFFFFu}) {
                CHECK_EQ(madd(f32::from_bits(acc), x, y).bits, fp::add(acc, fp::mul(a, b)));
                CHECK_EQ(msub(f32::from_bits(acc), x, y).bits, fp::sub(acc, fp::mul(a, b)));
            }
        }
    }
}

void literals_keep_their_bits() {
    CHECK_EQ(f32::lit(1.0f).bits, 0x3F800000u);
    CHECK_EQ(f32::lit(-0.0f).bits, 0x80000000u);
    CHECK_EQ(f32::lit(17.8816f).bits, bits_of(17.8816f));
    CHECK_EQ(f32{}.bits, 0u);
    constexpr f32 half = f32::lit(0.5f);          // usable in constant expressions
    static_assert(half.bits == 0x3F000000u);
    CHECK_EQ(bits_of(f32::lit(1.5f).host()), 0x3FC00000u);
}

void denormals_are_flushed_to_zero() {
    const f32 tiny = f32::from_bits(0x00000001u);
    CHECK_EQ((tiny + tiny).bits, 0u);                                               // a host float gives 0x00000002
    CHECK_EQ(bits_of(host(0x00000001u) + host(0x00000001u)), 0x00000002u);
    CHECK_EQ((f32::from_bits(0x00800000u) * f32::lit(0.5f)).bits, 0u);              // result below the smallest normal
    CHECK_EQ((f32::from_bits(0x80800000u) * f32::lit(0.5f)).bits, 0x80000000u);     // and it keeps its sign
    CHECK_EQ((f32::from_bits(0x00800001u) - f32::from_bits(0x00800000u)).bits, 0u); // a host float gives 0x00000001
    CHECK_EQ((f32::from_bits(0x007FFFFFu) + f32::lit(1.0f)).bits, 0x3F800000u);     // a denormal operand counts as zero
    CHECK_EQ((f32::from_bits(0x007FFFFFu) * f32::lit(4.0f)).bits, 0u);              // a host float gives a normal number
    CHECK(f32::from_bits(0x007FFFFFu) == f32{});                                    // compares as zero too
    CHECK(f32::lit(-0.0f) == f32::lit(0.0f));
    CHECK_EQ(f32::from_bits(0x007FFFFFu).to_s32(), 0);
}

void results_are_truncated_not_rounded() {
    // 1 + (just under 2^-23): the exact sum is above the halfway point, so a host float rounds up.
    const std::uint32_t one = 0x3F800000u, sliver = 0x33FFFFFFu;
    CHECK_EQ((f32::from_bits(one) + f32::from_bits(sliver)).bits, 0x3F800000u);
    CHECK_EQ(bits_of(host(one) + host(sliver)), 0x3F800001u);
    // 1 / 3: the host rounds the last bit up, the EE drops it.
    CHECK_EQ((f32::lit(1.0f) / f32::lit(3.0f)).bits, 0x3EAAAAAAu);
    CHECK_EQ(bits_of(host(one) / 3.0f), 0x3EAAAAABu);
    // (1 + 2^-23) * 1.5: the exact product ends in a half, which the host rounds up to even.
    const std::uint32_t a = 0x3F800001u, b = 0x3FC00000u;
    CHECK_EQ((f32::from_bits(a) * f32::from_bits(b)).bits, 0x3FC00001u);
    CHECK_EQ(bits_of(host(a) * host(b)), 0x3FC00002u);
    // multiply-add is two truncations, never fused
    const f32 acc = f32::lit(-1.0f), x = f32::from_bits(0x3F800001u);
    CHECK_EQ(madd(acc, x, x).bits, fp::add(acc.bits, fp::mul(x.bits, x.bits)));
    CHECK_EQ(madd(acc, x, x).bits, 0x34800000u);   // 2^-22: the 2^-46 term of the exact product is gone
}

void float_to_int_truncates_and_saturates() {
    CHECK_EQ(f32::lit(1.9f).to_s32(), 1);
    CHECK_EQ(f32::lit(-1.9f).to_s32(), -1);
    CHECK_EQ(f32::lit(0.99f).to_s32(), 0);
    CHECK_EQ(f32::lit(-0.5f).to_s32(), 0);
    CHECK_EQ(f32::lit(16777216.0f).to_s32(), 16777216);
    CHECK_EQ(f32::lit(-2147483648.0f).to_s32(), INT32_MIN);
    CHECK_EQ(f32::lit(2147483648.0f).to_s32(), INT32_MAX);        // one past the largest int: clamps
    CHECK_EQ(f32::lit(3.0e9f).to_s32(), INT32_MAX);
    CHECK_EQ(f32::lit(-3.0e9f).to_s32(), INT32_MIN);
    CHECK_EQ(f32::from_bits(0x7FFFFFFFu).to_s32(), INT32_MAX);    // what a host would call NaN
    CHECK_EQ(f32::from_bits(0xFFFFFFFFu).to_s32(), INT32_MIN);
    CHECK_EQ(f32::from_bits(0x7F800000u).to_s32(), INT32_MAX);    // what a host would call infinity
    // The order matters at the edge: negating after the conversion is not the conversion of the negation.
    CHECK_EQ(0u - static_cast<std::uint32_t>(f32::lit(-3.0e9f).to_s32()), 0x80000000u);
    CHECK_EQ(static_cast<std::uint32_t>((-f32::lit(-3.0e9f)).to_s32()), 0x7FFFFFFFu);
}

void int_to_float_truncates() {
    CHECK_EQ(f32::from_s32(0).bits, 0u);
    CHECK_EQ(f32::from_s32(1).bits, 0x3F800000u);
    CHECK_EQ(f32::from_s32(-1).bits, 0xBF800000u);
    CHECK_EQ(f32::from_s32(16777217).bits, 0x4B800000u);
    CHECK_EQ(f32::from_s32(16777219).bits, 0x4B800001u);          // a host conversion rounds to 0x4B800002
    CHECK_EQ(bits_of(static_cast<float>(16777219)), 0x4B800002u);
    CHECK_EQ(f32::from_s32(INT32_MAX).bits, 0x4EFFFFFFu);         // a host conversion rounds up to 2^31
    CHECK_EQ(f32::from_s32(INT32_MIN).bits, 0xCF000000u);
}

void division_by_zero_gives_the_largest_value_and_raises_the_flags() {
    CHECK_EQ((f32::lit(1.0f) / f32{}).bits, 0x7FFFFFFFu);
    CHECK_EQ((f32::lit(-1.0f) / f32{}).bits, 0xFFFFFFFFu);
    CHECK_EQ((f32::lit(1.0f) / f32::lit(-0.0f)).bits, 0xFFFFFFFFu);
    CHECK_EQ((f32{} / f32{}).bits, 0x7FFFFFFFu);
    CHECK_EQ((f32{} / f32::lit(2.0f)).bits, 0u);
    CHECK_EQ((f32::lit(1.0f) / f32::from_bits(0x007FFFFFu)).bits, 0x7FFFFFFFu);   // a denormal divisor is zero

    ps2::Cpu c{};
    c.fcr31 = 0x01000001u;
    CHECK_EQ(div(c, f32::lit(6.0f), f32::lit(3.0f)).bits, 0x40000000u);
    CHECK_EQ(c.fcr31, 0x01000001u);                                               // a valid division leaves fcr31 alone
    CHECK_EQ(div(c, f32::lit(6.0f), f32{}).bits, 0x7FFFFFFFu);
    CHECK_EQ(c.fcr31, 0x01030001u);
    c.fcr31 = 0;
    CHECK_EQ(rsqrt(c, f32::lit(1.0f), f32::lit(4.0f)).bits, 0x3F000000u);
    CHECK_EQ(c.fcr31, 0u);
    CHECK_EQ(rsqrt(c, f32::lit(-1.0f), f32{}).bits, 0xFFFFFFFFu);
    CHECK_EQ(c.fcr31, 0x00030000u);
}

void square_roots_ignore_the_sign() {
    CHECK_EQ(sqrt(f32::lit(4.0f)).bits, 0x40000000u);
    CHECK_EQ(sqrt(f32::lit(-4.0f)).bits, 0x40000000u);
    CHECK_EQ(sqrt(f32::lit(-0.0f)).bits, 0u);
    CHECK_EQ(sqrt(f32::lit(2.0f)).bits, 0x3FB504F3u);             // truncated, as the host happens to round here too
    CHECK_EQ(rsqrt(f32::lit(3.0f), f32::lit(-4.0f)).bits, 0x3FC00000u);
}

void comparisons_order_by_sign_and_magnitude() {
    CHECK(f32::lit(-2.0f) < f32::lit(-1.0f));
    CHECK(f32::lit(-1.0f) < f32{});
    CHECK(f32::from_bits(0x7F7FFFFFu) < f32::from_bits(0x7FFFFFFFu));   // no NaN: the top patterns still order
    CHECK(f32::from_bits(0xFFFFFFFFu) < f32::from_bits(0xFF7FFFFFu));
    CHECK(!(f32::lit(1.0f) < f32::lit(1.0f)));
    CHECK(f32::lit(1.0f) <= f32::lit(1.0f));
    CHECK_EQ(max(f32::lit(-0.0f), f32{}).bits, 0x80000000u);            // equal keys: the first operand wins
    CHECK_EQ(min(f32{}, f32::lit(-0.0f)).bits, 0u);
    CHECK_EQ(max(f32::lit(1.0f), f32::lit(2.0f)).bits, 0x40000000u);
    CHECK_EQ(min(f32::lit(1.0f), f32::lit(2.0f)).bits, 0x3F800000u);
}

}

int main() {
    operators_match_the_runtime();
    literals_keep_their_bits();
    denormals_are_flushed_to_zero();
    results_are_truncated_not_rounded();
    float_to_int_truncates_and_saturates();
    int_to_float_truncates();
    division_by_zero_gives_the_largest_value_and_raises_the_flags();
    square_roots_ignore_the_sign();
    comparisons_order_by_sign_and_magnitude();
    return check::done("test_f32");
}
