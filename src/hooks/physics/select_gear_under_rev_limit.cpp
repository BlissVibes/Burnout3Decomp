// select_gear_under_rev_limit (retail 0x001C6F30): see select_gear_under_rev_limit.hpp for the
// description, evidence and status (draft, oracle pending).
#include "hooks/physics/select_gear_under_rev_limit.hpp"

#include "ps2/fpu.hpp"

namespace b3::physics {

// float(pi/30) is 0x3DD67750, the pattern the test derivations assume; pinned so a change to the
// expression in the header cannot drift from its meaning.
static_assert(kRadiansPerSecondPerRpmBits == 0x3DD67750u);

std::int32_t select_gear_under_rev_limit(std::uint32_t *block, std::uint32_t divisor_bits,
                                         std::uint32_t dividend_bits,
                                         std::uint32_t inherited_value_bits,
                                         bool *divide_by_zero) {
    namespace fp = ps2::fp;

    // Everything up to the loop test runs whether or not any entry is examined.
    const auto count = static_cast<std::int32_t>(block[kGearCountWord]);
    const fp::Result quotient = fp::div(dividend_bits, divisor_bits);
    if (divide_by_zero) *divide_by_zero = quotient.invalid;
    const std::uint32_t r = quotient.value;
    const std::uint32_t limit = fp::mul(kRadiansPerSecondPerRpmBits, block[kRevLimitWord]);

    std::uint32_t value = inherited_value_bits;
    std::int32_t i = 1;
    while (i < count) {
        // Both operands come from memory on every pass: the multiplier is not cached, and a
        // count past the table makes the index run into the multiplier and the scalars after it.
        const std::uint32_t ratio = block[kGearRatioWordBase + i];
        const std::uint32_t mult = block[kGearMultiplierWord];
        // Two truncating multiplies in this grouping; folding or regrouping changes low bits.
        value = fp::mul(mult, fp::mul(r, ratio));
        if (fp::key(value) <= fp::key(limit)) break;
        ++i;
    }

    block[kEngineSpeedWord] = value;  // always written, even when the loop never ran
    return i;
}

}  // namespace b3::physics
