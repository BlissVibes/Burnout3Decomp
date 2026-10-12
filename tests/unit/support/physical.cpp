// ps2::physical() for hooks that use host_ram() from the fork's ps2/hook.hpp: the same segment mapping as
// the runtime's (kseg0/kseg1 and the uncached and accelerated mirrors of kuseg).

#include <cstdint>

namespace ps2 {

std::uint32_t physical(std::uint32_t address) {
    const std::uint32_t top = address >> 28;
    if (top >= 0x8u && top <= 0xBu) return address & 0x1FFFFFFFu;
    if (top == 0x2u || top == 0x3u) return address & 0x0FFFFFFFu;
    return address;
}

}
