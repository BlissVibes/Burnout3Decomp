// ee::ld / ee::st and ee::guest_ptr over the runtime's guest memory (the arrays come from tests/unit/support).

#include "check.hpp"
#include "ee/guest.hpp"

#include <cstring>

using namespace ee;

namespace {

struct Wheel;      // a guest struct we only know by offsets
}

template <>
struct ee::guest_size<Wheel> : std::integral_constant<std::uint32_t, 0xC0> {};

namespace {

void clear() {
    std::memset(ps2::g_ram, 0, ps2::kRamSize);
    std::memset(ps2::g_spr, 0, ps2::kScratchpadSize);
}

void scalars_are_little_endian_and_keep_their_width() {
    clear();
    st<u32>(0x1000, 0x11223344u);
    CHECK_EQ(ps2::g_ram[0x1000], 0x44);
    CHECK_EQ(ps2::g_ram[0x1003], 0x11);
    CHECK_EQ(ps2::g_ram[0x1004], 0x00);
    CHECK_EQ(ld<u32>(0x1000), 0x11223344u);
    CHECK_EQ(ld<u16>(0x1000), 0x3344u);
    CHECK_EQ(ld<u8>(0x1003), 0x11u);

    st<u8>(0x1010, 0xAB);
    st<u16>(0x1012, 0xBEEF);
    st<u64>(0x1018, 0x0102030405060708ull);
    CHECK_EQ(ld<u32>(0x1010), 0xBEEF00ABu);
    CHECK_EQ(ld<u64>(0x1018), 0x0102030405060708ull);
    CHECK_EQ(ld<u32>(0x101C), 0x01020304u);
    CHECK_EQ(ps2::g_ram[0x1011], 0x00);             // a byte store touches one byte
    CHECK_EQ(ps2::g_ram[0x1020], 0x00);
}

void signed_loads_sign_extend() {
    clear();
    st<u32>(0x2000, 0xFFFF80FFu);
    CHECK_EQ(ld<s8>(0x2000), -1);
    CHECK_EQ(ld<s8>(0x2001), -128);
    CHECK_EQ(ld<s16>(0x2000), -32513);
    CHECK_EQ(ld<s32>(0x2000), -32513);
    CHECK_EQ(ld<u8>(0x2001), 0x80u);
    st<s16>(0x2008, -2);
    CHECK_EQ(ld<u16>(0x2008), 0xFFFEu);
    st<s64>(0x2010, -2);
    CHECK_EQ(ld<u64>(0x2010), 0xFFFFFFFFFFFFFFFEull);
}

void floats_travel_as_bit_patterns() {
    clear();
    st<f32>(0x3000, f32::from_bits(0x00000001u));   // a denormal must not be normalised or flushed in transit
    CHECK_EQ(ld<u32>(0x3000), 0x00000001u);
    st<u32>(0x3004, 0x7FFFFFFFu);
    CHECK_EQ(ld<f32>(0x3004).bits, 0x7FFFFFFFu);
    st<f32>(0x3008, f32::lit(1.5f));
    CHECK_EQ(ld<u32>(0x3008), 0x3FC00000u);
}

void unaligned_and_scratchpad_addresses_work() {
    clear();
    st<u32>(0x4001, 0xCAFEF00Du);
    CHECK_EQ(ld<u32>(0x4001), 0xCAFEF00Du);
    CHECK_EQ(ps2::g_ram[0x4001], 0x0D);
    st<u32>(0x70000010u, 0x12345678u);
    CHECK_EQ(ps2::g_spr[0x10], 0x78);
    CHECK_EQ(ld<u16>(0x70000012u), 0x1234u);
    st<u8>(ps2::kRamSize - 1, 0x5A);                // the last byte of RAM
    CHECK_EQ(ld<u8>(ps2::kRamSize - 1), 0x5Au);
}

void quadwords_round_trip() {
    clear();
    ps2::Reg128 q{};
    q.d[0] = 0x1122334455667788ull;
    q.d[1] = 0x99AABBCCDDEEFF00ull;
    st<ps2::Reg128>(0x5000, q);
    CHECK_EQ(ld<u64>(0x5008), 0x99AABBCCDDEEFF00ull);
    const ps2::Reg128 back = ld<ps2::Reg128>(0x5000);
    CHECK_EQ(back.d[0], q.d[0]);
    CHECK_EQ(back.d[1], q.d[1]);
}

void pointers_load_store_and_step_by_the_guest_size() {
    clear();
    const guest_ptr<u32> p{0x6000};
    *p = 7u;
    p[1] = 8u;
    p[-1] = 6u;
    CHECK_EQ(ld<u32>(0x6000), 7u);
    CHECK_EQ(ld<u32>(0x6004), 8u);
    CHECK_EQ(ld<u32>(0x5FFC), 6u);
    CHECK_EQ(static_cast<u32>(*p) + static_cast<u32>(p[1]), 15u);
    CHECK_EQ((p + 3).address, 0x600Cu);
    CHECK_EQ((p - 2).address, 0x5FF8u);
    CHECK_EQ(p.load(), 7u);
    (p + 2).store(9u);
    CHECK_EQ(ld<u32>(0x6008), 9u);

    const guest_ptr<u8> bytes{0x6000};
    CHECK_EQ((bytes + 5).address, 0x6005u);
    const guest_ptr<s16> shorts{0x6000};
    CHECK_EQ((shorts + 5).address, 0x600Au);
    const guest_ptr<f32> floats{0x6100};
    floats[2] = f32::lit(2.0f);
    CHECK_EQ(ld<u32>(0x6108), 0x40000000u);
    CHECK_EQ(static_cast<f32>(floats[2]).bits, 0x40000000u);
    CHECK_EQ((floats + 1).address, 0x6104u);
}

void pointers_to_pointers_are_32_bit() {
    clear();
    st<u32>(0x7000, 0x00007100u);
    st<u32>(0x7004, 0xDEADBEEFu);                    // must not be read as the high half of a pointer
    st<u8>(0x7100, 0x42);
    const guest_ptr<guest_ptr<u8>> pp{0x7000};
    const guest_ptr<u8> inner = *pp;
    CHECK_EQ(inner.address, 0x7100u);
    CHECK_EQ(static_cast<u8>(*inner), 0x42u);
    CHECK_EQ((pp + 1).address, 0x7004u);
    pp[1] = guest_ptr<u8>{0x7200};
    CHECK_EQ(ld<u32>(0x7004), 0x7200u);
}

void null_compares_and_tests_false() {
    const guest_ptr<u32> none{};
    const guest_ptr<u32> some{0x8000};
    CHECK(!none);
    CHECK(some);
    CHECK(none == guest_ptr<u32>{0});
    CHECK(some != none);
    CHECK(some == guest_ptr<u32>{0x8000});
}

void struct_fields_are_reached_by_offset() {
    clear();
    const guest_ptr<Wheel> wheel{0x9820};            // wheels: stride 0xC0
    *wheel.at<f32>(0x74) = f32::lit(0.25f);
    CHECK_EQ(ld<u32>(0x9894), 0x3E800000u);
    CHECK_EQ((wheel + 1).address, 0x98E0u);
    *(wheel + 2).at<u8>(0xB3) = u8{1};
    CHECK_EQ(ps2::g_ram[0x9820 + 2 * 0xC0 + 0xB3], 1);
    CHECK_EQ(wheel.at<u8>(0xB3).address, 0x98D3u);

    const guest_ptr<void> opaque{0x9820};
    CHECK_EQ(opaque.at<f32>(0x74).load().bits, 0x3E800000u);
    CHECK_EQ(opaque.cast<Wheel>().address, 0x9820u);
    CHECK_EQ(wheel.cast<u8>()[0x74 + 3], 0x3Eu);
}

}

int main() {
    scalars_are_little_endian_and_keep_their_width();
    signed_loads_sign_extend();
    floats_travel_as_bit_patterns();
    unaligned_and_scratchpad_addresses_work();
    quadwords_round_trip();
    pointers_load_store_and_step_by_the_guest_size();
    pointers_to_pointers_are_32_bit();
    null_compares_and_tests_false();
    struct_fields_are_reached_by_offset();
    return check::done("test_guest");
}
