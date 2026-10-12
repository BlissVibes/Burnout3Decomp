// Standalone test for b3::physics::sap_compare_x (retail 0x001B9820).
//
// Expected values are derived from the specification, not from the game: the function returns
// -1 when key A is strictly below key B under EE single-precision ordering and +1 in every other
// case (greater or equal). EE ordering is: flush an exponent field of 0 to a signed zero, then
// order by sign and magnitude, so -0 == +0, denormals equal zero, and exponent 0xFF patterns
// (IEEE infinity and NaN) are ordinary huge finite values ordered by their mantissa bits.
// Each case below states which rule produces its expected value.

#include "hooks/physics/sap_compare_x.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using b3::physics::SapRecord;
using b3::physics::sap_compare_x;

namespace {

int failures = 0;

void expect(const char *what, std::uint32_t a, std::uint32_t b, std::int32_t want) {
    const std::int32_t got = sap_compare_x(a, b);
    if (got != want) {
        std::printf("FAIL %s: sap_compare_x(0x%08X, 0x%08X) = %d, expected %d\n", what, a, b, got, want);
        ++failures;
    }
}

// Both orders at once: a strictly below b gives (-1, +1); equal keys give (+1, +1).
void expect_less(const char *what, std::uint32_t lo, std::uint32_t hi) {
    expect(what, lo, hi, -1);
    expect(what, hi, lo, 1);
}
void expect_equal(const char *what, std::uint32_t a, std::uint32_t b) {
    expect(what, a, b, 1);
    expect(what, b, a, 1);
}

std::uint32_t bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

// Normal finite values: EE order equals IEEE order, so a host float compare of the chosen
// constants is the derivation.
void ordinary_values() {
    expect_less("1 < 2", bits(1.0f), bits(2.0f));
    expect_less("-1 < 1", bits(-1.0f), bits(1.0f));
    expect_less("-2 < -1", bits(-2.0f), bits(-1.0f));
    expect_less("0 < 1", bits(0.0f), bits(1.0f));
    expect_less("-1 < 0", bits(-1.0f), bits(0.0f));
    expect_less("0.5 < 0.75", bits(0.5f), bits(0.75f));
    expect_less("-0.75 < -0.5", bits(-0.75f), bits(-0.5f));
    expect_less("tiny normal < 1", 0x00800000u, bits(1.0f));           // smallest positive normal
    expect_less("-tiny normal < 0", 0x80800000u, bits(0.0f));
    expect_less("1 < largest finite", bits(1.0f), 0x7F7FFFFFu);
    expect_less("-largest finite < -1", 0xFF7FFFFFu, bits(-1.0f));
    expect_less("-largest < +largest", 0xFF7FFFFFu, 0x7F7FFFFFu);
    expect_less("adjacent ulp", 0x3F800000u, 0x3F800001u);              // 1.0 and the next float up
    expect_less("adjacent ulp negative", 0xBF800001u, 0xBF800000u);
}

// Equal keys: the spec has no zero return, so both orders give +1. Any record compared with
// itself also gives +1.
void ties_return_plus_one() {
    expect_equal("1 == 1", bits(1.0f), bits(1.0f));
    expect_equal("-3 == -3", bits(-3.0f), bits(-3.0f));
    expect_equal("0 == 0", 0u, 0u);
    expect_equal("largest == largest", 0x7F7FFFFFu, 0x7F7FFFFFu);
    expect("self compare", 0x42F6E979u, 0x42F6E979u, 1);
}

// Signed zero: both flush to magnitude 0, so they are equal (+1 both ways) and both sit strictly
// between the smallest negative and smallest positive normal.
void signed_zero() {
    expect_equal("-0 == +0", 0x80000000u, 0x00000000u);
    expect_less("-0 < tiny normal", 0x80000000u, 0x00800000u);
    expect_less("-tiny normal < +0", 0x80800000u, 0x00000000u);
    expect_less("-tiny normal < -0", 0x80800000u, 0x80000000u);
    expect_less("-0 < 1", 0x80000000u, bits(1.0f));
    expect_less("-1 < -0", bits(-1.0f), 0x80000000u);
}

// Denormals (exponent field 0, mantissa non-zero) flush to a signed zero before the compare, so
// every denormal equals every zero and every other denormal regardless of sign, and orders like
// zero against normals. Host IEEE would order them by magnitude, which is the trap this guards.
void denormals_flush_to_zero() {
    const std::uint32_t pos_min_denormal = 0x00000001u;
    const std::uint32_t pos_max_denormal = 0x007FFFFFu;
    const std::uint32_t neg_min_denormal = 0x80000001u;
    const std::uint32_t neg_max_denormal = 0x807FFFFFu;
    expect_equal("+denormal == +0", pos_min_denormal, 0u);
    expect_equal("+denormal == -0", pos_max_denormal, 0x80000000u);
    expect_equal("-denormal == +0", neg_min_denormal, 0u);
    expect_equal("-denormal == -0", neg_max_denormal, 0x80000000u);
    expect_equal("+denormal == -denormal", pos_max_denormal, neg_max_denormal);
    expect_equal("two denormals of different magnitude", pos_min_denormal, pos_max_denormal);
    expect_less("+denormal < smallest normal", pos_max_denormal, 0x00800000u);
    expect_less("-smallest normal < -denormal", 0x80800000u, neg_max_denormal);
    expect_less("-1 < -denormal", bits(-1.0f), neg_min_denormal);
    expect_less("+denormal < 1", pos_min_denormal, bits(1.0f));
}

// Exponent 0xFF patterns are finite on the EE and larger than every pattern with a smaller
// exponent; among themselves they order by mantissa bits (sign-magnitude), so the +inf pattern
// is below the +NaN patterns and a NaN with a bigger mantissa is bigger. Host IEEE would make
// every NaN compare false (+1 both ways), which this detects.
void exponent_ff_is_a_large_finite_value() {
    const std::uint32_t pos_inf = 0x7F800000u;
    const std::uint32_t neg_inf = 0xFF800000u;
    const std::uint32_t pos_qnan = 0x7FC00000u;
    const std::uint32_t neg_qnan = 0xFFC00000u;
    const std::uint32_t pos_snan = 0x7F800001u;
    const std::uint32_t pos_max = 0x7FFFFFFFu;   // largest magnitude pattern
    const std::uint32_t neg_max = 0xFFFFFFFFu;
    expect_less("largest finite < inf pattern", 0x7F7FFFFFu, pos_inf);
    expect_less("-inf pattern < -largest finite", neg_inf, 0xFF7FFFFFu);
    expect_less("inf pattern < snan pattern", pos_inf, pos_snan);
    expect_less("snan pattern < qnan pattern", pos_snan, pos_qnan);
    expect_less("qnan pattern < all-ones magnitude", pos_qnan, pos_max);
    expect_less("-all-ones magnitude < -qnan pattern", neg_max, neg_qnan);
    expect_less("-qnan pattern < -inf pattern", neg_qnan, neg_inf);
    expect_less("-qnan pattern < +qnan pattern", neg_qnan, pos_qnan);
    expect_less("1 < qnan pattern", bits(1.0f), pos_qnan);
    expect_less("-qnan pattern < 1", neg_qnan, bits(1.0f));
    expect_less("-all-ones < +all-ones", neg_max, pos_max);
    expect_equal("qnan pattern == itself", pos_qnan, pos_qnan);
    expect_equal("all-ones == itself", pos_max, pos_max);
    expect_less("0 < qnan pattern", 0u, pos_qnan);
    expect_less("-qnan pattern < -0", neg_qnan, 0x80000000u);
}

// Large and boundary magnitudes well inside the normal range.
void large_values() {
    expect_less("1e30 < 2e30", bits(1.0e30f), bits(2.0e30f));
    expect_less("-2e30 < -1e30", bits(-2.0e30f), bits(-1.0e30f));
    expect_less("1e-30 < 1e-29", bits(1.0e-30f), bits(1.0e-29f));
    expect_less("2^-126 < 2^-125", 0x00800000u, 0x01000000u);
    expect_less("2^127 < largest", 0x7F000000u, 0x7F7FFFFFu);
    expect_less("65535 < 65536", bits(65535.0f), bits(65536.0f));
}

// The exhaustive rule: for every pair of patterns from a mixed pool, the result must be -1
// exactly when the sign-magnitude key of the flushed A is strictly below that of the flushed
// B, and +1 otherwise. The key is recomputed here from the spec's words, independently of the
// fork header, so this cross-checks our use of ps2::fp::key.
std::int32_t spec_key(std::uint32_t x) {
    if (((x >> 23) & 0xFFu) == 0u) x &= 0x80000000u;             // flush denormal to signed zero
    const std::int32_t magnitude = static_cast<std::int32_t>(x & 0x7FFFFFFFu);
    return (x & 0x80000000u) ? -magnitude : magnitude;
}

void cross_check_against_spec_key() {
    const std::uint32_t pool[] = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu,
        0x00800000u, 0x80800000u, 0x3F800000u, 0xBF800000u, 0x3F800001u, 0xBF800001u,
        0x40000000u, 0xC0000000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u,
        0x7F800001u, 0xFF800001u, 0x7FC00000u, 0xFFC00000u, 0x7FFFFFFFu, 0xFFFFFFFFu,
        0x42F6E979u, 0xC2F6E979u, 0x3E4CCCCDu, 0x4B800000u, 0x0A000000u, 0x8A000000u,
    };
    for (std::uint32_t a : pool) {
        for (std::uint32_t b : pool) {
            const std::int32_t want = spec_key(a) < spec_key(b) ? -1 : 1;
            expect("pool pair", a, b, want);
        }
    }
}

// The record form reads only the key at offset 0; the trailing word must not influence the
// result, and the record must be 8 bytes as qsort is told.
void record_form_reads_only_the_key() {
    static_assert(sizeof(SapRecord) == 8, "qsort is called with an element size of 8");
    static_assert(offsetof(SapRecord, key) == 0, "the key is the first word of the record");
    const SapRecord lo{bits(1.0f), 0xFFFFFFFFu};
    const SapRecord hi{bits(2.0f), 0x00000000u};
    const SapRecord lo_other_payload{bits(1.0f), 0x12345678u};
    if (sap_compare_x(&lo, &hi) != -1) { std::puts("FAIL record form: lo vs hi should be -1"); ++failures; }
    if (sap_compare_x(&hi, &lo) != 1) { std::puts("FAIL record form: hi vs lo should be +1"); ++failures; }
    if (sap_compare_x(&lo, &lo_other_payload) != 1) { std::puts("FAIL record form: tie should be +1 regardless of payload"); ++failures; }
    if (sap_compare_x(&lo_other_payload, &lo) != 1) { std::puts("FAIL record form: tie should be +1 regardless of payload"); ++failures; }
    if (sap_compare_x(&lo, &lo) != 1) { std::puts("FAIL record form: self compare should be +1"); ++failures; }
}

// The result is a 32-bit integer; -1 widened to 64 bits (as the hooks table will do with set32
// semantics) must be all ones, not 0x00000000FFFFFFFF.
void result_sign_extends() {
    const std::int32_t r = sap_compare_x(bits(1.0f), bits(2.0f));
    const std::uint64_t widened = static_cast<std::uint64_t>(static_cast<std::int64_t>(r));
    if (widened != 0xFFFFFFFFFFFFFFFFull) { std::puts("FAIL: -1 does not sign-extend to all ones"); ++failures; }
    const std::int32_t p = sap_compare_x(bits(2.0f), bits(1.0f));
    if (static_cast<std::uint64_t>(static_cast<std::int64_t>(p)) != 1ull) { std::puts("FAIL: +1 does not widen to 1"); ++failures; }
}

}  // namespace

int main() {
    ordinary_values();
    ties_return_plus_one();
    signed_zero();
    denormals_flush_to_zero();
    exponent_ff_is_a_large_finite_value();
    large_values();
    cross_check_against_spec_key();
    record_form_reads_only_the_key();
    result_sign_extends();
    if (failures) {
        std::printf("test_sap_compare_x: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("test_sap_compare_x: all checks passed");
    return 0;
}
