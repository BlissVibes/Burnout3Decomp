#include "ps2/hook.hpp"

namespace b3 {

// 0x00389700: packs a float as 16-bit fixed point with `fraction_bits` fraction bits. With
// signed_format the result is sign-magnitude, bit 15 the sign. `x` is the float's bit pattern:
// all float arithmetic goes through ps2::fp so it rounds as the EE does.
std::uint16_t pack_fixed16(std::uint32_t x, std::int8_t fraction_bits, bool signed_format) {
    using namespace ps2;
    const std::uint32_t shift = static_cast<std::uint32_t>(fraction_bits) & 31u;
    const bool negative = fp::key(x) < fp::key(0u);
    std::uint32_t whole, fraction;
    if (negative) {
        whole = 0u - fp::cvt_w_s(x);   // truncate, then negate: the order matters when the conversion saturates
        fraction = fp::sub(x ^ fp::kSign, fp::cvt_s_w(whole));
    } else {
        whole = fp::cvt_w_s(x);
        fraction = fp::sub(x, fp::cvt_s_w(whole));
    }
    const std::uint32_t scale = fp::cvt_s_w(1u << shift);
    std::uint32_t packed = ((whole << shift) & 0xFFFFu) + (fp::cvt_w_s(fp::mul(fraction, scale)) & 0xFFFFu);
    packed &= 0xFFFFu;
    if (signed_format) packed = negative ? packed | 0x8000u : packed & 0x7FFFu;
    return static_cast<std::uint16_t>(packed);
}

// f12 = x, a0 = fraction bits (low byte, sign-extended), a1 = signed format (any non-zero 64-bit
// value); v0 = the result, zero-extended as the original leaves it after its andi.
void hook_pack_fixed16(ps2::Cpu &c) {
    c.r[2].d[0] = pack_fixed16(c.f[12], static_cast<std::int8_t>(c.r[4].w[0]), c.r[5].d[0] != 0);
}

}
