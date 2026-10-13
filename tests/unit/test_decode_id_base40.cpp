// decode_id_base40 (0x0021AEF0): every edge case of the specification with synthetic ids.
//
// Expected values are reasoned from the specification, never taken from the game. The rule is
//   key = sum over k = 0..11 of digit(text[k]) * 40^(11-k), digit = 0 at and after the first NUL,
// so a single character c decodes to digit(c) * 40^11, where 40^11 = 0x05D21DBA00000000, and a
// string of n < 12 characters is its fold over those characters times 40^(12-n). The hex constants
// below were produced by evaluating that formula with arbitrary-precision integers (our own model
// in the scratchpad), reduced modulo 2^64 where the sum exceeds it. A second, independent
// formulation of the same rule (`reference_key`, a positional sum with explicit powers instead of
// the running fold) is used for the random differential run and the per-byte digit table.

#include "check.hpp"
#include "hooks/valuedb/decode_id_base40.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

using b3::valuedb::decode_id_base40;
using b3::valuedb::decode_id_base40_digit;
using b3::valuedb::kIdBase40Length;

namespace {

constexpr std::uint64_t kPow40_11 = 0x05D21DBA00000000ull;   // 40^11, the weight of position 0

// 40^n for n = 0..11 (40^12 = 0xE8D4A51000000000 also fits, which is why in-alphabet ids never wrap).
std::uint64_t pow40(int n) {
    std::uint64_t p = 1;
    for (int k = 0; k < n; ++k) p *= 40u;
    return p;
}

// The digit table written out as sets, independently of the range arithmetic in the implementation.
std::uint32_t reference_digit(unsigned char b) {
    if (b >= 0x80) return 0;                                   // negative as the EE loads it
    if (b == '_') return 39;
    if (b >= 'a' && b <= 'z') return 13u + (b - 'a');
    if (b >= 'A' && b <= 'Z') return 13u + (b - 'A');
    if (b >= '0' && b <= '9') return 3u + (b - '0');
    if (b == '/') return 2;
    if (b == '-') return 1;
    if (b >= 0x7B) return 39u + (b - 0x7B);                    // '{' '|' '}' '~' DEL -> 39..43
    if (b >= 0x5B && b <= 0x5E) return 39u + (b - 0x5B);       // '[' '\' ']' '^' -> 39..42
    if (b == 0x60) return 44;                                  // '`'
    if (b >= 0x3A && b <= 0x40) return 13u + (b - 0x3A);       // ':' .. '@' -> 13..19
    return 0;                                                  // space, '.', ',', '+', controls, ...
}

// Positional-sum formulation over an explicit byte buffer of `size` bytes (the NUL rule applied).
std::uint64_t reference_key(const unsigned char *bytes, std::size_t size) {
    std::uint64_t key = 0;
    for (int k = 0; k < kIdBase40Length; ++k) {
        if (static_cast<std::size_t>(k) >= size || bytes[k] == 0) break;
        key += reference_digit(bytes[k]) * pow40(11 - k);     // modular, like the EE's 64-bit adds
    }
    return key;
}

std::uint64_t decode(const std::string &s) { return decode_id_base40(s.c_str()); }

void digit_table_matches_the_specification() {
    for (int b = 0; b < 256; ++b) {
        const std::int8_t signed_byte = static_cast<std::int8_t>(static_cast<unsigned char>(b));
        CHECK_EQ(decode_id_base40_digit(signed_byte), reference_digit(static_cast<unsigned char>(b)));
    }
    // Spot values from the specification's table.
    CHECK_EQ(decode_id_base40_digit('_'), 39u);
    CHECK_EQ(decode_id_base40_digit('a'), 13u);
    CHECK_EQ(decode_id_base40_digit('z'), 38u);
    CHECK_EQ(decode_id_base40_digit('A'), 13u);
    CHECK_EQ(decode_id_base40_digit('Z'), 38u);
    CHECK_EQ(decode_id_base40_digit('0'), 3u);
    CHECK_EQ(decode_id_base40_digit('9'), 12u);
    CHECK_EQ(decode_id_base40_digit('/'), 2u);
    CHECK_EQ(decode_id_base40_digit('-'), 1u);
    CHECK_EQ(decode_id_base40_digit(' '), 0u);
    CHECK_EQ(decode_id_base40_digit('.'), 0u);
    CHECK_EQ(decode_id_base40_digit(','), 0u);
    CHECK_EQ(decode_id_base40_digit('+'), 0u);
    CHECK_EQ(decode_id_base40_digit('\t'), 0u);
    CHECK_EQ(decode_id_base40_digit(0x7F), 43u);              // DEL
    CHECK_EQ(decode_id_base40_digit('{'), 39u);
    CHECK_EQ(decode_id_base40_digit('~'), 42u);
    CHECK_EQ(decode_id_base40_digit('['), 39u);
    CHECK_EQ(decode_id_base40_digit('^'), 42u);
    CHECK_EQ(decode_id_base40_digit('`'), 44u);               // the largest reachable digit
    CHECK_EQ(decode_id_base40_digit(':'), 13u);               // aliases 'a'
    CHECK_EQ(decode_id_base40_digit('@'), 19u);               // aliases 'g'
    CHECK_EQ(decode_id_base40_digit(static_cast<std::int8_t>(-128)), 0u);   // 0x80
    CHECK_EQ(decode_id_base40_digit(static_cast<std::int8_t>(-1)), 0u);     // 0xFF
    CHECK_EQ(decode_id_base40_digit(static_cast<std::int8_t>(0xE1)), 0u);   // 'a' with the top bit set is NOT 'a'
    // The '_' test comes before the 'A' range: 0x5F - 52 would be 43, the function says 39.
    CHECK(decode_id_base40_digit('_') != 43u);
}

void single_characters_sit_at_the_top_position() {
    CHECK_EQ(decode(""), 0ull);                                // empty: twelve zero digits
    CHECK_EQ(decode("a"), 13u * kPow40_11);
    CHECK_EQ(decode("a"), 0x4BAB827200000000ull);             // the specification's own example
    CHECK_EQ(decode("z"), 38u * kPow40_11);
    CHECK_EQ(decode("z"), 0xDD30699C00000000ull);             // bit 63 set: a true 64-bit result
    CHECK_EQ(decode("z") >> 63, 1ull);
    CHECK_EQ(decode("Z"), 0xDD30699C00000000ull);
    CHECK_EQ(decode("_"), 39u * kPow40_11);
    CHECK_EQ(decode("_"), 0xE302875600000000ull);
    CHECK_EQ(decode("/"), 2u * kPow40_11);
    CHECK_EQ(decode("/"), 0x0BA43B7400000000ull);
    CHECK_EQ(decode("-"), 1u * kPow40_11);
    CHECK_EQ(decode("-"), 0x05D21DBA00000000ull);
    CHECK_EQ(decode("0"), 3u * kPow40_11);
    CHECK_EQ(decode("0"), 0x1176592E00000000ull);
    CHECK_EQ(decode("9"), 12u * kPow40_11);
    CHECK_EQ(decode("9"), 0x45D964B800000000ull);
    CHECK_EQ(decode(" "), 0ull);                               // space is digit 0, so one space = empty
    CHECK_EQ(decode("."), 0ull);
    // The low 32 bits of every single-character key are zero (40^11 is a multiple of 2^33), so a
    // 32-bit truncation of the return would collapse all of them to 0.
    CHECK_EQ(static_cast<std::uint32_t>(decode("a")), 0u);
    CHECK(decode("a") != 0ull);
}

void letters_fold_case_by_construction() {
    for (int c = 'a'; c <= 'z'; ++c) {
        const char lower[2] = {static_cast<char>(c), 0};
        const char upper[2] = {static_cast<char>(c - 'a' + 'A'), 0};
        CHECK_EQ(decode_id_base40(lower), decode_id_base40(upper));
    }
    CHECK_EQ(decode("TRACK_01"), decode("track_01"));
    CHECK_EQ(decode("TRACK_01"), decode("Track_01"));
    CHECK_EQ(decode("TRACK_01"), 0xBEADC5F75CC7C000ull);
    // Case folding is not tolower: digits and punctuation are unaffected, and distinct letters stay distinct.
    CHECK(decode("a") != decode("b"));
    CHECK(decode("ab") != decode("ba"));
}

void shorter_strings_are_padded_over_every_remaining_position() {
    // "ABC" folds to (13*40 + 14)*40 + 15 = 21,375 and is then multiplied by 40 exactly 9 times
    // (positions 3..11, the NUL position among them).
    const std::uint64_t fold_abc = (13u * 40u + 14u) * 40u + 15u;
    CHECK_EQ(fold_abc, 21375ull);
    CHECK_EQ(decode("ABC"), fold_abc * pow40(9));
    CHECK_EQ(decode("ABC"), 0x4DC30520D8000000ull);
    // An implementation that padded only 11 - n times or not at all would give these instead.
    CHECK(decode("ABC") != fold_abc * pow40(8));
    CHECK(decode("ABC") != fold_abc);
    // Trailing spaces are invisible: pad digit and space digit are both 0.
    CHECK_EQ(decode("ABC"), decode("ABC "));
    CHECK_EQ(decode("ABC"), decode("ABC         "));           // padded to exactly 12 with spaces
    CHECK_EQ(decode("ABC"), decode("ABC             "));       // longer than 12: bytes past 11 are not read
    // Leading spaces are not invisible: they move the characters down.
    CHECK(decode(" ABC") != decode("ABC"));
    CHECK_EQ(decode("           a"), 13ull);                   // eleven spaces then 'a': digit 13 at the units place
    CHECK_EQ(decode("          a"), 13ull * 40ull);            // ten spaces then 'a', then one pad place
    CHECK_EQ(decode("          a"), 0x208ull);
    // Every prefix length of a 12-character id obeys fold(prefix) * 40^(12-n).
    const std::string full = "ab/cd-ef_gh0";
    std::uint64_t fold = 0;
    for (std::size_t n = 0; n <= full.size(); ++n) {
        CHECK_EQ(decode(full.substr(0, n)), fold * pow40(static_cast<int>(12 - n)));
        if (n < full.size()) fold = fold * 40u + reference_digit(static_cast<unsigned char>(full[n]));
    }
    CHECK_EQ(decode(full), 0x4DB7457BB7CC2FE3ull);
}

void only_the_first_twelve_bytes_are_read() {
    // A 12-byte buffer with no terminator, followed by garbage that would change the result if read.
    unsigned char buffer[20];
    std::memcpy(buffer, "0123456789-/", 12);
    std::memset(buffer + 12, 'Z', sizeof(buffer) - 12);        // digit 38 everywhere after the id
    const std::uint64_t expected = 0x12102914EF00292Aull;      // formula over exactly the 12 bytes
    CHECK_EQ(decode_id_base40(reinterpret_cast<const char *>(buffer)), expected);
    CHECK_EQ(decode("0123456789-/"), expected);
    CHECK_EQ(decode("0123456789-/abcdefgh"), expected);        // longer NUL-terminated string: same
    std::memset(buffer + 12, 0xFF, sizeof(buffer) - 12);
    CHECK_EQ(decode_id_base40(reinterpret_cast<const char *>(buffer)), expected);
    // A NUL at byte 12 exactly is never read either (the loop ends at twelve characters first).
    std::memset(buffer + 12, 0, sizeof(buffer) - 12);
    CHECK_EQ(decode_id_base40(reinterpret_cast<const char *>(buffer)), expected);
    // The first NUL stops the characters: bytes after it never count.
    const char embedded[] = {'a', 'b', 0, 'z', 'z', 'z', 'z', 'z', 'z', 'z', 'z', 'z', 'z', 0};
    CHECK_EQ(decode_id_base40(embedded), decode("ab"));
    // A buffer that is all non-NUL for 12 bytes right at the end of an allocation.
    std::string tail(12, 'z');
    CHECK_EQ(decode_id_base40(tail.data()), 0xE2DC520F96F96F96ull);
}

void high_bytes_are_negative_and_decode_as_zero() {
    const char high1[] = {static_cast<char>(0x80), 0};
    const char high2[] = {static_cast<char>(0xFF), 0};
    const char high3[] = {static_cast<char>(0xE1), 0};         // 'a' | 0x80
    CHECK_EQ(decode_id_base40(high1), 0ull);
    CHECK_EQ(decode_id_base40(high2), 0ull);
    CHECK_EQ(decode_id_base40(high3), 0ull);
    // An unsigned-byte implementation would have treated 0xE1 as 0xE1 - 84 = 141; make sure not.
    CHECK(decode_id_base40(high3) != 141u * kPow40_11);
    // Inside a string a high byte behaves exactly like a space at that position.
    const char mixed[] = {'a', static_cast<char>(0xFF), 'b', 0};
    CHECK_EQ(decode_id_base40(mixed), decode("a b"));
    const char all_high[] = {static_cast<char>(0x80), static_cast<char>(0x90), static_cast<char>(0xC3),
                             static_cast<char>(0xFF), 0};
    CHECK_EQ(decode_id_base40(all_high), 0ull);
    // High bytes still count as characters (they are not NUL), so the position advances past them.
    const char pushed[] = {static_cast<char>(0xFF), 'a', 0};
    CHECK_EQ(decode_id_base40(pushed), 13u * pow40(10));
}

void out_of_alphabet_ascii_aliases_and_wraps() {
    CHECK_EQ(decode(":"), decode("a"));                        // 0x3A - 45 = 13
    CHECK_EQ(decode("@"), decode("g"));                        // 0x40 - 45 = 19
    CHECK_EQ(decode("@"), 0x6E9834CE00000000ull);
    CHECK_EQ(decode("["), decode("_"));                        // 0x5B - 52 = 39
    CHECK_EQ(decode("{"), decode("_"));                        // 0x7B - 84 = 39
    CHECK_EQ(decode("`"), 44u * kPow40_11);                    // 0x60 - 52 = 44
    CHECK_EQ(decode("`"), 0x001D1BF800000000ull);
    const char del[] = {0x7F, 0};
    CHECK_EQ(decode_id_base40(del), 43u * kPow40_11);
    CHECK_EQ(decode_id_base40(del), 0xFA4AFE3E00000000ull);
    // Twelve digit-44 bytes: 44 * (40^12 - 1) / 39 = 0x106AE44120D20D20C exceeds 2^64 and wraps.
    CHECK_EQ(decode(std::string(12, '`')), 0x06AE44120D20D20Cull);
    // Twelve digit-42 bytes ('~') also wrap.
    CHECK_EQ(decode(std::string(12, '~')), 0xFABD9E113B13B13Aull);
    // Twelve in-alphabet maximal digits ('z' = 38) do not wrap: 38 * (40^12 - 1) / 39 < 2^64.
    CHECK_EQ(decode(std::string(12, 'z')), 0xE2DC520F96F96F96ull);
    CHECK_EQ(decode(std::string(12, 'z')), decode(std::string(12, 'Z')));
}

void whole_ids_against_the_positional_formula() {
    const char *samples[] = {
        "", "a", "ab", "abc", "ABC_", "TRACK_01", "track_01", "0123456789-/", "ab/cd-ef_gh0",
        "____________", "------------", "////////////", "000000000000", "999999999999",
        "zzzzzzzzzzzz", "ZZZZZZZZZZZZ", "            ", "a b c d e f ", "x.y,z+w", "/-/-/-/-/-/-",
    };
    for (const char *s : samples) {
        CHECK_EQ(decode_id_base40(s), reference_key(reinterpret_cast<const unsigned char *>(s), std::strlen(s)));
    }
    CHECK_EQ(decode("____________"), decode(std::string(12, '[')));   // 39 either way
    CHECK_EQ(decode("            "), 0ull);
}

// Differential run: random buffers of length 0..20 over a pool that includes every interesting byte
// class (alphabet, separators, out-of-alphabet ranges, controls, high bytes, embedded NULs) compared
// with the positional formulation. A small LCG keeps the run reproducible.
void random_buffers_match_the_positional_formula() {
    std::uint32_t state = 0x2E5F2D30u;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    static const unsigned char pool[] = {
        'a', 'b', 'm', 'y', 'z', 'A', 'N', 'Z', '0', '5', '9', '_', '/', '-', ' ', '.', ',', '+',
        ':', ';', '@', '[', '\\', ']', '^', '`', '{', '|', '}', '~', 0x7F, 0x01, 0x1F,
        0x80, 0x9F, 0xC0, 0xE1, 0xFF, 0x00,
    };
    const std::size_t pool_size = sizeof(pool);
    for (int round = 0; round < 20000; ++round) {
        unsigned char buffer[24];
        const std::size_t length = next() % 21;              // 0..20 bytes before the terminator
        for (std::size_t k = 0; k < length; ++k) buffer[k] = pool[next() % pool_size];
        buffer[length] = 0;
        for (std::size_t k = length + 1; k < sizeof(buffer); ++k) buffer[k] = pool[next() % pool_size];
        CHECK_EQ(decode_id_base40(reinterpret_cast<const char *>(buffer)), reference_key(buffer, length));
        // Full random bytes too (every value 0..255 equally likely), 12 bytes with no terminator guaranteed.
        unsigned char raw[16];
        for (unsigned char &b : raw) b = static_cast<unsigned char>(next());
        for (int k = 0; k < 12; ++k) if (raw[k] == 0) raw[k] = 0x80;   // keep this one terminator-free
        CHECK_EQ(decode_id_base40(reinterpret_cast<const char *>(raw)), reference_key(raw, 12));
    }
}

void misaligned_pointers_are_fine() {
    // The retail code reads single bytes, so any alignment of the string is legal.
    char storage[32];
    std::memset(storage, 0, sizeof(storage));
    for (int offset = 0; offset < 8; ++offset) {
        std::memcpy(storage + offset, "Burn_3", 7);
        CHECK_EQ(decode_id_base40(storage + offset), decode("Burn_3"));
        std::memset(storage, 0, sizeof(storage));
    }
}

}  // namespace

int main() {
    digit_table_matches_the_specification();
    single_characters_sit_at_the_top_position();
    letters_fold_case_by_construction();
    shorter_strings_are_padded_over_every_remaining_position();
    only_the_first_twelve_bytes_are_read();
    high_bytes_are_negative_and_decode_as_zero();
    out_of_alphabet_ascii_aliases_and_wraps();
    whole_ids_against_the_positional_formula();
    random_buffers_match_the_positional_formula();
    misaligned_pointers_are_fine();
    return check::done("test_decode_id_base40");
}
