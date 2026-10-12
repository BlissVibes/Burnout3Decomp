#pragma once

// find_key64_slot: linear search of a 64-bit id registry for a key, returning its slot index.
//
// Retail address 0x002518E0 (game code, SLUS-21050). Evidence: the recompiler's translation of
// the function (unit_0054.cpp) read against the reader's specification; the two agree. The sibling
// lookups at 0x00251920, 0x002519B0 and 0x00251EF0 run the same search and then read one of the two
// 32-bit per-slot arrays, which is where the table layout below comes from. The capacity of 128 is
// inferred from the spacing of those arrays (8..520, 520..1032), not from an allocator or a bounds
// check. Status: draft, oracle pending.
//
// Behaviour: the registry holds a signed slot count at byte 4 and an array of 64-bit keys starting at
// byte 1032. The function compares every key below the count against the wanted key, all 64 bits at
// once, and returns the first matching index, or -1 when none matches. A count of zero or less means
// no slot is examined. The retail code performs no bounds check against the capacity: a count above
// 128 makes it read beyond the key array, which is undefined here as well.
//
// Pure: no writes, no globals, no floating point.

#include <cstddef>
#include <cstdint>

namespace b3::registry {

// The registry object as the lookups see it. Field names are ours; the leading word is never read
// by these functions and its meaning is unknown. The two 32-bit arrays are read by the sibling
// lookups only, one per slot each; this function never touches them.
struct KeyTable {
    static constexpr std::int32_t kCapacity = 128;

    std::int32_t header_word;                 // +0, unread by the lookups
    std::int32_t count;                       // +4, number of live slots, signed
    std::int32_t slot_flag_a[kCapacity];      // +8, read by 0x002519B0 / 0x00251EF0 on a hit
    std::int32_t slot_flag_b[kCapacity];      // +520, read by 0x00251920 on a hit
    std::uint64_t keys[kCapacity];            // +1032, the 64-bit ids searched here
};

static_assert(offsetof(KeyTable, count) == 4);
static_assert(offsetof(KeyTable, slot_flag_a) == 8);
static_assert(offsetof(KeyTable, slot_flag_b) == 520);
static_assert(offsetof(KeyTable, keys) == 1032);
static_assert(sizeof(KeyTable) == 2056);

// Returns the lowest index i in [0, table.count) with table.keys[i] == key, or -1 if there is none.
// The 32-bit result is what the retail function leaves in its result register before sign extension.
std::int32_t find_key64_slot(const KeyTable &table, std::uint64_t key);

}
