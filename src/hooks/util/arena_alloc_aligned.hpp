#pragma once

// arena_alloc_aligned: one bump-allocation step over a small arena descriptor in guest memory.
//
// Retail address 0x002F9CA0 (game code, SLUS-21050). The owners table puts 0x002E2730-0x00303F14
// under game ("crash and aftertouch, camera", high confidence), but the routine itself is a generic
// helper, hence subsystem util. A leaf of 0x4C bytes (entry to the delay slot of the return at
// 0x002F9CEC): no callees, no stack frame, no floating point, no globals. Evidence: the
// recompiler's translation (unit_007E.cpp, label L_002F9CA0) read in full against the reader's
// specification, which was itself checked against the raw instruction words; the three agree.
// Eleven direct call sites: seven in the effects code at 0x002B739C, 0x002B7414, 0x002B7470,
// 0x002B74A4, 0x002B7554, 0x002B7590, 0x002B75E8 (unit_006D; that caller builds its descriptor on
// its own stack and sets alignments 16, 1, 4, 1, 4 through the setter before each call) and four
// in the front end at 0x00325678, 0x00325688, 0x00325698, 0x00325750 (unit_0089). No data word in
// the ELF holds this address, so there are no calls through pointers. Status: draft, oracle pending.
//
// Descriptor layout (field names are ours; the siblings fix the parts this function does not touch:
// 0x002F9CF0 sets +20, 0x002F9DA0 initialises +0/+8/+12/+16, 0x002F9D00 releases +4 and clears):
//   +0  u8  initialised flag      not touched here
//   +4  u32 owned buffer pointer  not touched here
//   +8  u32 base                  not touched here
//   +12 u32 limit                 one past the last usable byte
//   +16 u32 cursor                next free byte
//   +20 u32 alignment
//   +24 u32 last result           the start returned by the latest call, 0 after a failure
//
// Behaviour. The cursor is rounded up to the alignment with the mask formula
// (cursor + alignment - 1) & ~(alignment - 1), then advanced by the requested size, and the new
// cursor is written back BEFORE the capacity test. If the new cursor is above the limit (unsigned
// 32-bit compare; equal to the limit is still in bounds) the call fails: the last result becomes 0
// and 0 is returned, but the cursor stays advanced, so later calls fail too unless the arithmetic
// wraps. Otherwise the last result becomes the aligned start and that start is returned. Every
// value is a 32-bit word and every operation wraps at 32 bits.
//
// Consequences worth knowing: alignment 1 means no rounding; alignment 0 gives a mask of all ones,
// so the start is 0 and the new cursor equals the size (such a success returns 0, the same as a
// failure); a non-power-of-two alignment is not a true round-up (the mask formula is kept, never a
// modulo); only the low 32 bits of the size register matter; a size of 0 is legal and still moves
// the cursor to the aligned position; the descriptor may live anywhere, including a caller's stack.
// The descriptor address is not validated.
//
// Globals: none. Memory: 32-bit reads of +20, +16 and +12 (retail also re-reads +16 and, on
// success, +24 right after writing them; the values are the same, so this code keeps them in
// locals); 32-bit writes of +16 (always) and +24 (always: 0 or the start).

#include "ee/guest.hpp"

#include <cstdint>

namespace b3::util {

// Tag for the arena descriptor in guest memory; reached by field offset only, never stepped.
struct ArenaDesc;

inline constexpr std::uint32_t kArenaLimitOffset = 12;
inline constexpr std::uint32_t kArenaCursorOffset = 16;
inline constexpr std::uint32_t kArenaAlignmentOffset = 20;
inline constexpr std::uint32_t kArenaLastResultOffset = 24;

// Allocates `size` bytes from the arena described at `arena`. Returns the aligned start on success
// and 0 on failure (an alignment of 0 also yields a successful start of 0, see above). The retail
// result register holds this value sign-extended from 32 bits.
std::uint32_t arena_alloc_aligned(ee::guest_ptr<ArenaDesc> arena, std::uint32_t size);

// The same on a bare guest address of the descriptor.
std::uint32_t arena_alloc_aligned(std::uint32_t arena_address, std::uint32_t size);

}  // namespace b3::util
