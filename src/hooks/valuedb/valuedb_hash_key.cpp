// valuedb_hash_key, retail 0x0021BFA0. See the header for the description, evidence and status
// (draft, oracle pending). Pure integer logic: no floating point, no memory writes.

#include "hooks/valuedb/valuedb_hash_key.hpp"

#include <array>
#include <cstddef>
#include <cstring>

namespace b3::valuedb {

namespace {

// The reflected CRC-32 table, built at compile time from the polynomial: each entry is its index
// pushed through eight right-shift-and-conditionally-XOR steps.
constexpr std::array<std::uint32_t, 256> make_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int bit = 0; bit < 8; ++bit) c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
        table[n] = c;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kTable = make_table();

// One byte step. The state is signed and the shift is arithmetic, which is the whole point of
// keeping this as int32_t rather than the usual uint32_t: a negative state fills the top byte
// with ones before the table word is XORed in.
inline std::int32_t step(std::int32_t state, std::uint8_t index) {
    const std::int32_t shifted = state >> 8;   // arithmetic: C++20 defines >> on negative values
    return shifted ^ static_cast<std::int32_t>(kTable[index]);
}

}  // namespace

std::uint32_t valuedb_hash_table_entry(std::uint8_t index) { return kTable[index]; }

bool valuedb_hash_key_is_in_domain(const char *key) {
    for (const char *p = key; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80u) return false;
    }
    return true;
}

std::int32_t valuedb_hash_key(const char *key) {
    // The retail code measures the key first and then loops exactly that many times, so an embedded
    // NUL ends the hash just as it ends the string. The length comes back from the library strlen
    // as a 32-bit difference of pointers, read as a signed value, and the loop counter is compared
    // with a signed test, so a length of 0x80000000 or more would run zero iterations and return -1.
    // That cannot happen on a 32 MB console, but it is modelled here so the C++ states the retail
    // rule rather than a host-only one. `key` must not be null (see the header).
    const std::int32_t len = static_cast<std::int32_t>(static_cast<std::uint32_t>(std::strlen(key)));
    std::int32_t state = -1;
    for (std::int32_t i = 0; i < len; ++i) {
        // The index is the key byte XOR the low byte of the state before the shift. For ASCII bytes
        // this is exactly the retail index; for bytes >= 0x80 the retail index would be negative
        // (see the header), and we deliberately use the unsigned form instead.
        const std::uint8_t byte = static_cast<std::uint8_t>(key[i]);
        const std::uint8_t index = static_cast<std::uint8_t>(byte ^ (static_cast<std::uint32_t>(state) & 0xFFu));
        state = step(state, index);
    }
    return state;
}

}  // namespace b3::valuedb
