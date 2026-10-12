// compare_wide_strings: three-way ordering of two zero-terminated 16-bit strings.
// Retail address 0x0035EFA0. See compare_wide_strings.hpp for the evidence and status.
//
// Each step reads s1's unit first, then s2's. If s1 has ended, the result depends only on
// whether s2 ended too (equal) or not (s1 is a strict prefix, so it sorts first). Otherwise
// an ended s2 means s2 is the strict prefix and s1 sorts after; a smaller or larger unit
// decides immediately; equal units advance both pointers by one unit.

#include "hooks/frontend/compare_wide_strings.hpp"

namespace b3::frontend {

std::int32_t compare_wide_strings(const std::uint16_t *s1, const std::uint16_t *s2) {
    for (;;) {
        const std::uint32_t c1 = *s1;   // zero-extended 16-bit unit, read before s2's
        if (c1 == 0u) {
            const std::uint32_t c2 = *s2;
            return c2 == 0u ? 0 : -1;
        }
        const std::uint32_t c2 = *s2;
        if (c2 == 0u) return 1;
        if (c1 < c2) return -1;
        if (c2 < c1) return 1;
        ++s1;
        ++s2;
    }
}

}
