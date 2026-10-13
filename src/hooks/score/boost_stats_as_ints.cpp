// boost_stats_as_ints (retail 0x0014E7E0): three floats of the pointed-at record, truncated to
// ints, written through three out pointers. See boost_stats_as_ints.hpp for the description,
// evidence and layout. Status: draft (oracle pending).
#include "hooks/score/boost_stats_as_ints.hpp"

namespace b3::score {

namespace {

// One step of the getter: follow the record pointer held in the object, convert the float at
// `field_offset` with the EE's cvt.w.s and store the int. Kept as a step rather than hoisting the
// pointer load because retail re-reads the pointer every time and stores before it reads again,
// which only matters when an out pointer aliases the pointer field or a later field.
void store_field_as_int(ee::guest_ptr<void> object, std::uint32_t field_offset, ee::guest_ptr<std::int32_t> out) {
    const ee::guest_ptr<void> record = *object.at<ee::guest_ptr<void>>(kBoostStatsRecordPointerOffset);
    const ee::f32 value = *record.at<ee::f32>(field_offset);
    *out = value.to_s32();
}

}  // namespace

void boost_stats_as_ints(ee::guest_ptr<void> object,
                         ee::guest_ptr<std::int32_t> out_first,
                         ee::guest_ptr<std::int32_t> out_second,
                         ee::guest_ptr<std::int32_t> out_third) {
    // Descending field offsets into ascending out pointers; the order of the three steps is retail's.
    store_field_as_int(object, kBoostStatsFirstFieldOffset, out_first);
    store_field_as_int(object, kBoostStatsSecondFieldOffset, out_second);
    store_field_as_int(object, kBoostStatsThirdFieldOffset, out_third);
}

}  // namespace b3::score
