// Standalone test for b3::frontend::compare_wide_strings (retail 0x0035EFA0).
//
// Every expected value below is derived from the specification by reasoning, never from
// running the game: the routine walks both strings unit by unit, treats a 16-bit zero as the
// terminator, compares units as unsigned 16-bit values, returns exactly -1 / 0 / +1, the
// string that ends first sorts first, and nothing past the first difference or terminator
// is read. Each case states which of those rules fixes its answer.

#include "check.hpp"
#include "hooks/frontend/compare_wide_strings.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

using b3::frontend::compare_wide_strings;

namespace {

// A buffer whose tail is filled with a non-terminator unit chosen so that any read past the
// index the specification allows would change the answer. The expected values on these
// buffers therefore also prove the read bound.
struct Guarded {
    std::uint16_t units[64];
    Guarded(std::initializer_list<std::uint16_t> prefix, std::uint16_t filler) {
        std::size_t i = 0;
        for (std::uint16_t u : prefix) units[i++] = u;
        for (; i < 64; ++i) units[i] = filler;
    }
};

}

int main() {
    // --- Equal / empty -------------------------------------------------------------------
    {
        // Both empty: s1 ends at unit 0, s2 also ends -> equal -> 0.
        const std::uint16_t a[] = {0};
        const std::uint16_t b[] = {0};
        CHECK_EQ(compare_wide_strings(a, b), 0);
        // Same buffer passed twice: every unit matches until both terminate -> 0.
        const std::uint16_t s[] = {u'a', u'b', u'c', 0};
        CHECK_EQ(compare_wide_strings(s, s), 0);
        // Identical content in distinct buffers -> 0, in both argument orders.
        const std::uint16_t t[] = {u'a', u'b', u'c', 0};
        CHECK_EQ(compare_wide_strings(s, t), 0);
        CHECK_EQ(compare_wide_strings(t, s), 0);
    }

    // --- Prefix rules: the string that ends first sorts first -----------------------------
    {
        // s1 empty, s2 non-empty: s1 ends at index 0 -> -1 (no unit comparison happens).
        const std::uint16_t e[] = {0};
        const std::uint16_t x[] = {u'x', 0};
        CHECK_EQ(compare_wide_strings(e, x), -1);
        // s1 non-empty, s2 empty: s2 ends at index 0 while s1 has a unit -> +1.
        CHECK_EQ(compare_wide_strings(x, e), 1);
        // "ab" vs "abc": match for two units, then s1 ends and s2 does not -> -1.
        const std::uint16_t ab[] = {u'a', u'b', 0};
        const std::uint16_t abc[] = {u'a', u'b', u'c', 0};
        CHECK_EQ(compare_wide_strings(ab, abc), -1);
        // "abc" vs "ab": match for two units, then s2 ends while s1 has 'c' -> +1.
        CHECK_EQ(compare_wide_strings(abc, ab), 1);
    }

    // --- Ordering is numeric on the first differing unit; result is never the difference ---
    {
        // 'a' (0x61) < 'b' (0x62) at index 0 -> -1 / +1. 'a' vs 'z' has a gap of 25 and
        // must still give exactly -1 / +1, so the result is not the unit difference.
        const std::uint16_t a[] = {u'a', 0};
        const std::uint16_t b[] = {u'b', 0};
        const std::uint16_t z[] = {u'z', 0};
        CHECK_EQ(compare_wide_strings(a, b), -1);
        CHECK_EQ(compare_wide_strings(b, a), 1);
        CHECK_EQ(compare_wide_strings(a, z), -1);
        CHECK_EQ(compare_wide_strings(z, a), 1);
        // Difference deep in the string: "hello" vs "help": 'l' (0x6C) < 'p' (0x70) at index 3 -> -1.
        const std::uint16_t hello[] = {u'h', u'e', u'l', u'l', u'o', 0};
        const std::uint16_t help[] = {u'h', u'e', u'l', u'p', 0};
        CHECK_EQ(compare_wide_strings(hello, help), -1);
        CHECK_EQ(compare_wide_strings(help, hello), 1);
        // No case folding: 'A' (0x41) < 'a' (0x61), so "ABC" vs "abc" differ at index 0 -> -1.
        const std::uint16_t upper[] = {u'A', u'B', u'C', 0};
        const std::uint16_t lower[] = {u'a', u'b', u'c', 0};
        CHECK_EQ(compare_wide_strings(upper, lower), -1);
        CHECK_EQ(compare_wide_strings(lower, upper), 1);
        // The first difference decides even when later units would reverse the order:
        // "az" vs "ba": 'a' < 'b' at index 0 -> -1 regardless of 'z' > 'a' at index 1.
        const std::uint16_t az[] = {u'a', u'z', 0};
        const std::uint16_t ba[] = {u'b', u'a', 0};
        CHECK_EQ(compare_wide_strings(az, ba), -1);
        CHECK_EQ(compare_wide_strings(ba, az), 1);
    }

    // --- Units are unsigned: 0x8000-0xFFFF sort after 0x0001-0x7FFF ---------------------
    {
        // 0x7FFF vs 0x8000: unsigned 0x7FFF < 0x8000 -> -1. A sign-extending implementation
        // would see -32768 < 32767 and answer +1, so this pins the zero-extension.
        const std::uint16_t lo[] = {0x7FFF, 0};
        const std::uint16_t hi[] = {0x8000, 0};
        CHECK_EQ(compare_wide_strings(lo, hi), -1);
        CHECK_EQ(compare_wide_strings(hi, lo), 1);
        // 0x0001 vs 0xFFFF: smallest non-terminator against the largest unit -> -1 / +1.
        const std::uint16_t one[] = {0x0001, 0};
        const std::uint16_t top[] = {0xFFFF, 0};
        CHECK_EQ(compare_wide_strings(one, top), -1);
        CHECK_EQ(compare_wide_strings(top, one), 1);
        // 0xFFFF vs 0xFFFF then both end -> 0 (large units compare equal like any other).
        const std::uint16_t top2[] = {0xFFFF, 0};
        CHECK_EQ(compare_wide_strings(top, top2), 0);
        // 0xFFFE vs 0xFFFF: adjacent large values, unsigned 0xFFFE < 0xFFFF -> -1 / +1.
        const std::uint16_t almost[] = {0xFFFE, 0};
        CHECK_EQ(compare_wide_strings(almost, top), -1);
        CHECK_EQ(compare_wide_strings(top, almost), 1);
        // Surrogate code units are not decoded: 0xD800 < 0xE000 as plain numbers -> -1,
        // which differs from code-point order (the supplementary character would be > U+E000).
        const std::uint16_t surrogate[] = {0xD800, 0xDC00, 0};
        const std::uint16_t private_use[] = {0xE000, 0};
        CHECK_EQ(compare_wide_strings(surrogate, private_use), -1);
        CHECK_EQ(compare_wide_strings(private_use, surrogate), 1);
    }

    // --- Terminator is a 16-bit zero, not a zero byte ---------------------------------
    {
        // 0x0100 (high byte set, low byte zero) is a real unit, so "0x0100" vs "" has s2
        // ending first -> +1; a byte-wise compare would call s1 empty and answer 0.
        // Likewise 0x0001 (low byte set, high byte zero).
        const std::uint16_t hi_byte[] = {0x0100, 0};
        const std::uint16_t lo_byte[] = {0x0001, 0};
        const std::uint16_t empty[] = {0};
        CHECK_EQ(compare_wide_strings(hi_byte, empty), 1);
        CHECK_EQ(compare_wide_strings(lo_byte, empty), 1);
        CHECK_EQ(compare_wide_strings(empty, hi_byte), -1);
        CHECK_EQ(compare_wide_strings(empty, lo_byte), -1);
        // 0x0001 < 0x0100 numerically -> -1 / +1.
        CHECK_EQ(compare_wide_strings(lo_byte, hi_byte), -1);
        CHECK_EQ(compare_wide_strings(hi_byte, lo_byte), 1);
        // A zero unit in the middle ends the string there: "a\0b" vs "a\0c" both end at
        // index 1 -> 0; the differing 'b' / 'c' past the terminator is never consulted.
        const std::uint16_t a0b[] = {u'a', 0, u'b', 0};
        const std::uint16_t a0c[] = {u'a', 0, u'c', 0};
        CHECK_EQ(compare_wide_strings(a0b, a0c), 0);
    }

    // --- Read bound: nothing past the first difference or terminator is touched ----------
    {
        // Tails are never terminated and hold units that would flip the answer if compared.
        // The specification reads s1[k] and s2[k] only for k up to the first mismatch or
        // terminator, so the result must come from the prefix alone.
        //
        // Both "ab" terminated at index 2, tails 0xFFFF vs 0x0001 -> 0.
        Guarded eq1({u'a', u'b', 0}, 0xFFFF);
        Guarded eq2({u'a', u'b', 0}, 0x0001);
        CHECK_EQ(compare_wide_strings(eq1.units, eq2.units), 0);
        // Mismatch at index 0 with unterminated tails: 'a' < 'b' -> -1; the tails (0xFFFF
        // for s1, 0x0001 for s2) would answer +1 if they were ever compared.
        Guarded m1({u'a'}, 0xFFFF);
        Guarded m2({u'b'}, 0x0001);
        CHECK_EQ(compare_wide_strings(m1.units, m2.units), -1);
        CHECK_EQ(compare_wide_strings(m2.units, m1.units), 1);
        // s1 ends at index 1, s2 unterminated (filler 0x0001): s1 is the shorter -> -1, and
        // only s2[0] and s2[1] are read (s2[1] = 0x0001 != 0 settles it).
        Guarded p1({u'a', 0}, 0xFFFF);
        Guarded p2({u'a'}, 0x0001);
        CHECK_EQ(compare_wide_strings(p1.units, p2.units), -1);
        CHECK_EQ(compare_wide_strings(p2.units, p1.units), 1);
    }

    // --- Longer strings and the 64-bit view of the result -----------------------------
    {
        // 63 matching units then both terminate -> 0: the loop must keep going past short
        // buffers. Then raise the unit at index 62 on one side: 0x41+.. vs that + 1 -> -1 / +1.
        std::uint16_t long1[64], long2[64];
        for (int i = 0; i < 63; ++i) long1[i] = long2[i] = static_cast<std::uint16_t>(0x0041 + (i % 26));
        long1[63] = long2[63] = 0;
        CHECK_EQ(compare_wide_strings(long1, long2), 0);
        long2[62] = static_cast<std::uint16_t>(long1[62] + 1);
        CHECK_EQ(compare_wide_strings(long1, long2), -1);
        CHECK_EQ(compare_wide_strings(long2, long1), 1);
        // The result is a signed 32-bit int, so -1 widens to all ones in 64 bits and +1 / 0
        // widen to 0x1 / 0x0, matching how the retail code leaves its result register.
        const std::uint16_t a[] = {u'a', 0};
        const std::uint16_t b[] = {u'b', 0};
        const std::uint64_t minus_one = static_cast<std::uint64_t>(static_cast<std::int64_t>(compare_wide_strings(a, b)));
        const std::uint64_t plus_one = static_cast<std::uint64_t>(static_cast<std::int64_t>(compare_wide_strings(b, a)));
        const std::uint64_t zero = static_cast<std::uint64_t>(static_cast<std::int64_t>(compare_wide_strings(a, a)));
        CHECK_EQ(minus_one, 0xFFFFFFFFFFFFFFFFull);
        CHECK_EQ(plus_one, 0x1ull);
        CHECK_EQ(zero, 0x0ull);
    }

    return check::done("test_compare_wide_strings");
}
