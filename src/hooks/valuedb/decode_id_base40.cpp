// decode_id_base40, retail 0x0021AEF0. See the header for the description, evidence and status
// (draft, oracle pending). Pure 64-bit integer logic: no floating point, no memory writes.

#include "hooks/valuedb/decode_id_base40.hpp"

namespace b3::valuedb {

std::uint32_t decode_id_base40_digit(std::int8_t byte) {
    // Order matters only for '_', which lies inside the 'A' window and must be caught before it.
    // The subtractions are on the signed byte; every branch that reaches them has a byte of at least
    // 48, so the results are the non-negative values 3..44 and the width of the subtraction is moot.
    if (byte == 0x5F) return 39;                                  // '_'
    if (byte >= 97) return static_cast<std::uint32_t>(byte - 84); // 'a'..'z' and 0x7B..0x7F
    if (byte >= 65) return static_cast<std::uint32_t>(byte - 52); // 'A'..'Z' and 0x5B..0x5E, 0x60
    if (byte >= 48) return static_cast<std::uint32_t>(byte - 45); // '0'..'9' and 0x3A..0x40
    if (byte == 0x2F) return 2;                                   // '/'
    if (byte == 0x2D) return 1;                                   // '-'
    return 0;                                                     // space, punctuation, control, 0x80..0xFF
}

std::uint64_t decode_id_base40(const char *text) {
    // Retail multiplies by 40 as (v * 5) * 8 on the 64-bit register; in modular uint64_t arithmetic
    // that is exactly v * 40, including the wrap for digits of 40 and above.
    std::uint64_t value = 0;
    int i = 0;
    for (; i < kIdBase40Length; ++i) {
        // One signed byte per position. The explicit signed cast matters: a host whose char is
        // unsigned would otherwise send 0x80..0xFF into the 'a' range instead of giving digit 0.
        const std::int8_t byte = static_cast<std::int8_t>(static_cast<unsigned char>(text[i]));
        if (byte == 0) break;        // the NUL is read but contributes nothing and ends the characters
        value = value * 40u;
        value += decode_id_base40_digit(byte);
    }
    // Every position not covered by a character, the NUL position included, is a zero digit: the
    // value is shifted up by one base-40 place for each of them, (12 - n) times for an n-character
    // string. A string of twelve or more characters never reaches this loop.
    for (; i < kIdBase40Length; ++i) value = value * 40u;
    return value;
}

}  // namespace b3::valuedb
