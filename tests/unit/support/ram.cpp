// Guest memory for unit tests: the two arrays the runtime's memory.hpp expects, zero-filled.

#include "ps2/memory.hpp"

namespace ps2 {

std::uint8_t g_ram[kRamSize + 64];
std::uint8_t g_spr[kScratchpadSize];

}
