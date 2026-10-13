// Standalone test for b3::score::boost_stats_as_ints (retail 0x0014E7E0).
//
// Expected values are derived from the specification, never from running the game. The rule, three
// times over, in this order:
//   record = guest pointer at object + 1380;  *out_k = cvt(float at record + {936, 932, 928})
// with cvt the EE's float-to-int conversion: truncation toward zero, 0 for |x| < 1 and for
// denormals, saturation to 0x7FFFFFFF / 0x80000000 from 2^31 up (exponent-255 patterns included).
// Each case names the property it pins. Guest memory comes from tests/unit/support.

#include "hooks/score/boost_stats_as_ints.hpp"

#include "check.hpp"
#include "ee/f32.hpp"
#include "ee/guest.hpp"

#include <cstdint>
#include <cstring>

using b3::score::boost_stats_as_ints;
using ee::f32;
using ee::u32;

namespace {

constexpr u32 kObject = 0x006481A0u;   // the address every known caller passes
constexpr u32 kRecord = 0x01234560u;
constexpr u32 kOut = 0x00100000u;      // three out words, 16 bytes apart, with guard words between

void clear() { std::memset(ps2::g_ram, 0, ps2::kRamSize); }

void lay_out(u32 first_bits, u32 second_bits, u32 third_bits, u32 object = kObject, u32 record = kRecord) {
    ee::st<u32>(object + 1380, record);
    ee::st<u32>(record + 936, first_bits);
    ee::st<u32>(record + 932, second_bits);
    ee::st<u32>(record + 928, third_bits);
    ee::st<u32>(record + 924, f32::lit(111.0f).bits);   // neighbours that must not be read
    ee::st<u32>(record + 940, f32::lit(222.0f).bits);
    for (u32 i = 0; i < 12; ++i) ee::st<u32>(kOut + 4 * i, 0xEEEEEEEEu);
}

void call(u32 object = kObject) {
    boost_stats_as_ints(ee::guest_ptr<void>{object}, ee::guest_ptr<std::int32_t>{kOut}, ee::guest_ptr<std::int32_t>{kOut + 16},
                        ee::guest_ptr<std::int32_t>{kOut + 32});
}

// Runs the getter over three bit patterns and checks the three ints and the guard words.
void expect(u32 a, u32 b, u32 c, u32 ia, u32 ib, u32 ic) {
    lay_out(a, b, c);
    call();
    CHECK_EQ(ee::ld<u32>(kOut), ia);
    CHECK_EQ(ee::ld<u32>(kOut + 16), ib);
    CHECK_EQ(ee::ld<u32>(kOut + 32), ic);
    for (u32 i = 0; i < 12; ++i)
        if (i % 4 != 0) CHECK_EQ(ee::ld<u32>(kOut + 4 * i), 0xEEEEEEEEu);
    // the source floats and the pointer are unchanged
    CHECK_EQ(ee::ld<u32>(kRecord + 936), a);
    CHECK_EQ(ee::ld<u32>(kRecord + 932), b);
    CHECK_EQ(ee::ld<u32>(kRecord + 928), c);
    CHECK_EQ(ee::ld<u32>(kObject + 1380), kRecord);
}

u32 bits(float x) { return f32::lit(x).bits; }

void field_order() {
    clear();
    // descending field offsets go to ascending out pointers: +936 -> first, +932 -> second, +928 -> third
    expect(bits(1.0f), bits(2.0f), bits(3.0f), 1, 2, 3);
    expect(bits(100.0f), bits(7.0f), bits(65536.0f), 100, 7, 65536);
}

void conversion() {
    clear();
    expect(bits(2.9f), bits(-1.5f), bits(-0.99f), 2, 0xFFFFFFFFu, 0);          // truncation toward zero; |x| < 1 gives 0
    expect(0u, 0x80000000u, 0x00000001u, 0, 0, 0);                            // both zeros and a denormal
    expect(0x807FFFFFu, bits(0.999999f), bits(1.0f), 0, 0, 1);                // negative denormal; just below 1; exactly 1
    expect(bits(2147483520.0f), bits(-2147483520.0f), bits(16777216.0f), 0x7FFFFF80u, 0x80000080u, 0x01000000u);   // the largest floats below 2^31
    expect(bits(2147483648.0f), bits(-2147483648.0f), bits(4294967296.0f), 0x7FFFFFFFu, 0x80000000u, 0x7FFFFFFFu); // saturation from 2^31
    expect(0x7F800000u, 0xFF800000u, 0x7FC00000u, 0x7FFFFFFFu, 0x80000000u, 0x7FFFFFFFu);   // exponent 255: huge finite values
    expect(0x7FFFFFFFu, 0xFFFFFFFFu, bits(-3.0e9f), 0x7FFFFFFFu, 0x80000000u, 0x80000000u);
    expect(bits(8388607.5f), bits(-8388607.5f), bits(123456.789f), 8388607, static_cast<u32>(-8388607), 123456);
}

void other_object_and_record() {
    clear();
    const u32 object = 0x00200010u, record = 0x01E00004u;
    lay_out(bits(11.0f), bits(22.0f), bits(33.0f), object, record);
    call(object);
    CHECK_EQ(ee::ld<u32>(kOut), 11u);
    CHECK_EQ(ee::ld<u32>(kOut + 16), 22u);
    CHECK_EQ(ee::ld<u32>(kOut + 32), 33u);
}

// The record pointer is re-read before every field, and each store lands before the next read.
void aliasing() {
    clear();
    // The first out pointer is the pointer field itself: step 1 stores int(5.0) = 5... so make the
    // stored int a valid second record address instead. 0x00300000 as a float converts to 3145728.
    const u32 second_record = 3145728u;   // 0x00300000
    lay_out(bits(3145728.0f), bits(1.0f), bits(2.0f));
    ee::st<u32>(second_record + 932, bits(70.0f));
    ee::st<u32>(second_record + 928, bits(80.0f));
    boost_stats_as_ints(ee::guest_ptr<void>{kObject}, ee::guest_ptr<std::int32_t>{kObject + 1380}, ee::guest_ptr<std::int32_t>{kOut + 16},
                        ee::guest_ptr<std::int32_t>{kOut + 32});
    CHECK_EQ(ee::ld<u32>(kObject + 1380), second_record);   // step 1 overwrote the pointer
    CHECK_EQ(ee::ld<u32>(kOut + 16), 70u);                  // steps 2 and 3 followed the new pointer
    CHECK_EQ(ee::ld<u32>(kOut + 32), 80u);

    // The first out pointer is the third field: step 3 converts the int step 1 stored there.
    clear();
    lay_out(bits(9.0f), bits(4.0f), bits(1000.0f));
    boost_stats_as_ints(ee::guest_ptr<void>{kObject}, ee::guest_ptr<std::int32_t>{kRecord + 928}, ee::guest_ptr<std::int32_t>{kOut + 16},
                        ee::guest_ptr<std::int32_t>{kOut + 32});
    CHECK_EQ(ee::ld<u32>(kRecord + 928), 9u);               // the int 9, which as a float is a denormal
    CHECK_EQ(ee::ld<u32>(kOut + 16), 4u);
    CHECK_EQ(ee::ld<u32>(kOut + 32), 0u);                   // cvt of that denormal is 0, not 1000

    // All three out pointers are the same word: the last store wins.
    clear();
    lay_out(bits(1.0f), bits(2.0f), bits(3.0f));
    boost_stats_as_ints(ee::guest_ptr<void>{kObject}, ee::guest_ptr<std::int32_t>{kOut}, ee::guest_ptr<std::int32_t>{kOut},
                        ee::guest_ptr<std::int32_t>{kOut});
    CHECK_EQ(ee::ld<u32>(kOut), 3u);
}

}

int main() {
    field_order();
    conversion();
    other_object_and_record();
    aliasing();
    return check::done("test_boost_stats_as_ints");
}
