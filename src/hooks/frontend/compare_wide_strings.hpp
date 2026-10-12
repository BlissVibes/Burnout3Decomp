#pragma once

// compare_wide_strings: three-way ordering of two zero-terminated 16-bit strings.
//
// Retail address 0x0035EFA0 (game code, front end / online UI text range 0x003070B0-0x003D2FD0).
// Evidence: hand-read of the retail routine and the fork's recompiler output for that range
// (unit_0097.cpp, labels L_0035EFA0 to L_0035F014); callers at 0x00366E00, 0x003A5234,
// 0x003A53EC, 0x003A56BC, 0x003A572C. Status: draft, oracle run pending.
//
// Behaviour: walks both strings one 16-bit unit at a time and returns -1, 0 or +1 (never the
// unit difference). Units are compared as unsigned 16-bit values, so this is plain UTF-16
// code-unit order: no case folding, no locale, no surrogate decoding. A string that ends
// first sorts first; two strings that end together are equal. Reading stops at the first
// differing or terminating unit, and nothing past that point is touched.

#include <cstdint>

namespace b3::frontend {

std::int32_t compare_wide_strings(const std::uint16_t *s1, const std::uint16_t *s2);

}
