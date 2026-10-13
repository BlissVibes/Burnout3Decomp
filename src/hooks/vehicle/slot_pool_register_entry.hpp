#pragma once

// slot_pool_register_entry: appends one vehicle render entry to a fixed pool of 16-byte slots,
// recording up to two of its sub-objects and growing a running size total.
//
// Retail address 0x00254970 (game code, SLUS-21050). Evidence: the recompiler's translation of the
// function (unit_0055.cpp) read against the reader's specification; the two agree on every load,
// store and their order. Call graph: the only caller 0x00261F18 resets the pool through 0x00254A50,
// then calls the filler 0x00254D50 and this function for exactly 24 entries of 432 bytes each, then
// hands the pool to the consumer 0x00254580, which walks slots 0..count-1 and uses the running total
// (+20) when it sizes a packet. Status: draft, oracle pending.
//
// Fixed globals (gp = 0x004E8670):
//   0x004E29D0  u32  slot count          read and written (+1 when the entry got at least one pointer)
//   0x004E29D4  u32  running total       read and written (+22 per appended pointer)
//   0x004E29E0  u8   second sub-object enabled  read
//   0x004E29E4  u8   skip unowned entries       read, only when the entry's owner byte is zero
//   0x01D9D010  pool of 16-byte slots in bss, indexed by the slot count: +0 owner entry address,
//               +4 and +8 sub-object addresses, +12 number of sub-object addresses stored
//
// Behaviour. The entry holds a descriptor pointer at byte 228 whose byte 93 says whether the entry
// is owned. An unowned entry is dropped without any write when the skip flag is set; otherwise the
// function takes the slot at the current count, zeroes its pointer count, and appends the first
// sub-object (entry+232) when the word at entry+272 is non-zero, then the second sub-object
// (entry+276) when the enable byte and the word at entry+316 are both non-zero. Each append stores
// the new pointer count, the sub-object address at the next free pointer position (+4, then +8),
// and adds 22 to the running total. If anything was appended the slot's owner word becomes the
// entry address and the global count advances by one; otherwise only the slot's +12 was zeroed,
// its other words keep whatever an earlier frame left there, and the next entry reuses the slot.
//
// All arithmetic is 32-bit integer; no floating point, no stack, no result. The function has no
// bounds check: a count of 24 or more writes past the inferred capacity exactly as retail does, and
// the slot address wraps modulo 2^32. The descriptor pointer must be readable (precondition).
//
// What the sub-objects and the 22 units mean is inferred, not verified: sibling code treats
// entry+232 and entry+276 as two 44-byte sub-objects whose word at +40 is an active flag, and the
// consumer's use of the total beside 128-bit packet stores suggests a size in quadwords.

#include "ee/guest.hpp"

#include <cstdint>

namespace b3::vehicle {

// A vehicle render entry as the caller lays them out: 432 bytes apart. Only the offsets this
// function reads are named here; the rest of the entry is unknown.
struct VehicleRenderEntry;

// The descriptor an entry points at from byte 228. Only its owner byte is read here.
struct VehicleRenderDescriptor;

namespace slot_pool {

constexpr std::uint32_t kSlotCountAddress = 0x004E29D0u;
constexpr std::uint32_t kRunningTotalAddress = 0x004E29D4u;
constexpr std::uint32_t kEnableSecondSubobjectAddress = 0x004E29E0u;
constexpr std::uint32_t kSkipUnownedEntriesAddress = 0x004E29E4u;
constexpr std::uint32_t kPoolBase = 0x01D9D010u;
constexpr std::uint32_t kSlotStride = 16u;
constexpr std::uint32_t kInferredCapacity = 24u;       // by the caller's loop and the next used address
constexpr std::uint32_t kUnitsPerSubobject = 22u;      // added to the running total per pointer

// Slot word offsets.
constexpr std::uint32_t kSlotOwner = 0u;
constexpr std::uint32_t kSlotFirstPointer = 4u;
constexpr std::uint32_t kSlotPointerCount = 12u;

// Entry byte offsets.
constexpr std::uint32_t kEntryDescriptor = 228u;       // guest pointer to a VehicleRenderDescriptor
constexpr std::uint32_t kEntryFirstSubobject = 232u;
constexpr std::uint32_t kEntryFirstActive = 272u;      // word, non-zero when the first sub-object counts
constexpr std::uint32_t kEntrySecondSubobject = 276u;
constexpr std::uint32_t kEntrySecondActive = 316u;     // word, non-zero when the second sub-object counts

// Descriptor byte offsets.
constexpr std::uint32_t kDescriptorOwned = 93u;        // byte, zero means the entry is unowned

}

// Registers one entry in the slot pool as described above. The argument is the guest address the
// retail function receives; it is stored into guest memory, so it is never translated.
void slot_pool_register_entry(ee::guest_ptr<VehicleRenderEntry> entry);

}

template <>
struct ee::guest_size<b3::vehicle::VehicleRenderEntry> : std::integral_constant<std::uint32_t, 432> {};
