// ee::abi: reading arguments from and writing results to the register file the way the game's code does.
// Integers arrive in a0-a3 then t0-t3, floats in f12-f19, results leave in v0/v1/f0; a 32-bit result is
// sign-extended to 64 bits unless the original ended in andi/ori (docs/re/function-harness-spec.md).

#include "check.hpp"
#include "ee/abi.hpp"

#include <cstring>

using namespace ee;

namespace {

ps2::Cpu fresh() {
    ps2::Cpu c;
    std::memset(static_cast<void *>(&c), 0, sizeof c);
    for (int i = 0; i < 32; ++i) {
        c.r[i].d[0] = 0x1111111100000000ull * static_cast<unsigned>(i % 15 + 1) + 0x100u + static_cast<unsigned>(i);
        c.r[i].d[1] = 0xABCD000000000000ull + static_cast<unsigned>(i);
        c.f[i] = 0x40000000u + static_cast<unsigned>(i);
    }
    return c;
}

void integer_arguments_come_from_a0_to_t3() {
    ps2::Cpu c = fresh();
    for (int i = 0; i < 8; ++i) c.r[4 + i].d[0] = 0x1000u + static_cast<unsigned>(i);
    for (int i = 0; i < 8; ++i) CHECK_EQ(abi::arg<u32>(c, i), 0x1000u + static_cast<unsigned>(i));
    CHECK_EQ(abi::arg<u32>(c, 4), static_cast<u32>(c.r[8].w[0]));      // the fifth is t0, not a stack slot
}

void arguments_are_narrowed_to_the_asked_type() {
    ps2::Cpu c = fresh();
    c.r[4].d[0] = 0xFFFFFFFF80000001ull;
    CHECK_EQ(abi::arg<u32>(c, 0), 0x80000001u);
    CHECK_EQ(abi::arg<s32>(c, 0), INT32_MIN + 1);
    CHECK_EQ(abi::arg<u64>(c, 0), 0xFFFFFFFF80000001ull);
    CHECK_EQ(abi::arg<s64>(c, 0), static_cast<s64>(0xFFFFFFFF80000001ull));
    c.r[5].d[0] = 0x00000000000001F8ull;
    CHECK_EQ(abi::arg<s8>(c, 1), -8);                                   // the low byte, sign-extended
    CHECK_EQ(abi::arg<u8>(c, 1), 0xF8u);
    CHECK_EQ(abi::arg<s16>(c, 1), 0x1F8);
    CHECK_EQ(abi::arg<u16>(c, 1), 0x1F8u);
}

void a_flag_argument_is_tested_over_all_64_bits() {
    ps2::Cpu c = fresh();
    c.r[4].d[0] = 0;
    CHECK(!abi::arg<bool>(c, 0));
    c.r[4].d[0] = 1;
    CHECK(abi::arg<bool>(c, 0));
    c.r[4].d[0] = 0x0000000100000000ull;                                // bnez sees the upper half
    CHECK(abi::arg<bool>(c, 0));
    c.r[4].d[1] = 0x1;                                                  // but never the upper quadword
    c.r[4].d[0] = 0;
    CHECK(!abi::arg<bool>(c, 0));
}

void pointer_arguments_take_the_low_word() {
    ps2::Cpu c = fresh();
    c.r[6].d[0] = 0xFFFFFFFF81234560ull;
    CHECK_EQ(abi::arg<guest_ptr<f32>>(c, 2).address, 0x81234560u);
}

void float_arguments_come_from_f12_to_f19() {
    ps2::Cpu c = fresh();
    for (int i = 0; i < 8; ++i) CHECK_EQ(abi::farg(c, i).bits, 0x40000000u + 12u + static_cast<unsigned>(i));
}

void a_32_bit_result_is_sign_extended() {
    ps2::Cpu c = fresh();
    abi::ret_sx32(c, 0x80000000u);
    CHECK_EQ(c.r[2].d[0], 0xFFFFFFFF80000000ull);
    CHECK_EQ(c.r[2].d[1], 0xABCD000000000002ull);                       // the upper quadword is not the function's
    abi::ret_sx32(c, 0x7FFFFFFFu);
    CHECK_EQ(c.r[2].d[0], 0x000000007FFFFFFFull);
    abi::ret_sx32(c, 0xFFFFFFFFu, abi::v1);
    CHECK_EQ(c.r[3].d[0], 0xFFFFFFFFFFFFFFFFull);
    CHECK_EQ(c.r[2].d[0], 0x000000007FFFFFFFull);
}

void a_masked_result_is_zero_extended() {
    ps2::Cpu c = fresh();
    abi::ret_zx32(c, 0x80000000u);
    CHECK_EQ(c.r[2].d[0], 0x0000000080000000ull);
    abi::ret_zx32(c, 0x8180u, abi::v1);
    CHECK_EQ(c.r[3].d[0], 0x0000000000008180ull);
}

void wide_and_float_results() {
    ps2::Cpu c = fresh();
    abi::ret64(c, 0x0123456789ABCDEFull);
    CHECK_EQ(c.r[2].d[0], 0x0123456789ABCDEFull);
    abi::ret64(c, 0xFEDCBA9876543210ull, abi::v1);
    CHECK_EQ(c.r[3].d[0], 0xFEDCBA9876543210ull);
    abi::ret_f32(c, f32::lit(1.5f));
    CHECK_EQ(c.f[0], 0x3FC00000u);
    CHECK_EQ(c.f[1], 0x40000001u);
}

// --- typed functions wrapped as hooks -----------------------------------------------------------------

s32 g_seen_bits = 0;
bool g_seen_flag = false;

u32 pack_like(f32 x, s8 bits, bool flag) {          // f12, a0, a1: floats and integers count separately
    g_seen_bits = bits;
    g_seen_flag = flag;
    return static_cast<u32>(x.to_s32()) << 4;
}

s32 negative() { return -5; }
u32 high_bit() { return 0x80000000u; }
abi::zx32 high_bit_masked() { return {0x80000000u}; }
u64 wide() { return 0x8000000000000001ull; }
s16 short_negative() { return -2; }
u8 byte_value() { return 0xF0; }
bool truth() { return true; }
f32 scale(f32 a, f32 b) { return a * b; }
guest_ptr<u8> advance(guest_ptr<u8> p, u32 n) { return p + static_cast<s32>(n); }
void store_sum(guest_ptr<u32> out, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g) { *out = a + b + c + d + e + f + g; }
f32 mix(u32 a, f32 x, u32 b, f32 y, guest_ptr<f32> p, f32 z) { return f32::from_s32(static_cast<s32>(a + b + p.address)) + x + y + z; }
u32 with_cpu(ps2::Cpu &c, u32 a) { return c.r[16].w[0] + a; }

void a_typed_function_gets_its_arguments_by_class_and_order() {
    ps2::Cpu c = fresh();
    c.f[12] = f32::lit(3.75f).bits;
    c.r[4].d[0] = 0x1F8;                             // s8: -8
    c.r[5].d[0] = 0x0000000100000000ull;             // bool: true through the upper half
    abi::hook<&pack_like>(c);
    CHECK_EQ(g_seen_bits, -8);
    CHECK(g_seen_flag);
    CHECK_EQ(c.r[2].d[0], 0x30ull);

    c = fresh();
    c.r[4].d[0] = 10;
    c.f[12] = f32::lit(1.0f).bits;
    c.r[5].d[0] = 20;
    c.f[13] = f32::lit(2.0f).bits;
    c.r[6].d[0] = 30;
    c.f[14] = f32::lit(4.0f).bits;
    abi::hook<&mix>(c);
    CHECK_EQ(c.f[0], f32::lit(67.0f).bits);
}

void eight_integer_arguments_reach_t3() {
    std::memset(ps2::g_ram, 0, 0x100);
    ps2::Cpu c = fresh();
    c.r[4].d[0] = 0x40;
    for (int i = 1; i < 8; ++i) c.r[4 + i].d[0] = 1u << i;
    abi::hook<&store_sum>(c);
    CHECK_EQ(ps2::ld32(0x40), 0xFEu);
}

void results_are_extended_by_their_type() {
    ps2::Cpu c = fresh();
    abi::hook<&negative>(c);
    CHECK_EQ(c.r[2].d[0], 0xFFFFFFFFFFFFFFFBull);
    abi::hook<&high_bit>(c);
    CHECK_EQ(c.r[2].d[0], 0xFFFFFFFF80000000ull);    // addu, lw and shifts leave a u32 sign-extended too
    abi::hook<&high_bit_masked>(c);
    CHECK_EQ(c.r[2].d[0], 0x0000000080000000ull);
    abi::hook<&wide>(c);
    CHECK_EQ(c.r[2].d[0], 0x8000000000000001ull);
    abi::hook<&short_negative>(c);
    CHECK_EQ(c.r[2].d[0], 0xFFFFFFFFFFFFFFFEull);
    abi::hook<&byte_value>(c);
    CHECK_EQ(c.r[2].d[0], 0xF0ull);
    abi::hook<&truth>(c);
    CHECK_EQ(c.r[2].d[0], 1ull);

    c = fresh();
    c.f[12] = f32::lit(1.5f).bits;
    c.f[13] = f32::lit(2.0f).bits;
    const std::uint64_t v0 = c.r[2].d[0];
    abi::hook<&scale>(c);
    CHECK_EQ(c.f[0], 0x40400000u);
    CHECK_EQ(c.r[2].d[0], v0);                       // a float result leaves v0 alone

    c = fresh();
    c.r[4].d[0] = 0xFFFFFFFF80001000ull;
    c.r[5].d[0] = 0x10;
    abi::hook<&advance>(c);
    CHECK_EQ(c.r[2].d[0], 0xFFFFFFFF80001010ull);    // an address is a 32-bit value: sign-extended
}

void a_function_may_ask_for_the_register_file() {
    ps2::Cpu c = fresh();
    c.r[16].d[0] = 100;
    c.r[4].d[0] = 5;
    abi::hook<&with_cpu>(c);
    CHECK_EQ(c.r[2].d[0], 105ull);
}

void a_hook_leaves_everything_else_alone() {
    std::memset(ps2::g_ram, 0, 0x100);
    ps2::Cpu c = fresh();
    c.r[4].d[0] = 0x40;
    const ps2::Cpu before = c;
    abi::hook<&store_sum>(c);                        // void: no result register either
    CHECK(std::memcmp(static_cast<const void *>(&before), static_cast<const void *>(&c), sizeof c) == 0);

    abi::hook<&negative>(c);
    for (int i = 0; i < 32; ++i) {
        if (i != 2) CHECK_EQ(c.r[i].d[0], before.r[i].d[0]);
        CHECK_EQ(c.r[i].d[1], before.r[i].d[1]);
        CHECK_EQ(c.f[i], before.f[i]);
    }
    CHECK_EQ(c.pc, before.pc);                       // the runtime sets pc after the hook returns
}

}

int main() {
    integer_arguments_come_from_a0_to_t3();
    arguments_are_narrowed_to_the_asked_type();
    a_flag_argument_is_tested_over_all_64_bits();
    pointer_arguments_take_the_low_word();
    float_arguments_come_from_f12_to_f19();
    a_32_bit_result_is_sign_extended();
    a_masked_result_is_zero_extended();
    wide_and_float_results();
    a_typed_function_gets_its_arguments_by_class_and_order();
    eight_integer_arguments_reach_t3();
    results_are_extended_by_their_type();
    a_function_may_ask_for_the_register_file();
    a_hook_leaves_everything_else_alone();
    return check::done("test_abi");
}
