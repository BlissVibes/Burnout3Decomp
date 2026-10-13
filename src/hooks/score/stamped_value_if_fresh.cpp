// stamped_value_if_fresh (retail 0x001553A0): see stamped_value_if_fresh.hpp for the description,
// evidence and status (draft, oracle pending).
#include "hooks/score/stamped_value_if_fresh.hpp"

namespace b3::score {

static_assert(kNeverStamped.bits == 0xBF800000u);

std::uint32_t stamped_value_if_fresh(ee::guest_ptr<StampedSlotOwner> owner, ee::f32 window) {
    // The reads stay in retail order and stay lazy: an empty slot reads nothing but the stamp, and
    // a stale one never reads the stored word.
    const ee::f32 stamp = owner.at<ee::f32>(kStampTimeOffset).load();
    if (stamp == kNeverStamped) return 0;  // EE equality: only the exact -1.0f pattern matches

    const ee::f32 clock = owner.at<ee::f32>(kClockOffset).load();
    const ee::f32 expiry = stamp + window;  // EE add: truncating, denormals flushed, saturating
    if (expiry < clock) return 0;          // EE ordered less-than; expiry == clock is still fresh

    return owner.at<ee::u32>(kStampedValueOffset).load();
}

}  // namespace b3::score
