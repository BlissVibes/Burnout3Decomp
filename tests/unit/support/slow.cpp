// The runtime's out-of-RAM accessors. Unit tests have no hardware, ROM or VU memory behind them, so an
// access outside EE RAM and the scratchpad is a test bug: say where and stop.

#include "ps2/memory.hpp"

#include <cstdio>
#include <cstdlib>

namespace ps2 {

namespace {

[[noreturn]] void outside(const char *what, std::uint32_t a) {
    std::fprintf(stderr, "unit test: %s at 0x%08X is outside EE RAM and the scratchpad\n", what, a);
    std::abort();
}

}

std::uint8_t slow_ld8(std::uint32_t a) { outside("load", a); }
std::uint16_t slow_ld16(std::uint32_t a) { outside("load", a); }
std::uint32_t slow_ld32(std::uint32_t a) { outside("load", a); }
std::uint64_t slow_ld64(std::uint32_t a) { outside("load", a); }
void slow_ld128(std::uint32_t a, Reg128 &) { outside("load", a); }
void slow_st8(std::uint32_t a, std::uint8_t) { outside("store", a); }
void slow_st16(std::uint32_t a, std::uint16_t) { outside("store", a); }
void slow_st32(std::uint32_t a, std::uint32_t) { outside("store", a); }
void slow_st64(std::uint32_t a, std::uint64_t) { outside("store", a); }
void slow_st128(std::uint32_t a, const Reg128 &) { outside("store", a); }

}
