// find_key64_slot, retail 0x002518E0: first-match linear search of a 64-bit id registry.
// Evidence and layout notes are in find_key64_slot.hpp. Status: draft, oracle pending.

#include "hooks/registry/find_key64_slot.hpp"

namespace b3::registry {

std::int32_t find_key64_slot(const KeyTable &table, std::uint64_t key) {
    // The count is signed: zero or negative means nothing is examined and the key is absent.
    // The compare is on the whole 64-bit key; a value that differs only in its upper half does not
    // match. The first matching slot wins when a key appears more than once.
    const std::int32_t count = table.count;
    for (std::int32_t i = 0; i < count; ++i) {
        if (table.keys[i] == key) return i;
    }
    return -1;
}

}
