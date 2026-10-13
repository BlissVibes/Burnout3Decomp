// arena_alloc_aligned (retail 0x002F9CA0): one bump-allocation step over an arena descriptor in
// guest memory. Description, layout, evidence and status (draft, oracle pending) are in the header.

#include "hooks/util/arena_alloc_aligned.hpp"

namespace b3::util {

std::uint32_t arena_alloc_aligned(ee::guest_ptr<ArenaDesc> arena, std::uint32_t size) {
    const std::uint32_t alignment = *arena.at<std::uint32_t>(kArenaAlignmentOffset);
    const std::uint32_t cursor = *arena.at<std::uint32_t>(kArenaCursorOffset);

    // Round the cursor up with the mask formula. Everything is 32-bit and wraps: an alignment of 0
    // yields a mask of all ones and a start of 0, and a cursor near the top of the address space
    // can wrap to a small start.
    const std::uint32_t mask = alignment - 1u;
    const std::uint32_t start = (cursor + mask) & ~mask;
    const std::uint32_t next = start + size;

    // The cursor advances before the capacity test, so a failed allocation still consumes space.
    *arena.at<std::uint32_t>(kArenaCursorOffset) = next;

    // Unsigned compare: a new cursor equal to the limit is still in bounds, and an arena placed at
    // or above 0x80000000 is not special. Retail re-reads the cursor it just stored; the value is
    // the one in `next`.
    const std::uint32_t limit = *arena.at<std::uint32_t>(kArenaLimitOffset);
    if (limit < next) {
        *arena.at<std::uint32_t>(kArenaLastResultOffset) = 0u;
        return 0u;
    }
    *arena.at<std::uint32_t>(kArenaLastResultOffset) = start;
    return start;  // retail returns the word it just stored to +24, which is this value
}

std::uint32_t arena_alloc_aligned(std::uint32_t arena_address, std::uint32_t size) {
    return arena_alloc_aligned(ee::guest_ptr<ArenaDesc>{arena_address}, size);
}

}  // namespace b3::util
