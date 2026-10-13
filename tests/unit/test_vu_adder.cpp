// The fork's fast VU adder (vuops::add, sub and their four-lane forms in ps2/vu_ops.hpp, used by the
// recompiled VU programs and the fast interpreter) against the exact one (ps2::fp::add, sub): a VU adder
// truncates after keeping one guard bit of the smaller operand (gap 013).
// The WebAssembly JIT emits the same steps and is not covered here.
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
    return check::done("test_vu_adder");
}
#endif
