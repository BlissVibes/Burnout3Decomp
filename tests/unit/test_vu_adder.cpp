// The fork's fast VU adder (vuops::add, sub and their four-lane forms in ps2/vu_ops.hpp, used by the
// recompiled VU programs and the fast interpreter) against the exact one (ps2::fp::add, sub): a VU adder
// truncates after keeping one guard bit of the smaller operand (gap 013).
// The WebAssembly JIT has its own form of the adder; tests/runtime/test_vu1_jit.cpp compares it with these
// helpers.
// ps2/vu_ops.hpp is written with clang's vector builtins, as the fork is built; with another compiler this
// test reports that it was skipped.

#include "check.hpp"

#ifndef __clang__
int main() {
    std::puts("test_vu_adder: skipped (ps2/vu_ops.hpp needs clang)");
    return 0;
}
#else
#include "ps2/fpu.hpp"
#include "ps2/vu_ops.hpp"

#include <bit>
#include <cstdint>

namespace fp = ps2::fp;
namespace vuops = ps2::vuops;

namespace {

std::uint32_t bits_of(float value) { return std::bit_cast<std::uint32_t>(value); }

// +0 and -0 are one value here: the two adders are compared on what a program can tell apart by arithmetic.
std::uint32_t same_zero(std::uint32_t x) { return (x & fp::kAbs) ? x : 0u; }

std::uint32_t g_seed = 0x2545F491u;
std::uint32_t next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

// A float with a random sign and mantissa and an exponent near the middle of the range, so that no sum or
// difference of two of them is a denormal or overflows (the two adders differ there on purpose).
std::uint32_t sample(std::uint32_t spread) {
    const std::uint32_t r = next();
    const std::uint32_t exponent = 127u - spread / 2u + (next() % (spread + 1u));
    return (r & 0x807FFFFFu) | (exponent << 23);
}

void dropping_a_fraction_gives_the_integer_below() {
    const std::uint32_t big = bits_of(8388608.0f);
    const float values[] = {0.5f, 0.75f, 1.5f, 2.5f, 2.75f, 3.5f, 100.9f, 255.99f, 4194303.5f};
    for (float value : values) {
        const std::uint32_t up = vuops::add(bits_of(value), big);
        const std::uint32_t back = vuops::sub(up, big);
        CHECK_EQ(back, bits_of(static_cast<float>(static_cast<std::int32_t>(value))));
        CHECK_EQ(up, fp::add(bits_of(value), big));
    }
}

void sums_match_the_exact_adder() {
    for (std::uint32_t spread : {2u, 8u, 24u, 30u, 60u}) {
        for (int i = 0; i < 20000; ++i) {
            const std::uint32_t a = sample(spread), b = sample(spread);
            CHECK_EQ(same_zero(vuops::add(a, b)), same_zero(fp::add(a, b)));
            CHECK_EQ(same_zero(vuops::sub(a, b)), same_zero(fp::sub(a, b)));
        }
    }
}

void the_four_lane_forms_match_the_exact_adder() {
    for (std::uint32_t spread : {2u, 24u, 60u}) {
        for (int i = 0; i < 5000; ++i) {
            vuops::v4u a, b;
            for (int lane = 0; lane < 4; ++lane) {
                a[lane] = sample(spread);
                b[lane] = sample(spread);
            }
            const vuops::v4u sum = vuops::vadd(a, b), difference = vuops::vsub(a, b);
            for (int lane = 0; lane < 4; ++lane) {
                CHECK_EQ(same_zero(sum[lane]), same_zero(fp::add(a[lane], b[lane])));
                CHECK_EQ(same_zero(difference[lane]), same_zero(fp::sub(a[lane], b[lane])));
            }
        }
    }
}

// The fast paths' range: an exponent field of 255 is the largest host float, in an operand and in a result
// (fp::add saturates to 0x7FFFFFFF where the host sum overflows), and a denormal is a zero.
std::uint32_t in_range(std::uint32_t x) {
    if ((x & 0x7F800000u) == 0x7F800000u) return (x & fp::kSign) | 0x7F7FFFFFu;
    return fp::flush(x);
}
std::uint32_t exact_sum(std::uint32_t a, std::uint32_t b) { return in_range(fp::add(in_range(a), in_range(b))); }

// Operands from every class, the top of the range among them: any bits, exponents near the top, the largest
// magnitudes, denormals and zeros, and a second operand a few powers of two under the first.
std::uint32_t any_class(std::uint32_t near) {
    const std::uint32_t r = next();
    switch (next() % 8u) {
    case 0: return r;
    case 1: return (r & 0x807FFFFFu) | ((224u + next() % 32u) << 23);
    case 2: return (r & fp::kSign) | (next() % 2u ? 0x7F7FFFFFu : 0x7FFFFFFFu);
    case 3: return r & 0x807FFFFFu;
    case 4: return r & fp::kSign;
    case 5: {
        const std::uint32_t e = (near >> 23) & 0xFFu, down = next() % 30u;
        return (r & 0x807FFFFFu) | ((e > down ? e - down : 1u) << 23);
    }
    case 6: return (near ^ (r & fp::kSign)) + (next() % 5u) - 2u;
    default: return sample(60);
    }
}

void the_top_of_the_range_matches_the_exact_adder() {
    // The two-sum of the first form overflowed beside the largest magnitude and left the sum uncorrected.
    CHECK_EQ(vuops::add(0x73C00000u, 0xFF7FFFFFu), 0xFF7FFFFDu);
    CHECK_EQ(vuops::add(0xFE800003u, 0x7F7FFFFFu), exact_sum(0xFE800003u, 0x7F7FFFFFu));
    CHECK_EQ(vuops::add(0x7F7FFFFFu, 0x7F7FFFFFu), 0x7F7FFFFFu);
    for (int i = 0; i < 400000; ++i) {
        const std::uint32_t a = any_class(0), b = any_class(a);
        CHECK_EQ(vuops::add(a, b), exact_sum(a, b));
        CHECK_EQ(vuops::add(b, a), exact_sum(b, a));
        CHECK_EQ(vuops::sub(a, b), exact_sum(a, b ^ fp::kSign));
        // A product with 1.0 is the operand itself, so MADD and MSUB are the adder alone.
        CHECK_EQ(vuops::madd(a, b, fp::kOne), exact_sum(a, b));
        CHECK_EQ(vuops::msub(a, b, fp::kOne), exact_sum(a, b ^ fp::kSign));
    }
    for (int i = 0; i < 100000; ++i) {
        vuops::v4u a, b;
        for (int lane = 0; lane < 4; ++lane) {
            a[lane] = any_class(0);
            b[lane] = any_class(a[lane]);
        }
        const vuops::v4u one = vuops::vsplat(fp::kOne);
        const vuops::v4u sum = vuops::vadd(a, b), difference = vuops::vsub(a, b), madd = vuops::vmadd(a, b, one), msub = vuops::vmsub(a, b, one);
        for (int lane = 0; lane < 4; ++lane) {
            CHECK_EQ(sum[lane], exact_sum(a[lane], b[lane]));
            CHECK_EQ(difference[lane], exact_sum(a[lane], b[lane] ^ fp::kSign));
            CHECK_EQ(madd[lane], exact_sum(a[lane], b[lane]));
            CHECK_EQ(msub[lane], exact_sum(a[lane], b[lane] ^ fp::kSign));
        }
    }
}

void an_accumulated_product_is_added_with_the_same_adder() {
    // 3 * 0.25 is exact, so MADD and MSUB are the adder alone: 8388608 + 0.75 and 8388609 - 0.75.
    const std::uint32_t three = bits_of(3.0f), quarter = bits_of(0.25f);
    CHECK_EQ(vuops::madd(bits_of(8388608.0f), three, quarter), bits_of(8388608.0f));
    CHECK_EQ(vuops::msub(bits_of(8388609.0f), three, quarter), bits_of(8388608.0f));
    const vuops::v4u acc = vuops::vsplat(bits_of(8388608.0f)), a = vuops::vsplat(three), b = vuops::vsplat(quarter);
    const vuops::v4u sum = vuops::vmadd(acc, a, b), difference = vuops::vmsub(vuops::vsplat(bits_of(8388609.0f)), a, b);
    for (int lane = 0; lane < 4; ++lane) {
        CHECK_EQ(sum[lane], bits_of(8388608.0f));
        CHECK_EQ(difference[lane], bits_of(8388608.0f));
    }
}

}

int main() {
    dropping_a_fraction_gives_the_integer_below();
    sums_match_the_exact_adder();
    the_four_lane_forms_match_the_exact_adder();
    an_accumulated_product_is_added_with_the_same_adder();
    the_top_of_the_range_matches_the_exact_adder();
    return check::done("test_vu_adder");
}
#endif
