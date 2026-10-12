// Standalone test for b3::valuedb::valuedb_hash_key (retail 0x0021BFA0). Returns 0 on success,
// non-zero with a message on failure.
//
// How the expected values were derived (never from running the game):
//  * The table is the reflected CRC-32 table for polynomial 0xEDB88320. Its first entries and its
//    last one are classic published constants that follow from the polynomial by hand: entry 0 is 0,
//    entry 1 is 0x77073096, entry 2 is 0xEE0E612C, entry 128 is the polynomial itself, entry 255 is
//    0x2D02EF8D. The whole table is also recomputed here by the 8-step bitwise rule and checked for
//    CRC linearity (table[a ^ b] == table[a] ^ table[b]).
//  * The specification says each byte step is: index = byte XOR low byte of state; state = (state
//    arithmetically shifted right by 8) XOR table[index], starting from -1, no final XOR. Two
//    independently written models of that rule are compared against the implementation:
//    reference_hash (written as "textbook step XOR the sign fill", 32-bit) and ee_model_hash (the
//    same rule written with the widths the console uses: 64-bit working values, each key byte
//    read as a signed 8-bit value, 32-bit arithmetic for the shift and the table offset, the table
//    word sign-extended when fetched, a signed loop compare against a 32-bit length), compared on
//    the full 64-bit result.
//  * The fixed vectors were worked out from the rule with a small script over the recomputed table
//    (for "a": index = 0x61 ^ 0xFF = 0x9E, state = (-1 >> 8) ^ table[0x9E] = ~table[0x9E] =
//    0xE84841BC). Textbook no-invert CRC-32 values are listed where they differ, as regression
//    guards against a uint32_t/logical-shift reimplementation. The multi-word keys are made up for
//    this test; none is a key the game uses.
//  * Keys with bytes >= 0x80 are out of domain (the retail result depends on unrelated library data
//    that is not in this repo); the tests only check the domain report and the documented fallback,
//    never a retail value for such keys.

#include "hooks/valuedb/valuedb_hash_key.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using b3::valuedb::valuedb_hash_key;
using b3::valuedb::valuedb_hash_key_is_in_domain;
using b3::valuedb::valuedb_hash_table_entry;

namespace {

int failures = 0;

void expect_u32(const char *what, std::uint32_t got, std::uint32_t want) {
    if (got != want) {
        std::printf("FAIL %s: got 0x%08X want 0x%08X\n", what, got, want);
        ++failures;
    }
}

void expect_u64(const char *what, std::uint64_t got, std::uint64_t want) {
    if (got != want) {
        std::printf("FAIL %s: got 0x%016llX want 0x%016llX\n", what, static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
        ++failures;
    }
}

void expect_true(const char *what, bool ok) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

std::uint32_t hash_u32(const char *key) { return static_cast<std::uint32_t>(valuedb_hash_key(key)); }

// The implementation's result as the EE would hold it in v0: the 32-bit value sign-extended to 64.
std::uint64_t hash_v0(const char *key) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(valuedb_hash_key(key)));
}

// Textbook reflected CRC-32 update (unsigned, logical shift, no final XOR), start -1.
std::uint32_t textbook_hash(const char *key) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char *p = key; *p; ++p) {
        const std::uint8_t idx = static_cast<std::uint8_t>((crc ^ static_cast<std::uint8_t>(*p)) & 0xFFu);
        crc = (crc >> 8) ^ valuedb_hash_table_entry(idx);
    }
    return crc;
}

// Independent model of the specification: the textbook step, with the top byte filled with ones
// whenever the previous state had bit 31 set (that is what an arithmetic shift does).
std::uint32_t reference_hash(const char *key) {
    std::uint32_t state = 0xFFFFFFFFu;
    for (const char *p = key; *p; ++p) {
        const std::uint8_t idx = static_cast<std::uint8_t>((state ^ static_cast<std::uint8_t>(*p)) & 0xFFu);
        const std::uint32_t fill = (state & 0x80000000u) ? 0xFF000000u : 0u;
        state = ((state >> 8) | fill) ^ valuedb_hash_table_entry(idx);
    }
    return state;
}

// One textbook step with the sign fill, used for the step-wise property check.
std::uint32_t one_step(std::uint32_t state, std::uint8_t byte) {
    const std::uint8_t idx = static_cast<std::uint8_t>((state ^ byte) & 0xFFu);
    const std::uint32_t fill = (state & 0x80000000u) ? 0xFF000000u : 0u;
    return ((state >> 8) | fill) ^ valuedb_hash_table_entry(idx);
}

// Helper: a 32-bit result as the console holds it in a 64-bit general register (sign-extended).
std::int64_t sx32(std::uint32_t w) { return static_cast<std::int64_t>(static_cast<std::int32_t>(w)); }

// The specification's rule written at the console's widths (64-bit working values, 32-bit
// arithmetic where the original is 32-bit), independently of the implementation. Only valid for
// keys whose bytes are all below 0x80: a byte with the top bit set is read as a negative value, the
// table offset it produces lies below the table, and the word there is library data we do not
// have; the model reports such a key by setting *out_of_domain.
std::int64_t ee_model_hash(const char *key, bool *out_of_domain) {
    *out_of_domain = false;
    // The length is a 32-bit pointer difference, held sign-extended.
    const std::int64_t length = sx32(static_cast<std::uint32_t>(std::strlen(key)));
    std::int64_t state = -1;     // all 64 bits set
    std::int64_t position = 0;   // compared with the length as signed 64-bit values
    const char *cursor = key;
    while (position < length) {
        // The key byte is read as a signed 8-bit value and widened to 64 bits.
        const std::int64_t byte = static_cast<std::int8_t>(*cursor);
        // The low byte of the state is taken before the shift; the shift itself is a 32-bit
        // arithmetic shift whose result is sign-extended back to 64 bits.
        const std::int64_t low = state & 0xFF;
        state = sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(state)) >> 8));
        position = sx32(static_cast<std::uint32_t>(position) + 1u);   // 32-bit counter, sign-extended
        ++cursor;
        // The table index is the 64-bit XOR of the signed byte and the low byte, turned into a byte
        // offset with a 32-bit shift and added to the table's address in 32 bits. Expressed as a
        // word index that is simply the signed value itself; it must land inside the table.
        const std::int64_t index = sx32(static_cast<std::uint32_t>(byte ^ low));
        if (index < 0 || index > 255) {
            *out_of_domain = true;
            return 0;
        }
        // The table word is fetched as a 32-bit value and sign-extended, then XORed in at 64 bits.
        state ^= sx32(valuedb_hash_table_entry(static_cast<std::uint8_t>(index)));
    }
    return state;   // returned as the full 64-bit value
}

// Compares the implementation against the console-width model on the full 64-bit result.
void expect_matches_model(const char *what, const std::string &key) {
    bool ood = false;
    const std::int64_t want = ee_model_hash(key.c_str(), &ood);
    if (ood) {
        std::printf("FAIL %s: model says key is out of domain\n", what);
        ++failures;
        return;
    }
    const std::int64_t got = static_cast<std::int64_t>(valuedb_hash_key(key.c_str()));
    if (got != want) {
        std::printf("FAIL %s: got 0x%016llX want 0x%016llX\n", what, static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
        ++failures;
    }
}

// Tiny deterministic generator so the random keys are reproducible without any library.
std::uint32_t lcg(std::uint32_t &s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

}  // namespace

int main() {
    // Table sanity: derived from the polynomial, not from the ELF.
    expect_u32("table[0]", valuedb_hash_table_entry(0), 0x00000000u);
    expect_u32("table[1]", valuedb_hash_table_entry(1), 0x77073096u);
    expect_u32("table[2]", valuedb_hash_table_entry(2), 0xEE0E612Cu);
    expect_u32("table[128]", valuedb_hash_table_entry(128), 0xEDB88320u);
    expect_u32("table[255]", valuedb_hash_table_entry(255), 0x2D02EF8Du);

    // Whole table recomputed here by the 8-step bitwise rule, and CRC linearity over all pairs.
    {
        std::uint32_t mine[256];
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int bit = 0; bit < 8; ++bit) c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
            mine[n] = c;
        }
        int table_mismatches = 0;
        for (int n = 0; n < 256; ++n) {
            if (valuedb_hash_table_entry(static_cast<std::uint8_t>(n)) != mine[n]) ++table_mismatches;
        }
        expect_true("all 256 table entries match the 8-step rule", table_mismatches == 0);
        int linearity_failures = 0;
        for (int a = 0; a < 256; ++a) {
            for (int b = 0; b < 256; ++b) {
                const std::uint32_t lhs = valuedb_hash_table_entry(static_cast<std::uint8_t>(a ^ b));
                const std::uint32_t rhs = valuedb_hash_table_entry(static_cast<std::uint8_t>(a)) ^
                                          valuedb_hash_table_entry(static_cast<std::uint8_t>(b));
                if (lhs != rhs) ++linearity_failures;
            }
        }
        expect_true("table is linear: table[a^b] == table[a]^table[b]", linearity_failures == 0);
    }

    // Empty key: zero iterations, start value returned unchanged; sign-extended to 64 bits it is
    // all ones, which is what a harness comparing the full v0 register must expect.
    expect_u32("empty", hash_u32(""), 0xFFFFFFFFu);
    expect_true("empty is -1", valuedb_hash_key("") == -1);
    expect_u64("empty sign-extends to all ones", hash_v0(""), 0xFFFFFFFFFFFFFFFFull);

    // First step from -1: index 0x61 ^ 0xFF = 0x9E, result ~table[0x9E].
    expect_u32("a vs ~table[0x9E]", hash_u32("a"), ~valuedb_hash_table_entry(0x9E));
    expect_u32("a", hash_u32("a"), 0xE84841BCu);
    expect_true("a differs from textbook 0x174841BC",
                hash_u32("a") != 0x174841BCu && textbook_hash("a") == 0x174841BCu);

    // Fixed vectors worked out from the rule.
    expect_u32("ab", hash_u32("ab"), 0x9E83B792u);
    expect_u32("abc", hash_u32("abc"), 0x3524413Du);
    expect_u32("Example Key", hash_u32("Example Key"), 0xD69FB90Fu);
    expect_u32("Sample/Entry", hash_u32("Sample/Entry"), 0x65B79227u);
    expect_true("Example Key differs from textbook 0xF92D620D",
                textbook_hash("Example Key") == 0xF92D620Du && hash_u32("Example Key") != 0xF92D620Du);

    // 64-bit extension as the EE returns it: positive results have a zero upper half, negative
    // results are sign-extended (never zero-extended).
    expect_u64("abc in v0 (positive, zero upper half)", hash_v0("abc"), 0x000000003524413Dull);
    expect_u64("Sample/Entry in v0 (positive, zero upper half)", hash_v0("Sample/Entry"), 0x0000000065B79227ull);
    expect_u64("a in v0 (sign-extended)", hash_v0("a"), 0xFFFFFFFFE84841BCull);
    expect_u64("ab in v0 (sign-extended)", hash_v0("ab"), 0xFFFFFFFF9E83B792ull);
    expect_u64("Example Key in v0 (sign-extended)", hash_v0("Example Key"), 0xFFFFFFFFD69FB90Full);

    // Boundary bytes of the ASCII domain.
    expect_u32("DEL (0x7F)", hash_u32("\x7F"), 0x12477CDFu);
    expect_u32("single 0x01", hash_u32("\x01"), reference_hash("\x01"));
    expect_u32("single space", hash_u32(" "), reference_hash(" "));
    expect_matches_model("tab/newline/return vs console-width model", std::string("\t\n\r"));
    expect_matches_model("0x7F * 300 vs console-width model", std::string(300, '\x7F'));
    expect_matches_model("0x01 * 300 vs console-width model", std::string(300, '\x01'));

    // An embedded NUL ends the key: the retail code loops strlen(key) times, and a leading NUL
    // makes it the empty key.
    expect_u32("embedded NUL", hash_u32("a\0b"), hash_u32("a"));
    expect_u32("leading NUL hashes as empty", hash_u32("\0abc"), 0xFFFFFFFFu);

    // Long keys: 100 'A's, 1000 'x's and 70000 'q's (loop counter well past 16 bits).
    {
        const std::string a100(100, 'A');
        const std::string x1000(1000, 'x');
        const std::string q70000(70000, 'q');
        expect_u32("A*100", hash_u32(a100.c_str()), 0xC076DE8Du);
        expect_u32("x*1000", hash_u32(x1000.c_str()), 0x20DCF6A2u);
        expect_u32("x*1000 vs reference", hash_u32(x1000.c_str()), reference_hash(x1000.c_str()));
        expect_u32("q*70000", hash_u32(q70000.c_str()), 0x6046B86Du);
        expect_matches_model("q*70000 vs console-width model", q70000);
    }

    // Every single ASCII byte and every ASCII pair against the console-width model (full 64-bit).
    {
        for (int b = 0x01; b <= 0x7F; ++b) {
            std::string key(1, static_cast<char>(b));
            expect_matches_model("single ASCII byte vs console-width model", key);
        }
        int pair_failures = 0;
        for (int b0 = 0x01; b0 <= 0x7F; ++b0) {
            for (int b1 = 0x01; b1 <= 0x7F; ++b1) {
                std::string key;
                key.push_back(static_cast<char>(b0));
                key.push_back(static_cast<char>(b1));
                bool ood = false;
                const std::int64_t want = ee_model_hash(key.c_str(), &ood);
                if (ood || static_cast<std::int64_t>(valuedb_hash_key(key.c_str())) != want) ++pair_failures;
            }
        }
        expect_true("all 16129 ASCII pairs match the console-width model", pair_failures == 0);
    }

    // Each step equals the textbook step XOR the sign fill: check against the independent model
    // over pseudo-random printable ASCII keys of varied length, including lengths 1 and 255.
    {
        std::uint32_t seed = 0xB3B3B3B3u;
        bool saw_negative_midway = false;
        for (int t = 0; t < 2000; ++t) {
            const int len = (t == 0) ? 1 : (t == 1) ? 255 : static_cast<int>(lcg(seed) % 64u) + 1;
            std::string key;
            for (int i = 0; i < len; ++i) key.push_back(static_cast<char>(0x20u + lcg(seed) % 0x5Fu));  // 0x20..0x7E
            if (hash_u32(key.c_str()) != reference_hash(key.c_str())) {
                std::printf("FAIL random key #%d (%s)\n", t, key.c_str());
                ++failures;
                break;
            }
            // The quirk only matters when some intermediate state is negative; make sure the
            // random set actually exercises that (any prefix hashing to a negative value counts).
            if (!saw_negative_midway && key.size() > 1) {
                const std::string prefix = key.substr(0, key.size() - 1);
                if (valuedb_hash_key(prefix.c_str()) < 0) saw_negative_midway = true;
            }
        }
        expect_true("random set exercises negative intermediate states", saw_negative_midway);
    }

    // 5000 pseudo-random full-ASCII keys (bytes 0x01..0x7F, control characters included), lengths
    // 1..300, compared on the full 64-bit value against the console-width model.
    {
        std::uint32_t seed = 0x5EED1234u;
        int random_failures = 0;
        for (int t = 0; t < 5000; ++t) {
            const int len = static_cast<int>(lcg(seed) % 300u) + 1;
            std::string key;
            for (int i = 0; i < len; ++i) key.push_back(static_cast<char>(0x01u + lcg(seed) % 0x7Fu));  // 0x01..0x7F
            bool ood = false;
            const std::int64_t want = ee_model_hash(key.c_str(), &ood);
            if (ood || static_cast<std::int64_t>(valuedb_hash_key(key.c_str())) != want) {
                if (random_failures == 0) std::printf("FAIL full-ASCII random key #%d (len %d)\n", t, len);
                ++random_failures;
            }
        }
        expect_true("5000 full-ASCII random keys match the console-width model", random_failures == 0);
    }

    // Sign-fill property over random prefixes: appending one byte equals the textbook step when the
    // prefix hash is non-negative, and the textbook step XOR 0xFF000000 when it is negative. Both
    // signs must actually occur in the sample.
    {
        std::uint32_t seed = 0xF1115EEDu;
        bool saw_negative = false, saw_non_negative = false;
        int property_failures = 0;
        for (int t = 0; t < 400; ++t) {
            const int len = static_cast<int>(lcg(seed) % 40u);
            std::string prefix;
            for (int i = 0; i < len; ++i) prefix.push_back(static_cast<char>(0x20u + lcg(seed) % 0x5Fu));
            const char next = static_cast<char>(0x20u + lcg(seed) % 0x5Fu);
            const std::uint32_t before = hash_u32(prefix.c_str());
            const std::uint8_t idx = static_cast<std::uint8_t>((before ^ static_cast<std::uint8_t>(next)) & 0xFFu);
            const std::uint32_t textbook_step = (before >> 8) ^ valuedb_hash_table_entry(idx);
            const std::uint32_t want = (before & 0x80000000u) ? (textbook_step ^ 0xFF000000u) : textbook_step;
            if (before & 0x80000000u) saw_negative = true; else saw_non_negative = true;
            const std::string extended = prefix + next;
            if (hash_u32(extended.c_str()) != want) ++property_failures;
            if (one_step(before, static_cast<std::uint8_t>(next)) != want) ++property_failures;
        }
        expect_true("sign-fill property holds over 400 random prefixes", property_failures == 0);
        expect_true("sign-fill sample exercised a negative prefix state", saw_negative);
        expect_true("sign-fill sample exercised a non-negative prefix state", saw_non_negative);
    }

    // Misaligned key pointers: the same key placed at offsets 0..15 of a 16-byte-aligned buffer
    // hashes identically (the retail strlen reads aligned chunks, but the hash reads bytes).
    {
        alignas(16) char buffer[64];
        for (int offset = 0; offset < 16; ++offset) {
            std::memset(buffer, 'Z', sizeof buffer);
            std::memcpy(buffer + offset, "Sample/Entry", 13);  // includes the NUL
            expect_u32("misaligned Sample/Entry", hash_u32(buffer + offset), 0x65B79227u);
        }
    }

    // Domain report boundaries.
    expect_true("ASCII keys are in domain",
                valuedb_hash_key_is_in_domain("Sample/Entry") && valuedb_hash_key_is_in_domain(""));
    expect_true("0x7F is in domain", valuedb_hash_key_is_in_domain("\x7F"));
    expect_true("0x80 is out of domain", !valuedb_hash_key_is_in_domain("\x80"));
    expect_true("NUL then 0x80 is in domain (NUL ends the key)", valuedb_hash_key_is_in_domain("\0\x80"));
    {
        const char f5_0f_87[] = { static_cast<char>(0xF5), static_cast<char>(0x0F), static_cast<char>(0x87), 0 };
        const char hi_ff_a[] = { static_cast<char>(0x80), static_cast<char>(0xFF), 'a', 0 };
        expect_true("F5 0F 87 is out of domain", !valuedb_hash_key_is_in_domain(f5_0f_87));
        expect_true("80 FF 61 is out of domain", !valuedb_hash_key_is_in_domain(hi_ff_a));
        bool ood = false;
        (void)ee_model_hash(f5_0f_87, &ood);
        expect_true("console-width model also flags F5 0F 87 as out of domain", ood);
    }

    // Out-of-domain bytes (>= 0x80): the retail result depends on unrelated ELF data, so we only
    // require our documented deterministic fallback (unsigned index) and the domain report. The
    // fallback for the single byte 0x80 is ~table[0x80 ^ 0xFF]; this is explicitly NOT the retail
    // value, which is never asserted here.
    {
        const char high[] = { static_cast<char>(0x80), static_cast<char>(0xFF), 'a', 0 };
        expect_true("high bytes are out of domain", !valuedb_hash_key_is_in_domain(high));
        expect_u32("high bytes use the unsigned index", hash_u32(high), reference_hash(high));
        expect_u32("high bytes are deterministic", hash_u32(high), hash_u32(high));
        expect_u32("documented fallback for 0x80 is ~table[0x7F]", hash_u32("\x80"), ~valuedb_hash_table_entry(0x7F));
    }

    if (failures != 0) {
        std::printf("test_valuedb_hash_key: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_valuedb_hash_key: all checks passed\n");
    return 0;
}
