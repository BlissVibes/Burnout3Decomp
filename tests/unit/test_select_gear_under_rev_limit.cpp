// Standalone test for b3::physics::select_gear_under_rev_limit (retail 0x001C6F30).
//
// Expected values are derived from the specification, never from running the game. The function
// forms r = dividend / divisor, limit = (pi/30 as the single 0x3DD67750) * block[10], then for
// i = 1 while i < block[33] computes value = block[8] * (r * block[1 + i]) and stops at the first
// value <= limit under EE ordering; it stores the last value to block[21] and returns i.
//
// Derivation rules used below (all from the spec's description of the EE float model):
//  * A multiply by a power of two only moves the exponent, so it is exact: (pi/30 bits) * 8192
//    = 0x3DD67750 with the exponent field raised by 13 = 0x44567750 (about 857.86 rad/s), the
//    limit used by most cases. A multiply by 1.0 is also exact.
//  * Products of small integers and halves (200 * 3 = 600, 4 * 600 = 2400) are exactly
//    representable, so truncation cannot bite and host float literals give the expected bits.
//  * Divides are exact for 100 / 0.5 = 200 and 1 / 1 = 1; 1 / 3 truncates the double quotient's
//    mantissa (0x555555...) to 0x3EAAAAAA, where host rounding would give 0x3EAAAAAB.
//  * Truncating multiply of 24-bit mantissas: (1+ulp)*(1+ulp) has mantissa product
//    0x800001*0x800001 = 0x400001000001, truncated to 0x800002 -> 0x3F800002; then
//    (3+ulp) * that = 0xC00001*0x800002 = 0x600002000002, truncated to 0xC00004 -> 0x40400004.
//    Regrouped as ((3+ulp)*(1+ulp))*(1+ulp) the truncations give 0x40400002 then 0x40400003, so
//    the grouping is observable.
//  * A zero exponent field (denormal or zero) in either multiply operand gives a signed zero;
//    an exponent sum over 255 saturates to sign|0x7FFFFFFF; e <= 0 gives a signed zero.
//  * x / (flushed) 0 gives sign|0x7FFFFFFF and the invalid flag; 0 / x gives a signed zero.
//  * Ordering is sign-magnitude on flushed patterns: -0 and +0 are equal, 0x7FFFFFFF is the
//    largest positive, 0xFFFFFFFF the most negative, and IEEE NaN patterns are ordinary huge
//    values (0x7FC00000 * 1.0 stays 0x7FC00000 and sits above every normal number).

#include "hooks/physics/select_gear_under_rev_limit.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using b3::physics::kEngineSpeedWord;
using b3::physics::kGearCountWord;
using b3::physics::kGearMultiplierWord;
using b3::physics::kGearRatioWordBase;
using b3::physics::kRevLimitWord;
using b3::physics::select_gear_under_rev_limit;

namespace {

int failures = 0;
int checks = 0;

void check_eq(const char *what, std::uint64_t got, std::uint64_t want) {
    ++checks;
    if (got != want) {
        std::printf("FAIL %s: got 0x%llX, expected 0x%llX\n", what, static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
        ++failures;
    }
}

std::uint32_t bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

constexpr int kWords = 48;              // enough to cover word 33 (the count) and aliasing reads
constexpr std::uint32_t kFill = 0xA5A5A5A5u;  // pattern for words the function must not touch
constexpr std::uint32_t kSentinel = 0xCAFEF00Du;  // the "inherited" value for count <= 1
constexpr std::uint32_t kStale84 = 0xDEADBEEFu;   // what word 21 holds before the call

struct Block {
    std::uint32_t w[kWords];

    Block() {
        for (auto &x : w) x = kFill;
        w[kEngineSpeedWord] = kStale84;
    }
    void set_ratios(std::initializer_list<float> table) {  // entries 1, 2, ... in order
        int i = 1;
        for (float f : table) w[kGearRatioWordBase + i++] = bits(f);
    }
};

// Runs the function and checks the return, the stored word, the divide flag, and that no word
// other than 21 changed.
void run(const char *what, Block b, std::uint32_t divisor, std::uint32_t dividend, std::int32_t want_index,
         std::uint32_t want_stored, bool want_div_zero = false) {
    Block before = b;
    bool div_zero = !want_div_zero;  // make sure the function writes the flag either way
    const std::int32_t got = select_gear_under_rev_limit(b.w, divisor, dividend, kSentinel, &div_zero);
    check_eq(what, static_cast<std::uint32_t>(got), static_cast<std::uint32_t>(want_index));
    check_eq(what, b.w[kEngineSpeedWord], want_stored);
    check_eq(what, div_zero ? 1u : 0u, want_div_zero ? 1u : 0u);
    for (int i = 0; i < kWords; ++i) {
        if (i == kEngineSpeedWord) continue;
        if (b.w[i] != before.w[i]) {
            std::printf("FAIL %s: word %d changed from 0x%08X to 0x%08X\n", what, i, before.w[i], b.w[i]);
            ++failures;
        }
    }
    // The null flag pointer form must behave identically.
    Block again = before;
    const std::int32_t got2 = select_gear_under_rev_limit(again.w, divisor, dividend, kSentinel);
    check_eq(what, static_cast<std::uint32_t>(got2), static_cast<std::uint32_t>(want_index));
    check_eq(what, again.w[kEngineSpeedWord], want_stored);
}

// A block whose limit is 8192 rpm (0x44567750 ~ 857.86 rad/s), multiplier 4, six ratios giving
// values 2400, 1600, 1200, 800, 600, 400 for r = 200 (dividend 100, divisor 0.5).
Block typical() {
    Block b;
    b.w[kRevLimitWord] = bits(8192.0f);
    b.w[kGearMultiplierWord] = bits(4.0f);
    b.set_ratios({3.0f, 2.0f, 1.5f, 1.0f, 0.75f, 0.5f});
    b.w[kGearCountWord] = 7;
    return b;
}

constexpr std::uint32_t kLimit8192 = 0x44567750u;

void ordinary_selection() {
    // 2400 > 1600 > 1200 > 857.86 >= 800: entry 4 is the first under the limit.
    run("typical: fourth gear", typical(), bits(0.5f), bits(100.0f), 4, bits(800.0f));

    // r = 50 (25 / 0.5): values 600, 400, ...; entry 1 already qualifies.
    run("first entry qualifies", typical(), bits(0.5f), bits(25.0f), 1, bits(600.0f));

    // r = 1000 (500 / 0.5): values 12000 ... 2000, none under the limit; returns count (7) and
    // stores the value for entry 6: 4 * (1000 * 0.5) = 2000.
    run("no entry qualifies", typical(), bits(0.5f), bits(500.0f), 7, bits(2000.0f));

    // Last entry exactly qualifies: count 3 with ratios 3, 2 and r = 200 gives 2400, 1600 both
    // over the limit -> returns 3, stores 1600.
    Block b = typical();
    b.w[kGearCountWord] = 3;
    run("count 3, none qualify", b, bits(0.5f), bits(100.0f), 3, bits(1600.0f));

    // count 2 examines entry 1 only.
    b = typical();
    b.w[kGearCountWord] = 2;
    run("count 2, entry 1 too high", b, bits(0.5f), bits(100.0f), 2, bits(2400.0f));
    run("count 2, entry 1 qualifies", b, bits(0.5f), bits(25.0f), 1, bits(600.0f));
}

void boundary_against_limit() {
    // r = 1 and multiplier 1 make value == ratio bit for bit, so the ratio can be set to the
    // limit itself: equal qualifies (the compare is <=), one ulp above does not.
    Block b = typical();
    b.w[kGearMultiplierWord] = bits(1.0f);
    b.w[kGearRatioWordBase + 1] = kLimit8192 + 1;
    b.w[kGearRatioWordBase + 2] = kLimit8192;
    b.w[kGearCountWord] = 4;
    run("equal to limit qualifies, one ulp above does not", b, bits(1.0f), bits(1.0f), 2, kLimit8192);

    b.w[kGearCountWord] = 2;
    run("one ulp above as the only entry", b, bits(1.0f), bits(1.0f), 2, kLimit8192 + 1);
}

void no_entry_examined() {
    // count <= 1: the loop never runs, 1 is returned and the inherited value replaces word 21.
    for (std::int32_t count : {1, 0, -1, -5, INT32_MIN}) {
        Block b = typical();
        b.w[kGearCountWord] = static_cast<std::uint32_t>(count);
        char what[64];
        std::snprintf(what, sizeof what, "count %d stores the inherited value", count);
        run(what, b, bits(0.5f), bits(100.0f), 1, kSentinel);
    }
    // The divide still happens before the loop test: a zero divisor is flagged with count 0.
    Block b = typical();
    b.w[kGearCountWord] = 0;
    run("count 0 still flags divide by zero", b, 0u, bits(100.0f), 1, kSentinel, true);
    // ... and a huge count pattern that is negative as int32 (0x80000001) is "no entries".
    b = typical();
    b.w[kGearCountWord] = 0x80000001u;
    run("count 0x80000001 is negative", b, bits(0.5f), bits(100.0f), 1, kSentinel);
}

void divide_edge_cases() {
    // 1 / 3 truncates to 0x3EAAAAAA; with multiplier 1 and ratio 1 that is the stored value, and
    // it is far under the limit so entry 1 is chosen.
    Block b = typical();
    b.w[kGearMultiplierWord] = bits(1.0f);
    b.set_ratios({1.0f});
    b.w[kGearCountWord] = 2;
    run("1/3 truncates", b, bits(3.0f), bits(1.0f), 1, 0x3EAAAAAAu);

    // Divide by zero: r = +0x7FFFFFFF; with ratio 1 and multiplier 1 every value is the largest
    // positive pattern, which is above any ordinary limit, so nothing qualifies: returns count and
    // stores 0x7FFFFFFF, flag set.
    b = typical();
    b.w[kGearMultiplierWord] = bits(1.0f);
    b.set_ratios({1.0f, 1.0f, 1.0f});
    b.w[kGearCountWord] = 4;
    run("positive / 0 saturates and never qualifies", b, 0u, bits(100.0f), 4, 0x7FFFFFFFu, true);
    // Negative dividend: r = 0xFFFFFFFF, the most negative pattern, under every limit -> entry 1.
    run("negative / 0 qualifies at once", b, 0u, bits(-100.0f), 1, 0xFFFFFFFFu, true);
    // 0 / 0 is also a divide by zero (positive sign).
    run("0 / 0", b, 0u, 0u, 4, 0x7FFFFFFFu, true);
    // A denormal divisor is flushed to zero first.
    run("x / denormal is a divide by zero", b, 0x00000001u, bits(100.0f), 4, 0x7FFFFFFFu, true);
    // -0 divisor gives a negative saturated quotient.
    run("positive / -0", b, 0x80000000u, bits(100.0f), 1, 0xFFFFFFFFu, true);

    // 0 / x is a signed zero with no flag; -0 * positive * positive stays -0; -0 <= limit holds.
    run("-0 / x", b, bits(2.0f), 0x80000000u, 1, 0x80000000u, false);
    run("+0 / x", b, bits(2.0f), 0u, 1, 0u, false);
    run("denormal / x flushes to +0", b, bits(2.0f), 0x00000001u, 1, 0u, false);
}

void multiply_edge_cases() {
    // Grouping: r = ratio = 1+ulp, multiplier = 3+ulp -> 0x40400004 (regrouped would be
    // 0x40400003); the limit is far above so entry 1 is chosen and the value is stored.
    Block b = typical();
    b.w[kGearMultiplierWord] = 0x40400001u;
    b.w[kGearRatioWordBase + 1] = 0x3F800001u;
    b.w[kGearCountWord] = 2;
    run("truncating multiplies in retail grouping", b, bits(1.0f), 0x3F800001u, 1, 0x40400004u);

    // Denormal ratio: flushed, so the value is +0 and qualifies under a positive limit.
    b = typical();
    b.w[kGearRatioWordBase + 1] = 0x00000001u;  // the smallest positive denormal
    b.w[kGearCountWord] = 2;
    run("denormal ratio gives +0", b, bits(0.5f), bits(100.0f), 1, 0u);
    // Negative denormal ratio: the sign survives the flush.
    b.w[kGearRatioWordBase + 1] = 0x80000001u;
    run("negative denormal ratio gives -0", b, bits(0.5f), bits(100.0f), 1, 0x80000000u);

    // Underflow: r = 2^-100 and ratio 2^-100 give exponent sum far below 1, so the product is +0;
    // multiplying a zero by the (positive) multiplier keeps it +0.
    b = typical();
    b.w[kGearRatioWordBase + 1] = 0x0D800000u;  // 2^-100
    b.w[kGearCountWord] = 2;
    run("underflow to +0", b, bits(1.0f), 0x0D800000u, 1, 0u);
    // ... with a negative multiplier the zero is -0 (sign of mult xor sign of +0).
    b.w[kGearMultiplierWord] = bits(-4.0f);
    run("underflow to -0 via negative multiplier", b, bits(1.0f), 0x0D800000u, 1, 0x80000000u);

    // Overflow saturates: 2^100 * 2^100 already exceeds the exponent range -> 0x7FFFFFFF, and
    // 4 * 0x7FFFFFFF stays saturated. Above every ordinary limit -> returns count.
    b = typical();
    b.w[kGearRatioWordBase + 1] = 0x71800000u;  // 2^100
    b.w[kGearCountWord] = 2;
    run("overflow saturates", b, bits(1.0f), 0x71800000u, 2, 0x7FFFFFFFu);
    // The saturation keeps the sign: negative dividend -> 0xFFFFFFFF, which qualifies.
    run("negative overflow saturates and qualifies", b, bits(1.0f), 0xF1800000u, 1, 0xFFFFFFFFu);

    // IEEE infinity / NaN patterns are plain huge values on the EE: a ratio of 0x7FC00000 times
    // 1.0 twice stays 0x7FC00000 (exact mantissa product), is above the limit and so is skipped;
    // the second entry (1.0) qualifies.
    b = typical();
    b.w[kGearMultiplierWord] = bits(1.0f);
    b.w[kGearRatioWordBase + 1] = 0x7FC00000u;
    b.w[kGearRatioWordBase + 2] = bits(1.0f);
    b.w[kGearCountWord] = 3;
    run("NaN pattern ratio is a huge finite value", b, bits(1.0f), bits(1.0f), 2, bits(1.0f));
    // As the only entry it is stored as is.
    b.w[kGearCountWord] = 2;
    run("NaN pattern stored unchanged", b, bits(1.0f), bits(1.0f), 2, 0x7FC00000u);
}

void compare_edge_cases() {
    // Limit from a NaN-pattern rev limit is a huge finite value, so even 2400 qualifies at entry 1
    // (host arithmetic would make the limit NaN and no compare would ever succeed).
    Block b = typical();
    b.w[kRevLimitWord] = 0x7FC00000u;
    run("NaN pattern rev limit is huge", b, bits(0.5f), bits(100.0f), 1, bits(2400.0f));
    // Infinity pattern likewise (0x7F800000 * pi/30 is finite and huge).
    b.w[kRevLimitWord] = 0x7F800000u;
    run("Inf pattern rev limit is huge", b, bits(0.5f), bits(100.0f), 1, bits(2400.0f));

    // Zero limit (rev limit 0 or denormal): -0 <= +0 and +0 <= -0 under the flushed ordering.
    b = typical();
    b.w[kRevLimitWord] = 0u;
    b.w[kGearRatioWordBase + 1] = 0x80000000u;  // -0 ratio -> value -0
    b.w[kGearCountWord] = 2;
    run("-0 value under +0 limit", b, bits(0.5f), bits(100.0f), 1, 0x80000000u);
    b.w[kRevLimitWord] = 0x80000000u;  // -0 rpm -> limit -0
    b.w[kGearRatioWordBase + 1] = 0u;  // +0 ratio -> value +0
    run("+0 value under -0 limit", b, bits(0.5f), bits(100.0f), 1, 0u);
    b.w[kRevLimitWord] = 0x00000001u;  // denormal rpm flushes: limit +0
    run("+0 value under denormal limit", b, bits(0.5f), bits(100.0f), 1, 0u);
    // The smallest positive normal is NOT under a zero limit.
    b.w[kRevLimitWord] = 0u;
    b.w[kGearMultiplierWord] = bits(1.0f);
    b.w[kGearRatioWordBase + 1] = 0x00800000u;
    run("smallest normal is above a zero limit", b, bits(1.0f), bits(1.0f), 2, 0x00800000u);

    // Negative limit (-8192 rpm -> 0xC4567750 ~ -857.86): -1000 qualifies, -1 and +0 do not.
    b = typical();
    b.w[kRevLimitWord] = bits(-8192.0f);
    b.w[kGearMultiplierWord] = bits(1.0f);
    b.set_ratios({0.0f, -1.0f, -1000.0f});
    b.w[kGearCountWord] = 4;
    run("negative limit ordering", b, bits(1.0f), bits(1.0f), 3, bits(-1000.0f));
}

void aliasing_past_the_table() {
    // A count of 8 or more makes entry 7 read word 8, the multiplier itself. With r = 1 and
    // multiplier 2, entries 1..6 at 1000 give 2000 (over the 857.86 limit) and entry 7 gives
    // 2 * (1 * 2) = 4, which qualifies: returns 7, stores 4.
    Block b = typical();
    b.w[kGearMultiplierWord] = bits(2.0f);
    b.set_ratios({1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f});
    b.w[kGearCountWord] = 9;
    run("entry 7 aliases the multiplier", b, bits(1.0f), bits(1.0f), 7, bits(4.0f));

    // With multiplier 1000 entry 7 gives 1e6 and entry 8 reads word 9 (+36) = 0.5 -> 500.
    b.w[kGearMultiplierWord] = bits(1000.0f);
    b.w[kGearMultiplierWord + 1] = bits(0.5f);
    run("entry 8 aliases word 9", b, bits(1.0f), bits(1.0f), 8, bits(500.0f));

    // Entry 9 reads the rev limit word itself (8192 -> 1000 * 8192, over) and entry 20 reads
    // word 21, the stale +84 value before it is overwritten: with 0.25 there, value 250 qualifies
    // at index 20 and then +84 becomes 250. Words 10..20 hold the fill pattern 0xA5A5A5A5, a
    // negative normal (-2.98e-13 or so) which multiplied out is negative and so qualifies first,
    // so those are set to 1000 as well.
    b.w[kGearMultiplierWord + 1] = bits(1000.0f);
    for (int w = kRevLimitWord + 1; w < kEngineSpeedWord; ++w) b.w[w] = bits(1000.0f);
    b.w[kEngineSpeedWord] = bits(0.25f);
    b.w[kGearCountWord] = 25;
    run("entry 20 reads the old +84", b, bits(1.0f), bits(1.0f), 20, bits(250.0f));

    // Entry 32 reads word 33, the count itself: 40 as a bit pattern is a denormal, flushed to +0
    // by the multiply, so with everything before it over the limit the result is index 32 and +0.
    for (int w = kEngineSpeedWord; w < kGearCountWord; ++w) b.w[w] = bits(1000.0f);
    b.w[kGearCountWord] = 40;
    run("entry 32 reads the count word as a denormal", b, bits(1.0f), bits(1.0f), 32, 0u);
}

void inherited_value_is_only_used_when_nothing_is_examined() {
    // With count >= 2 the inherited value never reaches memory: the loop always overwrites it.
    Block b = typical();
    bool flag = false;
    const std::int32_t got = select_gear_under_rev_limit(b.w, bits(0.5f), bits(500.0f), kSentinel, &flag);
    check_eq("inherited unused: index", static_cast<std::uint32_t>(got), 7u);
    check_eq("inherited unused: stored", b.w[kEngineSpeedWord], bits(2000.0f));
    // A different inherited value gives the same result.
    b = typical();
    select_gear_under_rev_limit(b.w, bits(0.5f), bits(500.0f), 0x12345678u, &flag);
    check_eq("inherited ignored", b.w[kEngineSpeedWord], bits(2000.0f));
}

}  // namespace

int main() {
    ordinary_selection();
    boundary_against_limit();
    no_entry_examined();
    divide_edge_cases();
    multiply_edge_cases();
    compare_edge_cases();
    aliasing_past_the_table();
    inherited_value_is_only_used_when_nothing_is_examined();
    std::printf("test_select_gear_under_rev_limit: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
