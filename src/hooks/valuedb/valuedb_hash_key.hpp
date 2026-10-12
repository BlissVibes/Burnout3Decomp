#pragma once

// valuedb_hash_key: hashes a NUL-terminated key string for the game's named-value database.
//
// Retail address 0x0021BFA0 (game code). Calls only the Sony libc strlen (0x001273E8); no stores
// except its own stack frame, no floating point, no globals written.
// Evidence: the recompiler's generated C++ for unit 0x0046 (labels 0x0021BFA0-0x0021BFF0) and the
// epilogue in unit 0x0047 (label 0x0021C000) read in full and cross-checked against the reader's
// specification; the 256-word table the retail code reads at 0x0049B110 was compared word for word
// with the table recomputed from the reflected CRC-32 polynomial 0xEDB88320 (all equal), so the
// table is recomputed here and no retail data is copied. Status: draft, oracle pending.
//
// Behaviour. A table-driven CRC-32 pass over the key's bytes (strlen(key) of them), starting from
// -1 with no final inversion, with one deliberate-looking quirk: the running value is a SIGNED
// 32-bit integer and the per-byte shift is arithmetic, so whenever the previous value is negative
// the top eight bits are filled with ones before the table word is XORed in. Each step therefore
// equals the textbook reflected CRC-32 step XOR (previous value negative ? 0xFF000000 : 0). Since
// the start value is -1 the very first step already differs from textbook CRC-32, so a uint32_t
// implementation with a logical shift would be wrong for every non-empty key.
// The empty key hashes to -1 (0xFFFFFFFF). Bytes of the key are read as signed chars; for all
// ASCII keys the table index is 0..255. See the note on out-of-domain bytes below.

#include <cstdint>

namespace b3::valuedb {

// Hashes `key` (NUL-terminated, host pointer onto the guest string). Result is the signed 32-bit
// state described above; on the EE it is returned sign-extended to 64 bits.
//
// Preconditions and corners. `key` must not be null: the retail code would hand address 0 to the
// library strlen and hash whatever bytes sit there (kernel memory we do not model); here a null key
// is undefined behaviour, as with std::strlen. The length is taken as a signed 32-bit value and the
// loop uses a signed compare, exactly as retail does, so a key of 2^31 bytes or more would hash to
// -1; unreachable on the console, modelled for fidelity only.
//
// Out of domain: a key byte >= 0x80 is sign-extended by the retail code and gives a negative table
// index, so the retail game reads one of the 256 unrelated words that happen to sit just below the
// table (library read-only data) and its result depends on that data, which we neither copy nor
// model. All real keys are ASCII. For such bytes this reimplementation instead uses the index with
// the sign extension stripped (the unsigned byte XOR the low byte of the state, 0..255), which is
// deterministic but is NOT the retail result. `valuedb_hash_key_is_in_domain` reports whether a
// key is free of such bytes.
std::int32_t valuedb_hash_key(const char *key);

// True when every byte of the NUL-terminated key is below 0x80, i.e. the hash matches retail.
bool valuedb_hash_key_is_in_domain(const char *key);

// One entry of the reflected CRC-32 table (polynomial 0xEDB88320) this hash is driven by, recomputed
// in code. Exposed so tests can check the table against values derived from the polynomial.
std::uint32_t valuedb_hash_table_entry(std::uint8_t index);

}
