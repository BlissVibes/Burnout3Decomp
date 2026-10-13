// Standalone test for b3::util::arena_alloc_aligned (retail 0x002F9CA0).
//
// Expected values are derived from the specification, never from running the game. The rule:
//   start = (cursor + alignment - 1) & ~(alignment - 1)      32-bit, wrapping
//   next  = start + size                                     written to +16 before the test
//   limit < next (unsigned) -> +24 = 0, result 0; otherwise +24 = start, result start.
// Each case names the property it pins. The descriptor lives in guest RAM from tests/unit/support.

#include "hooks/util/arena_alloc_aligned.hpp"

#include "check.hpp"
#include "ee/guest.hpp"

#include <cstdint>
#include <cstring>

using b3::util::arena_alloc_aligned;
using b3::util::ArenaDesc;
using ee::u32;

namespace {

constexpr u32 kArena = 0x00300000u;   // where the descriptor sits in guest RAM

// Filler in the three fields the function must not touch, so a stray write shows up.
constexpr u32 kFlagWord = 0x11111101u, kOwned = 0x22222222u, kBase = 0x00400000u;

void make(u32 limit, u32 cursor, u32 alignment, u32 last = 0xDDDDDDDDu, u32 at = kArena) {
    std::memset(ps2::g_ram + at - 16, 0xEE, 64);
    ee::st<u32>(at + 0, kFlagWord);
    ee::st<u32>(at + 4, kOwned);
    ee::st<u32>(at + 8, kBase);
    ee::st<u32>(at + 12, limit);
    ee::st<u32>(at + 16, cursor);
    ee::st<u32>(at + 20, alignment);
    ee::st<u32>(at + 24, last);
}

// Calls the function and checks the result, the cursor and the last-result word together.
void expect(u32 size, u32 result, u32 cursor_after, u32 at = kArena) {
    const u32 limit = ee::ld<u32>(at + 12), alignment = ee::ld<u32>(at + 20);
    CHECK_EQ(arena_alloc_aligned(ee::guest_ptr<ArenaDesc>{at}, size), result);
    CHECK_EQ(ee::ld<u32>(at + 16), cursor_after);
    CHECK_EQ(ee::ld<u32>(at + 24), result);
    // untouched fields and the bytes around the descriptor
    CHECK_EQ(ee::ld<u32>(at + 0), kFlagWord);
    CHECK_EQ(ee::ld<u32>(at + 4), kOwned);
    CHECK_EQ(ee::ld<u32>(at + 8), kBase);
    CHECK_EQ(ee::ld<u32>(at + 12), limit);
    CHECK_EQ(ee::ld<u32>(at + 20), alignment);
    CHECK_EQ(ee::ld<u32>(at - 4), 0xEEEEEEEEu);
    CHECK_EQ(ee::ld<u32>(at + 28), 0xEEEEEEEEu);
}

void rounding() {
    make(0x00500000u, 0x00400000u, 16);
    expect(8, 0x00400000u, 0x00400008u);       // already aligned: no rounding
    expect(8, 0x00400010u, 0x00400018u);       // 0x...08 rounds up to 0x...10
    make(0x00500000u, 0x00400001u, 4);
    expect(3, 0x00400004u, 0x00400007u);
    make(0x00500000u, 0x00400003u, 1);
    expect(5, 0x00400003u, 0x00400008u);       // alignment 1: the cursor as it is
    make(0x00500000u, 0x0040000Fu, 16);
    expect(0, 0x00400010u, 0x00400010u);       // size 0 still moves the cursor to the aligned position
    make(0x00500000u, 0x00400011u, 0x100);
    expect(0x20, 0x00400100u, 0x00400120u);
}

void capacity() {
    make(0x00400020u, 0x00400000u, 16);
    expect(0x20, 0x00400000u, 0x00400020u);    // new cursor equal to the limit is still in bounds
    make(0x00400020u, 0x00400000u, 16);
    expect(0x21, 0u, 0x00400021u);             // one byte over: fails, the cursor stays advanced
    expect(1, 0u, 0x00400031u);                // and the next call fails too (0x21 rounds to 0x30)
    make(0x00400020u, 0x00400011u, 16);
    expect(1, 0u, 0x00400021u);                // the rounding alone can push it over
    make(0x00400020u, 0x00400020u, 1, 0x12345678u);
    expect(0, 0x00400020u, 0x00400020u);       // empty request at the very end succeeds
    expect(1, 0u, 0x00400021u);                // a failure overwrites the last result with 0
}

void odd_alignments() {
    make(0x00500000u, 0x00400007u, 0);
    expect(0x30, 0u, 0x30u);                   // alignment 0: mask all ones, start 0, cursor = size, "success" 0
    make(0x00500000u, 0x00400005u, 6);
    expect(1, 0x0040000Au & ~5u, (0x0040000Au & ~5u) + 1u);   // not a power of two: the mask formula, not a round-up
    make(0x00500000u, 0x00400005u, 3);
    expect(2, 0x00400005u, 0x00400007u);       // (5 + 2) & ~2 = 5
}

void wrapping_and_unsigned_compare() {
    make(0xFFFFFFFFu, 0xFFFFFFF9u, 16);
    expect(4, 0u, 4u);                         // rounding wraps past 2^32: start 0, in bounds
    make(0x90000000u, 0x80000004u, 4);
    expect(0x10, 0x80000004u, 0x80000014u);    // addresses above 2^31 compare unsigned
    make(0x7FFFFFFFu, 0x7FFFFFF0u, 1);
    expect(0x20, 0u, 0x80000010u);             // 0x80000010 is above 0x7FFFFFFF unsigned (a signed compare would pass it)
    make(0x00500000u, 0x00400000u, 4);
    expect(0xFFFFFFFCu, 0x00400000u, 0x003FFFFCu);   // a huge size wraps the cursor below the start; still "in bounds"
}

void descriptor_anywhere_and_bare_address() {
    const u32 at = 0x01F00040u;
    make(0x00500000u, 0x00400002u, 8, 0, at);
    expect(6, 0x00400008u, 0x0040000Eu, at);
    make(0x00500000u, 0x00400002u, 8);
    CHECK_EQ(arena_alloc_aligned(kArena, 6u), 0x00400008u);   // the overload on a bare guest address
    CHECK_EQ(ee::ld<u32>(kArena + 16), 0x0040000Eu);
}

}

int main() {
    std::memset(ps2::g_ram, 0, ps2::kRamSize);
    rounding();
    capacity();
    odd_alignments();
    wrapping_and_unsigned_compare();
    descriptor_anywhere_and_bare_address();
    return check::done("test_arena_alloc_aligned");
}
