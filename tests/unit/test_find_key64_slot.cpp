// find_key64_slot (retail 0x002518E0) on synthetic registries.
//
// Expected values come from the specification, not from the game: the function returns the lowest
// index below the signed count whose 64-bit key equals the wanted key, and -1 otherwise. Each check
// below builds a table where that rule gives one unambiguous answer and states the reasoning inline.

#include "check.hpp"
#include "hooks/registry/find_key64_slot.hpp"

#include <cstdint>
#include <cstring>

using b3::registry::KeyTable;
using b3::registry::find_key64_slot;

namespace {

constexpr std::int32_t kCap = KeyTable::kCapacity;

// A table whose slot i holds key base + i, with the flag arrays filled with noise so a read of them
// would show up as a wrong answer. Alignment is at least 8, as the retail 64-bit loads require.
alignas(16) KeyTable g_table;

void fill(std::uint64_t base, std::int32_t count) {
    g_table.header_word = static_cast<std::int32_t>(0xDEADBEEF);
    g_table.count = count;
    for (std::int32_t i = 0; i < kCap; ++i) {
        g_table.slot_flag_a[i] = 0x55555555 ^ i;
        g_table.slot_flag_b[i] = static_cast<std::int32_t>(0xAAAAAAAAu) ^ i;
        g_table.keys[i] = base + static_cast<std::uint64_t>(i);
    }
}

void layout_matches_the_retail_object() {
    // The sibling lookups fix the three arrays at +8, +520 and +1032 and the key array needs 8-byte
    // alignment; the header's static_asserts cover the offsets, this covers alignment and the global.
    CHECK(alignof(KeyTable) >= 8);
    CHECK((reinterpret_cast<std::uintptr_t>(&g_table) & 7u) == 0u);
}

void empty_and_negative_counts_find_nothing() {
    // count <= 0: the loop body never runs, so even a key sitting in slot 0 is not found.
    fill(0x1000, 0);
    CHECK_EQ(find_key64_slot(g_table, 0x1000u), -1);
    g_table.count = -1;
    CHECK_EQ(find_key64_slot(g_table, 0x1000u), -1);
    g_table.count = INT32_MIN;
    CHECK_EQ(find_key64_slot(g_table, 0x1000u), -1);
    // A negative count must not be treated as a large unsigned one: the result is still -1 and the
    // search terminates (an unsigned loop would read far past the table before finding anything).
    g_table.count = -kCap;
    CHECK_EQ(find_key64_slot(g_table, 0x1005u), -1);
}

void single_slot() {
    fill(0x42, 1);
    CHECK_EQ(find_key64_slot(g_table, 0x42u), 0);      // the only slot matches: index 0
    CHECK_EQ(find_key64_slot(g_table, 0x43u), -1);     // slot 1 holds 0x43 but lies beyond count
    CHECK_EQ(find_key64_slot(g_table, 0u), -1);
}

void count_is_an_exclusive_upper_bound() {
    // Key base+7 sits in slot 7. It is visible once count > 7 and invisible when count == 7.
    fill(0x5000, 7);
    CHECK_EQ(find_key64_slot(g_table, 0x5007u), -1);
    g_table.count = 8;
    CHECK_EQ(find_key64_slot(g_table, 0x5007u), 7);
    CHECK_EQ(find_key64_slot(g_table, 0x5006u), 6);
    CHECK_EQ(find_key64_slot(g_table, 0x5000u), 0);
}

void full_table_boundaries() {
    // With count == capacity the last slot (127) is reachable and nothing beyond it is read.
    fill(0x7000, kCap);
    CHECK_EQ(find_key64_slot(g_table, 0x7000u + kCap - 1), kCap - 1);
    CHECK_EQ(find_key64_slot(g_table, 0x7000u), 0);
    CHECK_EQ(find_key64_slot(g_table, 0x7000u + kCap), -1);   // would be slot 128, past the array
    CHECK_EQ(find_key64_slot(g_table, 0x6FFFu), -1);
}

void zero_key_is_an_ordinary_value() {
    // Zero is matched like any other key: found when present below count, absent otherwise.
    fill(1, 4);                                   // keys 1,2,3,4: no zero
    CHECK_EQ(find_key64_slot(g_table, 0u), -1);
    g_table.keys[2] = 0;
    CHECK_EQ(find_key64_slot(g_table, 0u), 2);
}

void compare_is_full_64_bit() {
    // The wanted key and the slot key must agree in all 64 bits. A slot holding the zero-extended
    // 0x00000000FFFFFFFF does not match a sign-extended 0xFFFFFFFFFFFFFFFF and vice versa, and keys
    // that differ only in the upper or only in the lower half do not match either.
    fill(0, 4);
    g_table.keys[0] = 0x00000000FFFFFFFFull;
    g_table.keys[1] = 0xFFFFFFFFFFFFFFFFull;
    g_table.keys[2] = 0x8000000000000000ull;
    g_table.keys[3] = 0x0123456789ABCDEFull;
    CHECK_EQ(find_key64_slot(g_table, 0x00000000FFFFFFFFull), 0);
    CHECK_EQ(find_key64_slot(g_table, 0xFFFFFFFFFFFFFFFFull), 1);
    CHECK_EQ(find_key64_slot(g_table, 0x8000000000000000ull), 2);
    CHECK_EQ(find_key64_slot(g_table, 0x0123456789ABCDEFull), 3);
    CHECK_EQ(find_key64_slot(g_table, 0x0000000080000000ull), -1);   // low half of slot 2 only
    CHECK_EQ(find_key64_slot(g_table, 0x8000000000000001ull), -1);   // slot 2 with the low bit set
    CHECK_EQ(find_key64_slot(g_table, 0x0123456700000000ull), -1);   // upper half of slot 3 only
    CHECK_EQ(find_key64_slot(g_table, 0x0000000089ABCDEFull), -1);   // lower half of slot 3 only
    CHECK_EQ(find_key64_slot(g_table, 0xFFFFFFFF89ABCDEFull), -1);   // sign-extended lower half
}

void first_match_wins() {
    // Duplicates: the lowest index is returned, whatever the later slots hold.
    fill(0x9000, 10);
    g_table.keys[3] = 0xABCDu;
    g_table.keys[6] = 0xABCDu;
    g_table.keys[9] = 0xABCDu;
    CHECK_EQ(find_key64_slot(g_table, 0xABCDu), 3);
    g_table.keys[0] = 0xABCDu;
    CHECK_EQ(find_key64_slot(g_table, 0xABCDu), 0);
    // A duplicate at or beyond count does not count: with count 3 only slots 0..2 are live.
    g_table.keys[0] = 0x9000u;
    g_table.count = 3;
    CHECK_EQ(find_key64_slot(g_table, 0xABCDu), -1);
}

void flag_arrays_and_header_do_not_matter() {
    // The function reads only the count and the keys; changing everything else leaves the answer.
    fill(0x100, 5);
    const std::int32_t before = find_key64_slot(g_table, 0x103u);
    CHECK_EQ(before, 3);
    g_table.header_word = 0;
    std::memset(g_table.slot_flag_a, 0, sizeof g_table.slot_flag_a);
    std::memset(g_table.slot_flag_b, 0xFF, sizeof g_table.slot_flag_b);
    CHECK_EQ(find_key64_slot(g_table, 0x103u), before);
    // Keys beyond count may hold anything, including the wanted key, without being found.
    g_table.keys[5] = 0x1234u;
    g_table.keys[kCap - 1] = 0x1234u;
    CHECK_EQ(find_key64_slot(g_table, 0x1234u), -1);
}

void miss_result_sign_extends_to_all_ones() {
    // The retail function leaves -1 sign-extended across the 64-bit result register; the shim gets
    // that for free by widening our int32_t. A found index widens with a zero upper half.
    fill(0x200, 2);
    const std::int64_t miss = find_key64_slot(g_table, 0x999u);
    CHECK_EQ(static_cast<std::uint64_t>(miss), 0xFFFFFFFFFFFFFFFFull);
    const std::int64_t hit = find_key64_slot(g_table, 0x201u);
    CHECK_EQ(static_cast<std::uint64_t>(hit), 1ull);
}

}

int main() {
    layout_matches_the_retail_object();
    empty_and_negative_counts_find_nothing();
    single_slot();
    count_is_an_exclusive_upper_bound();
    full_table_boundaries();
    zero_key_is_an_ordinary_value();
    compare_is_full_64_bit();
    first_match_wins();
    flag_arrays_and_header_do_not_matter();
    miss_result_sign_extends_to_all_ones();
    return check::done("test_find_key64_slot");
}
