// slot_pool_register_entry (retail 0x00254970): every case is checked against the whole of guest RAM.
// A snapshot of RAM is taken before the call, the writes the specification predicts are applied to
// the snapshot by hand, and after the call RAM must equal the snapshot byte for byte, so an extra,
// missing or misplaced store fails as surely as a wrong value. Expected values are derived by reasoning
// from the specification (pool slot = base + 16 * count; append stores count+1, then the sub-object
// address at +4 + 4 * old count, then total + 22; owner and count+1 only when something was appended);
// nothing comes from running the game.

#include "check.hpp"
#include "ee/guest.hpp"
#include "hooks/vehicle/slot_pool_register_entry.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace ee;
using namespace b3::vehicle;
using namespace b3::vehicle::slot_pool;

namespace {

// Test fixtures live well away from the pool and the globals.
constexpr u32 kEntry = 0x00500000u;         // 432-byte entries, 0x00500000 + 432 * i
constexpr u32 kDescriptor = 0x00480000u;    // one descriptor per entry, 128 bytes apart
constexpr u32 kEntryStride = 432u;
constexpr u32 kDescriptorStride = 128u;

std::vector<u8> g_snapshot;
std::vector<u8> g_spr_snapshot;

void clear() {
    std::memset(ps2::g_ram, 0, ps2::kRamSize);
    std::memset(ps2::g_spr, 0, ps2::kScratchpadSize);
}

void snapshot() {
    g_snapshot.assign(ps2::g_ram, ps2::g_ram + ps2::kRamSize);
    g_spr_snapshot.assign(ps2::g_spr, ps2::g_spr + ps2::kScratchpadSize);
}

// Predict a 32-bit store in the snapshot (little-endian, like ee::st).
void expect32(u32 address, u32 value) {
    for (int i = 0; i < 4; ++i) g_snapshot[address + static_cast<u32>(i)] = static_cast<u8>(value >> (8 * i));
}

// RAM and scratchpad must now match the predicted snapshot everywhere.
void ram_matches_prediction(const char *what) {
    const bool ram_ok = std::memcmp(ps2::g_ram, g_snapshot.data(), ps2::kRamSize) == 0;
    const bool spr_ok = std::memcmp(ps2::g_spr, g_spr_snapshot.data(), ps2::kScratchpadSize) == 0;
    if (!ram_ok) {
        for (u32 a = 0; a < ps2::kRamSize; ++a) {
            if (ps2::g_ram[a] != g_snapshot[a]) {
                std::printf("  %s: first difference at 0x%08X: got 0x%02X, predicted 0x%02X\n", what, a,
                            ps2::g_ram[a], g_snapshot[a]);
                break;
            }
        }
    }
    CHECK(ram_ok);
    CHECK(spr_ok);
}

struct EntrySetup {
    u8 owned = 1;            // descriptor byte 93
    u32 first_active = 1;    // word at entry+272
    u32 second_active = 1;   // word at entry+316
};

// Writes entry i and its descriptor; the descriptor pointer is at entry+228.
u32 make_entry(u32 index, const EntrySetup &setup) {
    const u32 entry = kEntry + kEntryStride * index;
    const u32 descriptor = kDescriptor + kDescriptorStride * index;
    st<u32>(entry + kEntryDescriptor, descriptor);
    st<u32>(entry + kEntryFirstActive, setup.first_active);
    st<u32>(entry + kEntrySecondActive, setup.second_active);
    st<u8>(descriptor + kDescriptorOwned, setup.owned);
    // Fill the sub-objects with a recognisable pattern so a stray write into them would show.
    for (u32 off = kEntryFirstSubobject; off < kEntryFirstActive; off += 4) st<u32>(entry + off, 0xA0000000u | off);
    for (u32 off = kEntrySecondSubobject; off < kEntrySecondActive; off += 4) st<u32>(entry + off, 0xB0000000u | off);
    return entry;
}

void set_globals(u32 count, u32 total, u8 enable_second, u8 skip_unowned) {
    st<u32>(kSlotCountAddress, count);
    st<u32>(kRunningTotalAddress, total);
    st<u8>(kEnableSecondSubobjectAddress, enable_second);
    st<u8>(kSkipUnownedEntriesAddress, skip_unowned);
}

// Fills every pool slot with stale words so "left as it was" is distinguishable from "zeroed".
void fill_pool_with_stale(u32 slots) {
    for (u32 i = 0; i < slots; ++i) {
        const u32 slot = kPoolBase + kSlotStride * i;
        st<u32>(slot + kSlotOwner, 0x5A000000u | i);
        st<u32>(slot + kSlotFirstPointer, 0x5B000000u | i);
        st<u32>(slot + kSlotFirstPointer + 4, 0x5C000000u | i);
        st<u32>(slot + kSlotPointerCount, 0x5D000000u | i);
    }
}

void call(u32 entry) { slot_pool_register_entry(guest_ptr<VehicleRenderEntry>{entry}); }

u32 slot_address(u32 count) { return kPoolBase + (count << 4); }

// Both sub-objects active, enable set: pointers at +4 and +8, total + 44, owner stored, count + 1.
void both_subobjects_fill_the_slot() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(0, 0, 1, 1);
    const u32 e = make_entry(0, {});
    snapshot();
    const u32 slot = slot_address(0);
    expect32(slot + kSlotPointerCount, 2);
    expect32(slot + kSlotFirstPointer, e + kEntryFirstSubobject);
    expect32(slot + kSlotFirstPointer + 4, e + kEntrySecondSubobject);
    expect32(kRunningTotalAddress, 44);
    expect32(slot + kSlotOwner, e);
    expect32(kSlotCountAddress, 1);
    call(e);
    ram_matches_prediction("both");
    CHECK_EQ(ld<u32>(slot + kSlotPointerCount), 2u);
    CHECK_EQ(ld<u32>(kSlotCountAddress), 1u);
    CHECK_EQ(ld<u32>(kRunningTotalAddress), 44u);
}

// Only the first sub-object: pointer at +4, +8 keeps its stale word, total + 22.
void first_subobject_only() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(3, 100, 1, 0);
    const u32 e = make_entry(1, {1, 1, 0});
    snapshot();
    const u32 slot = slot_address(3);
    expect32(slot + kSlotPointerCount, 1);
    expect32(slot + kSlotFirstPointer, e + kEntryFirstSubobject);
    expect32(kRunningTotalAddress, 122);
    expect32(slot + kSlotOwner, e);
    expect32(kSlotCountAddress, 4);
    call(e);
    ram_matches_prediction("first only");
    CHECK_EQ(ld<u32>(slot + kSlotFirstPointer + 4), 0x5C000003u);   // stale second pointer kept
}

// Only the second sub-object: its pointer lands at +4, not at a fixed +8.
void second_subobject_only_lands_in_the_first_position() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(7, 0, 1, 0);
    const u32 e = make_entry(2, {1, 0, 1});
    snapshot();
    const u32 slot = slot_address(7);
    expect32(slot + kSlotPointerCount, 1);
    expect32(slot + kSlotFirstPointer, e + kEntrySecondSubobject);
    expect32(kRunningTotalAddress, 22);
    expect32(slot + kSlotOwner, e);
    expect32(kSlotCountAddress, 8);
    call(e);
    ram_matches_prediction("second only");
    CHECK_EQ(ld<u32>(slot + kSlotFirstPointer), e + kEntrySecondSubobject);
    CHECK_EQ(ld<u32>(slot + kSlotFirstPointer + 4), 0x5C000007u);
}

// Enable byte zero: the second sub-object is ignored even though its active word is set.
void second_subobject_disabled_by_the_enable_byte() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(0, 0, 0, 0);
    const u32 e = make_entry(0, {1, 1, 1});
    snapshot();
    const u32 slot = slot_address(0);
    expect32(slot + kSlotPointerCount, 1);
    expect32(slot + kSlotFirstPointer, e + kEntryFirstSubobject);
    expect32(kRunningTotalAddress, 22);
    expect32(slot + kSlotOwner, e);
    expect32(kSlotCountAddress, 1);
    call(e);
    ram_matches_prediction("enable zero");
}

// Enable byte zero and first inactive: only the +12 clear happens; owner and count untouched.
void nothing_appended_only_clears_the_pointer_count() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(5, 999, 0, 0);
    const u32 e = make_entry(0, {1, 0, 1});
    snapshot();
    const u32 slot = slot_address(5);
    expect32(slot + kSlotPointerCount, 0);
    call(e);
    ram_matches_prediction("nothing appended");
    CHECK_EQ(ld<u32>(slot + kSlotOwner), 0x5A000005u);
    CHECK_EQ(ld<u32>(slot + kSlotFirstPointer), 0x5B000005u);
    CHECK_EQ(ld<u32>(kSlotCountAddress), 5u);
    CHECK_EQ(ld<u32>(kRunningTotalAddress), 999u);

    // Same with the enable byte set and both active words zero.
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(5, 999, 1, 1);
    const u32 e2 = make_entry(0, {1, 0, 0});
    snapshot();
    expect32(slot + kSlotPointerCount, 0);
    call(e2);
    ram_matches_prediction("nothing appended, enable set");
}

// Unowned entry with the skip flag set: no write at all, not even the +12 clear.
void unowned_entry_is_skipped_without_any_write() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(2, 50, 1, 1);
    const u32 e = make_entry(0, {0, 1, 1});
    snapshot();
    call(e);
    ram_matches_prediction("skipped");
    CHECK_EQ(ld<u32>(slot_address(2) + kSlotPointerCount), 0x5D000002u);
    CHECK_EQ(ld<u32>(kSlotCountAddress), 2u);

    // Any non-zero skip byte counts, including ones with the top bit set.
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(2, 50, 1, 0x80);
    const u32 e2 = make_entry(0, {0, 1, 1});
    snapshot();
    call(e2);
    ram_matches_prediction("skipped, flag 0x80");
}

// Unowned entry with the skip flag clear: registered like any other.
void unowned_entry_is_registered_when_the_skip_flag_is_clear() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(0, 0, 1, 0);
    const u32 e = make_entry(0, {0, 1, 1});
    snapshot();
    const u32 slot = slot_address(0);
    expect32(slot + kSlotPointerCount, 2);
    expect32(slot + kSlotFirstPointer, e + kEntryFirstSubobject);
    expect32(slot + kSlotFirstPointer + 4, e + kEntrySecondSubobject);
    expect32(kRunningTotalAddress, 44);
    expect32(slot + kSlotOwner, e);
    expect32(kSlotCountAddress, 1);
    call(e);
    ram_matches_prediction("unowned, skip clear");
}

// Owned entries never consult the skip flag: an owner byte of 0x80 with the skip flag set still
// registers. (The non-read itself is only observable in an access trace; the outcome is checked.)
void owned_entry_ignores_the_skip_flag() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(0, 0, 1, 1);
    const u32 e = make_entry(0, {0x80, 1, 0});
    snapshot();
    const u32 slot = slot_address(0);
    expect32(slot + kSlotPointerCount, 1);
    expect32(slot + kSlotFirstPointer, e + kEntryFirstSubobject);
    expect32(kRunningTotalAddress, 22);
    expect32(slot + kSlotOwner, e);
    expect32(kSlotCountAddress, 1);
    call(e);
    ram_matches_prediction("owner 0x80");
}

// The active tests are on the full 32-bit word: a word with a zero low byte, or only bit 31, is active.
void active_words_are_tested_as_whole_words() {
    const u32 patterns[] = {0x80000000u, 0x00000100u, 0x00010000u, 0xFFFFFFFFu, 0x00000001u};
    for (u32 pattern : patterns) {
        clear();
        fill_pool_with_stale(kInferredCapacity);
        set_globals(0, 0, 0x80, 0);
        const u32 e = make_entry(0, {1, pattern, pattern});
        snapshot();
        const u32 slot = slot_address(0);
        expect32(slot + kSlotPointerCount, 2);
        expect32(slot + kSlotFirstPointer, e + kEntryFirstSubobject);
        expect32(slot + kSlotFirstPointer + 4, e + kEntrySecondSubobject);
        expect32(kRunningTotalAddress, 44);
        expect32(slot + kSlotOwner, e);
        expect32(kSlotCountAddress, 1);
        call(e);
        ram_matches_prediction("active pattern");
    }
}

// The running total is a plain 32-bit add, so it wraps.
void running_total_wraps_at_32_bits() {
    clear();
    set_globals(0, 0xFFFFFFF0u, 1, 0);
    const u32 e = make_entry(0, {});
    snapshot();
    const u32 slot = slot_address(0);
    expect32(slot + kSlotPointerCount, 2);
    expect32(slot + kSlotFirstPointer, e + kEntryFirstSubobject);
    expect32(slot + kSlotFirstPointer + 4, e + kEntrySecondSubobject);
    expect32(kRunningTotalAddress, 0xFFFFFFF0u + 44u);   // 0x0000001C after the wrap
    expect32(slot + kSlotOwner, e);
    expect32(kSlotCountAddress, 1);
    call(e);
    ram_matches_prediction("total wrap");
    CHECK_EQ(ld<u32>(kRunningTotalAddress), 0x1Cu);
}

// Last slot in the inferred capacity (count 23) and one past it (count 24): no clamp, the write
// goes to 0x01D9D190 exactly as retail would.
void no_bounds_check_on_the_slot_count() {
    for (u32 count : {23u, 24u, 100u}) {
        clear();
        fill_pool_with_stale(kInferredCapacity + 1);
        set_globals(count, 0, 1, 0);
        const u32 e = make_entry(0, {});
        snapshot();
        const u32 slot = slot_address(count);
        expect32(slot + kSlotPointerCount, 2);
        expect32(slot + kSlotFirstPointer, e + kEntryFirstSubobject);
        expect32(slot + kSlotFirstPointer + 4, e + kEntrySecondSubobject);
        expect32(kRunningTotalAddress, 44);
        expect32(slot + kSlotOwner, e);
        expect32(kSlotCountAddress, count + 1);
        call(e);
        ram_matches_prediction("unclamped count");
    }
    CHECK_EQ(slot_address(24), 0x01D9D190u);
    CHECK_EQ(slot_address(23), 0x01D9D180u);
}

// The slot address is 32-bit arithmetic: a count of 0x10000000 shifts to zero and lands on slot 0,
// and a count of 0xFFFFFFFF addresses the 16 bytes below the pool; the count then wraps to zero.
void slot_address_wraps_at_32_bits() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(0x10000000u, 0, 1, 0);
    const u32 e = make_entry(0, {});
    snapshot();
    const u32 slot = kPoolBase;
    expect32(slot + kSlotPointerCount, 2);
    expect32(slot + kSlotFirstPointer, e + kEntryFirstSubobject);
    expect32(slot + kSlotFirstPointer + 4, e + kEntrySecondSubobject);
    expect32(kRunningTotalAddress, 44);
    expect32(slot + kSlotOwner, e);
    expect32(kSlotCountAddress, 0x10000001u);
    call(e);
    ram_matches_prediction("count 0x10000000");

    clear();
    set_globals(0xFFFFFFFFu, 0, 1, 0);
    const u32 e2 = make_entry(0, {1, 1, 0});
    snapshot();
    const u32 below = kPoolBase - 16u;
    expect32(below + kSlotPointerCount, 1);
    expect32(below + kSlotFirstPointer, e2 + kEntryFirstSubobject);
    expect32(kRunningTotalAddress, 22);
    expect32(below + kSlotOwner, e2);
    expect32(kSlotCountAddress, 0);
    call(e2);
    ram_matches_prediction("count -1");
}

// A stale +12 is cleared before the appends, so the stale value never feeds the pointer position.
void stale_pointer_count_does_not_leak_into_the_pointer_position() {
    clear();
    set_globals(0, 0, 1, 0);
    st<u32>(kPoolBase + kSlotPointerCount, 0x00000077u);
    const u32 e = make_entry(0, {1, 1, 0});
    snapshot();
    expect32(kPoolBase + kSlotPointerCount, 1);
    expect32(kPoolBase + kSlotFirstPointer, e + kEntryFirstSubobject);
    expect32(kRunningTotalAddress, 22);
    expect32(kPoolBase + kSlotOwner, e);
    expect32(kSlotCountAddress, 1);
    call(e);
    ram_matches_prediction("stale +12");
}

// The caller's shape: a reset (count 0, total 0, two flag bytes) then 24 entries of mixed states.
// Entries with nothing to append do not advance the count, so later entries pack down.
void caller_sequence_packs_the_pool() {
    clear();
    fill_pool_with_stale(kInferredCapacity);
    set_globals(0, 0, 1, 1);
    u32 entries[kInferredCapacity];
    u32 expected_count = 0, expected_total = 0;
    for (u32 i = 0; i < kInferredCapacity; ++i) {
        // Cycle through the five observable states.
        EntrySetup s;
        switch (i % 5) {
        case 0: s = {1, 1, 1}; break;          // both
        case 1: s = {1, 1, 0}; break;          // first only
        case 2: s = {1, 0, 1}; break;          // second only
        case 3: s = {1, 0, 0}; break;          // nothing appended, slot reused
        default: s = {0, 1, 1}; break;         // unowned and skipped
        }
        entries[i] = make_entry(i, s);
    }
    snapshot();
    for (u32 i = 0; i < kInferredCapacity; ++i) {
        const EntrySetup s = {static_cast<u8>(i % 5 == 4 ? 0 : 1), static_cast<u32>(i % 5 == 0 || i % 5 == 1),
                              static_cast<u32>(i % 5 == 0 || i % 5 == 2)};
        if (s.owned == 0) continue;                                  // skipped, no writes
        const u32 slot = slot_address(expected_count);
        u32 k = 0;
        expect32(slot + kSlotPointerCount, 0);
        if (s.first_active) {
            expect32(slot + kSlotPointerCount, ++k);
            expect32(slot + kSlotFirstPointer + 4 * (k - 1), entries[i] + kEntryFirstSubobject);
            expected_total += kUnitsPerSubobject;
            expect32(kRunningTotalAddress, expected_total);
        }
        if (s.second_active) {
            expect32(slot + kSlotPointerCount, ++k);
            expect32(slot + kSlotFirstPointer + 4 * (k - 1), entries[i] + kEntrySecondSubobject);
            expected_total += kUnitsPerSubobject;
            expect32(kRunningTotalAddress, expected_total);
        }
        if (k != 0) {
            expect32(slot + kSlotOwner, entries[i]);
            expect32(kSlotCountAddress, ++expected_count);
        }
    }
    for (u32 i = 0; i < kInferredCapacity; ++i) call(entries[i]);
    ram_matches_prediction("caller sequence");
    // 24 entries: 5 both (i=0,5,10,15,20), 5 first (1,6,11,16,21), 5 second (2,7,12,17,22), 5 empty
    // (3,8,13,18,23), 4 skipped (4,9,14,19). Count = 15 slots, total = (5*2 + 5 + 5) * 22 = 440.
    CHECK_EQ(expected_count, 15u);
    CHECK_EQ(ld<u32>(kSlotCountAddress), 15u);
    CHECK_EQ(ld<u32>(kRunningTotalAddress), 440u);
    CHECK_EQ(ld<u32>(slot_address(0) + kSlotOwner), entries[0]);
    CHECK_EQ(ld<u32>(slot_address(1) + kSlotOwner), entries[1]);
    CHECK_EQ(ld<u32>(slot_address(2) + kSlotOwner), entries[2]);
    CHECK_EQ(ld<u32>(slot_address(3) + kSlotOwner), entries[5]);      // 3 reused the slot, 4 skipped
    CHECK_EQ(ld<u32>(slot_address(3) + kSlotPointerCount), 2u);
    CHECK_EQ(ld<u32>(slot_address(15) + kSlotPointerCount), 0u);      // the last empty entry cleared slot 15
    CHECK_EQ(ld<u32>(slot_address(15) + kSlotOwner), 0x5A00000Fu);     // but did not own it
}

// The guest_ptr argument steps by the caller's 432-byte stride.
void entry_pointer_has_the_caller_stride() {
    const guest_ptr<VehicleRenderEntry> p{kEntry};
    CHECK_EQ((p + 1).address, kEntry + 432u);
    CHECK_EQ((p + 23).address, kEntry + 23u * 432u);
}

}

int main() {
    both_subobjects_fill_the_slot();
    first_subobject_only();
    second_subobject_only_lands_in_the_first_position();
    second_subobject_disabled_by_the_enable_byte();
    nothing_appended_only_clears_the_pointer_count();
    unowned_entry_is_skipped_without_any_write();
    unowned_entry_is_registered_when_the_skip_flag_is_clear();
    owned_entry_ignores_the_skip_flag();
    active_words_are_tested_as_whole_words();
    running_total_wraps_at_32_bits();
    no_bounds_check_on_the_slot_count();
    slot_address_wraps_at_32_bits();
    stale_pointer_count_does_not_leak_into_the_pointer_position();
    caller_sequence_packs_the_pool();
    entry_pointer_has_the_caller_stride();
    return check::done("test_slot_pool_register_entry");
}
