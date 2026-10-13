// slot_pool_register_entry, retail 0x00254970: appends a vehicle render entry to the 16-byte slot
// pool at 0x01D9D010 and grows the running total at 0x004E29D4. Evidence, globals and layout notes
// are in slot_pool_register_entry.hpp. Status: draft, oracle pending.

#include "hooks/vehicle/slot_pool_register_entry.hpp"

namespace b3::vehicle {

namespace {

using namespace slot_pool;

// Stores one sub-object address into the slot's next free pointer position. Retail's order is the
// pointer count, then the pointer, then the running total; each is a read-modify-write of guest
// memory so that two appends add 44 and the second pointer lands at +8 only when the first was taken.
void append_pointer(std::uint32_t slot, std::uint32_t subobject) {
    const std::uint32_t count = ee::ld<std::uint32_t>(slot + kSlotPointerCount);
    const std::uint32_t total = ee::ld<std::uint32_t>(kRunningTotalAddress);
    ee::st<std::uint32_t>(slot + kSlotPointerCount, count + 1u);
    ee::st<std::uint32_t>(slot + kSlotFirstPointer + (count << 2), subobject);
    ee::st<std::uint32_t>(kRunningTotalAddress, total + kUnitsPerSubobject);
}

}

void slot_pool_register_entry(ee::guest_ptr<VehicleRenderEntry> entry) {
    const std::uint32_t a0 = entry.address;

    // An unowned entry is dropped before any write when the skip flag is set. The flag byte is read
    // only on that path; an owned entry never consults it.
    const std::uint32_t descriptor = ee::ld<std::uint32_t>(a0 + kEntryDescriptor);
    if (ee::ld<std::uint8_t>(descriptor + kDescriptorOwned) == 0u) {
        if (ee::ld<std::uint8_t>(kSkipUnownedEntriesAddress) != 0u) return;
    }

    // The slot is chosen by the count as read here; the address arithmetic is 32-bit and unchecked.
    const std::uint32_t slot = kPoolBase + (ee::ld<std::uint32_t>(kSlotCountAddress) << 4);
    ee::st<std::uint32_t>(slot + kSlotPointerCount, 0u);

    if (ee::ld<std::uint32_t>(a0 + kEntryFirstActive) != 0u) {
        append_pointer(slot, a0 + kEntryFirstSubobject);
    }

    // The second active word is read only when the enable byte is set.
    if (ee::ld<std::uint8_t>(kEnableSecondSubobjectAddress) != 0u) {
        if (ee::ld<std::uint32_t>(a0 + kEntrySecondActive) != 0u) {
            append_pointer(slot, a0 + kEntrySecondSubobject);
        }
    }

    // The slot becomes the entry's and the pool grows only if a pointer was stored; otherwise the
    // slot's other words are left as they were and the next entry takes the same slot.
    if (ee::ld<std::uint32_t>(slot + kSlotPointerCount) != 0u) {
        const std::uint32_t count = ee::ld<std::uint32_t>(kSlotCountAddress);
        ee::st<std::uint32_t>(slot + kSlotOwner, a0);
        ee::st<std::uint32_t>(kSlotCountAddress, count + 1u);
    }
}

}
