// Standalone test for b3::ai::compare_ints_cyclic_from_pivot (retail 0x0026CBE0).
//
// Expected values are derived from the specification, not from the game. The rules, in order:
//   1. a == P -> -1 (b unread)      2. b == P -> +1
//   3. P < a and b < P -> -1        4. a < P and P < b -> +1
//   5. a < b -> -1                  6. (b < a) ? +1 : 0
// The sweep below uses an independent restatement of the same order: key(x) = 0 for the pivot,
// 1 for values above it, 2 for values below it; compare by key, then ascending; except that two
// pivot-valued elements give -1 (rule 1 fires before rule 2). Every hand-written case names the
// rule that produces its value. The pointer form is checked against guest RAM from
// tests/unit/support with the pivot seeded at 0x004E19F8, including the "b is unread" trace.

#include "check.hpp"
#include "hooks/ai/compare_ints_cyclic_from_pivot.hpp"

#include <cstdint>
#include <cstring>
#include <limits>

using b3::ai::compare_ints_cyclic_from_pivot;
using b3::ai::kCyclicPivotAddress;
using ee::guest_ptr;
using ee::ld;
using ee::st;

namespace {

constexpr std::int32_t kMin = std::numeric_limits<std::int32_t>::min();
constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();

// Independent model: key order then ascending, with the pivot-tie exception stated in the spec.
std::int32_t model(std::int32_t a, std::int32_t b, std::int32_t pivot) {
    if (a == pivot) return -1;      // rule 1, also covers a == b == pivot
    if (b == pivot) return 1;       // rule 2
    const int key_a = a > pivot ? 1 : 2;
    const int key_b = b > pivot ? 1 : 2;
    if (key_a != key_b) return key_a < key_b ? -1 : 1;
    if (a == b) return 0;
    return a < b ? -1 : 1;
}

// The retail result register holds the int32 sign-extended to 64 bits: -1 is all ones, 0 and +1
// are plain. The shim does this with a sign-extending widen, which is what we check here.
std::uint64_t as_register(std::int32_t result) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
}

void pivot_sorts_first() {
    // Rule 1: a is the pivot, whatever b is.
    CHECK_EQ(compare_ints_cyclic_from_pivot(5, 9, 5), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(5, 1, 5), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(5, -7, 5), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(-1, 0, -1), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(0, 0, 0), -1);          // a == b == P: rule 1, not 0
    CHECK_EQ(compare_ints_cyclic_from_pivot(5, 5, 5), -1);
    // Rule 2: b is the pivot and a is not.
    CHECK_EQ(compare_ints_cyclic_from_pivot(9, 5, 5), 1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(1, 5, 5), 1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(-7, 5, 5), 1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(0, -1, -1), 1);
    // Not antisymmetric on pivot ties: both directions are -1.
    CHECK_EQ(compare_ints_cyclic_from_pivot(3, 3, 3), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(3, 3, 3), compare_ints_cyclic_from_pivot(3, 3, 3));
}

void wrap_rules_before_plain_order() {
    // Rule 3: a above the pivot, b below it -> a first even though a > b numerically.
    CHECK_EQ(compare_ints_cyclic_from_pivot(7, 2, 5), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(6, 4, 5), -1);          // adjacent on each side
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMax, kMin, 0), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(0, -2, -1), -1);        // pivot -1, the idle value
    // Rule 4: a below, b above -> b first.
    CHECK_EQ(compare_ints_cyclic_from_pivot(2, 7, 5), 1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(4, 6, 5), 1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMin, kMax, 0), 1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(-2, 0, -1), 1);
    // A plain ascending compare would give the opposite sign on every pair above; pin one.
    CHECK((7 < 2) == false && compare_ints_cyclic_from_pivot(7, 2, 5) == -1);
}

void same_side_is_ascending() {
    // Rule 5 and 6: both above the pivot.
    CHECK_EQ(compare_ints_cyclic_from_pivot(6, 7, 5), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(7, 6, 5), 1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(7, 7, 5), 0);           // the only 0 path
    // Both below the pivot.
    CHECK_EQ(compare_ints_cyclic_from_pivot(1, 2, 5), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(2, 1, 5), 1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(2, 2, 5), 0);
    // Both below a negative pivot, all negative.
    CHECK_EQ(compare_ints_cyclic_from_pivot(-30, -20, -10), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(-20, -30, -10), 1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(-20, -20, -10), 0);
    // Equal non-pivot values are 0 in both directions (antisymmetric away from the pivot).
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMax, kMax, 0), 0);
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMin, kMin, 0), 0);
}

void compares_are_signed() {
    // Signed: kMin is below every pivot, kMax above. Unsigned compares would invert these.
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMin, 1, 0), 1);        // rule 4 (kMin < 0 < 1)
    CHECK_EQ(compare_ints_cyclic_from_pivot(1, kMin, 0), -1);       // rule 3
    CHECK_EQ(compare_ints_cyclic_from_pivot(-1, 1, 0), 1);          // rule 4
    CHECK_EQ(compare_ints_cyclic_from_pivot(1, -1, 0), -1);         // rule 3
    CHECK_EQ(compare_ints_cyclic_from_pivot(-5, -3, 7), -1);        // rule 5, both below
    CHECK_EQ(compare_ints_cyclic_from_pivot(-3, -5, 7), 1);         // rule 6
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMin, kMax, kMin), -1); // rule 1
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMax, kMin, kMin), 1);  // rule 2
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMin, kMax, kMax), 1);  // rule 2
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMin + 1, kMin, kMax), 1);   // rule 6, both below
    CHECK_EQ(compare_ints_cyclic_from_pivot(kMax - 1, kMax, kMin), -1);  // rule 5, both above
}

void result_register_view() {
    CHECK_EQ(as_register(compare_ints_cyclic_from_pivot(5, 9, 5)), 0xFFFFFFFFFFFFFFFFull);
    CHECK_EQ(as_register(compare_ints_cyclic_from_pivot(9, 5, 5)), 1ull);
    CHECK_EQ(as_register(compare_ints_cyclic_from_pivot(7, 7, 5)), 0ull);
    CHECK_EQ(as_register(compare_ints_cyclic_from_pivot(7, 6, 5)), 1ull);
    CHECK_EQ(as_register(compare_ints_cyclic_from_pivot(6, 7, 5)), 0xFFFFFFFFFFFFFFFFull);
}

// Every ordered pair from a pool of boundary values against every pivot from the same pool.
void sweep_against_model() {
    constexpr std::int32_t pool[] = {kMin, kMin + 1, -1000000, -2, -1, 0, 1, 2, 7, 1000000, kMax - 1, kMax};
    constexpr int n = sizeof pool / sizeof pool[0];
    int checked = 0;
    for (int p = 0; p < n; ++p)
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const std::int32_t got = compare_ints_cyclic_from_pivot(pool[i], pool[j], pool[p]);
                const std::int32_t want = model(pool[i], pool[j], pool[p]);
                if (got != want)
                    std::printf("mismatch a=%d b=%d pivot=%d: got %d want %d\n", pool[i], pool[j], pool[p], got, want);
                CHECK_EQ(got, want);
                CHECK(got == -1 || got == 0 || got == 1);
                ++checked;
            }
    CHECK_EQ(checked, n * n * n);
    // Away from the pivot the comparator is antisymmetric; on pivot pairs it is -1 both ways.
    for (int p = 0; p < n; ++p)
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const std::int32_t fwd = compare_ints_cyclic_from_pivot(pool[i], pool[j], pool[p]);
                const std::int32_t rev = compare_ints_cyclic_from_pivot(pool[j], pool[i], pool[p]);
                if (pool[i] == pool[p] && pool[j] == pool[p]) {
                    CHECK_EQ(fwd, -1);
                    CHECK_EQ(rev, -1);
                } else {
                    CHECK_EQ(fwd, -rev);
                }
            }
}

// The comparator induces the order: pivot, then ascending values above it, then ascending values
// below it. A plain insertion sort with the comparator must produce exactly that.
void induced_order() {
    const std::int32_t pivot = 3;
    std::int32_t values[] = {7, -4, 3, 1, 9, 0, 5, 2, -1};
    constexpr int n = sizeof values / sizeof values[0];
    for (int i = 1; i < n; ++i) {
        const std::int32_t v = values[i];
        int j = i - 1;
        while (j >= 0 && compare_ints_cyclic_from_pivot(values[j], v, pivot) > 0) {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = v;
    }
    const std::int32_t expected[] = {3, 5, 7, 9, -4, -1, 0, 1, 2};
    for (int i = 0; i < n; ++i) CHECK_EQ(values[i], expected[i]);
}

void clear_guest_memory() {
    std::memset(ps2::g_ram, 0, ps2::kRamSize);
    std::memset(ps2::g_spr, 0, ps2::kScratchpadSize);
}

// The pointer form against guest RAM: elements where qsort would hand them (an int array at a
// large object offset) and the pivot seeded at the fixed global.
void pointer_form_reads_guest_memory() {
    clear_guest_memory();
    const std::uint32_t base = 0x01A32980u;        // some object + 0x32980, 4-aligned
    const guest_ptr<std::int32_t> slots{base};
    st<std::int32_t>(kCyclicPivotAddress, 5);
    slots[0] = 5;
    slots[1] = 7;
    slots[2] = 2;
    slots[3] = 7;
    slots[4] = -3;
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 0, slots + 1), -1);   // rule 1
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 1, slots + 0), 1);    // rule 2
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 1, slots + 2), -1);   // rule 3
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 2, slots + 1), 1);    // rule 4
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 1, slots + 3), 0);    // rule 6, equal
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 4, slots + 2), -1);   // rule 5, both below
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 2, slots + 4), 1);    // rule 6
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 0, slots + 0), -1);   // self-compare on the pivot
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 1, slots + 1), 0);    // self-compare off the pivot

    // The pivot is read at call time: changing the global changes the answer for the same slots.
    st<std::int32_t>(kCyclicPivotAddress, 7);
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 1, slots + 0), -1);   // 7 is now the pivot
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 0, slots + 1), 1);
    st<std::int32_t>(kCyclicPivotAddress, -1);                            // the caller's idle value
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 4, slots + 2), 1);    // -3 below -1, 2 above: rule 4
    CHECK_EQ(compare_ints_cyclic_from_pivot(slots + 2, slots + 4), -1);

    // Nothing was written by the comparator.
    CHECK_EQ(ld<std::int32_t>(kCyclicPivotAddress), -1);
    CHECK_EQ(static_cast<std::int32_t>(slots[0]), 5);
    CHECK_EQ(static_cast<std::int32_t>(slots[1]), 7);
    CHECK_EQ(static_cast<std::int32_t>(slots[2]), 2);
    CHECK_EQ(static_cast<std::int32_t>(slots[3]), 7);
    CHECK_EQ(static_cast<std::int32_t>(slots[4]), -3);
}

// Rule 1 returns before b is loaded. The fork's loader reads host memory for any in-range guest
// address, so an unreadable b cannot be planted; instead b is placed at the very top of RAM where
// a 32-bit load would run off the array if it happened (the support array has 64 slack bytes,
// which keeps even a wrong implementation from faulting while still exercising the address).
void pivot_element_does_not_need_b() {
    clear_guest_memory();
    st<std::int32_t>(kCyclicPivotAddress, 42);
    const guest_ptr<std::int32_t> a{0x00100000u};
    *a = 42;
    const guest_ptr<std::int32_t> b_at_end{ps2::kRamSize - 4};
    *b_at_end = 42;
    CHECK_EQ(compare_ints_cyclic_from_pivot(a, b_at_end), -1);
    // And when a is not the pivot, b at the end of RAM is read normally.
    *a = 41;
    CHECK_EQ(compare_ints_cyclic_from_pivot(a, b_at_end), 1);           // rule 2
    *b_at_end = 43;
    CHECK_EQ(compare_ints_cyclic_from_pivot(a, b_at_end), 1);           // rule 4
}

// Negative and boundary values through guest memory, where the load must sign-extend.
void guest_loads_are_signed() {
    clear_guest_memory();
    st<std::int32_t>(kCyclicPivotAddress, 0);
    const guest_ptr<std::int32_t> p{0x00200000u};
    p[0] = kMin;
    p[1] = kMax;
    p[2] = -1;
    p[3] = 1;
    CHECK_EQ(compare_ints_cyclic_from_pivot(p + 0, p + 1), 1);     // kMin below 0, kMax above
    CHECK_EQ(compare_ints_cyclic_from_pivot(p + 1, p + 0), -1);
    CHECK_EQ(compare_ints_cyclic_from_pivot(p + 2, p + 3), 1);     // -1 below, 1 above
    CHECK_EQ(compare_ints_cyclic_from_pivot(p + 0, p + 2), -1);    // both below: kMin < -1
    CHECK_EQ(compare_ints_cyclic_from_pivot(p + 3, p + 1), -1);    // both above: 1 < kMax
    // A pivot of kMin: everything else is "above" it.
    st<std::int32_t>(kCyclicPivotAddress, kMin);
    CHECK_EQ(compare_ints_cyclic_from_pivot(p + 0, p + 1), -1);    // rule 1
    CHECK_EQ(compare_ints_cyclic_from_pivot(p + 2, p + 3), -1);    // both above, -1 < 1
}

}  // namespace

int main() {
    pivot_sorts_first();
    wrap_rules_before_plain_order();
    same_side_is_ascending();
    compares_are_signed();
    result_register_view();
    sweep_against_model();
    induced_order();
    pointer_form_reads_guest_memory();
    pivot_element_does_not_need_b();
    guest_loads_are_signed();
    return check::done("test_compare_ints_cyclic_from_pivot");
}
