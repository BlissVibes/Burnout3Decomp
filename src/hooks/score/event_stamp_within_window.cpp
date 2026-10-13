// event_stamp_within_window (retail 0x00156170): see event_stamp_within_window.hpp for the
// description, evidence and status (draft, oracle pending).
#include "hooks/score/event_stamp_within_window.hpp"

namespace b3::score {

std::int32_t event_stamp_within_window_at(ee::guest_ptr<EventStampOwner> owner, std::uint32_t stamp_offset,
                                          ee::f32 window) {
    // The reads stay in retail order and stay lazy: a negative stamp reads nothing else.
    const ee::guest_ptr<EventStampState> state = owner.at<ee::guest_ptr<EventStampState>>(kEventStateBlockOffset).load();
    const ee::f32 stamp = state.at<ee::f32>(stamp_offset).load();
    if (stamp < ee::f32::from_bits(0)) return 0;  // EE ordered less-than against +0.0: -0 and denormals are not negative

    const ee::f32 deadline = stamp + window;  // EE add: truncating, denormals flushed, saturating
    const ee::f32 clock = state.at<ee::f32>(kEventClockOffset).load();
    return clock <= deadline ? 1 : 0;         // EE ordered less-or-equal: inclusive at the deadline
}

std::int32_t event_stamp_within_window(ee::guest_ptr<EventStampOwner> owner, ee::f32 window) {
    return event_stamp_within_window_at(owner, kEventStampOffset, window);
}

}  // namespace b3::score
