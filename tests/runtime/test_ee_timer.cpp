// The fork's EE hardware registers alone: the four EE timers. A timer's compare and overflow are noticed by
// an event the runtime sets for the cycle they fall on. Generated code only stops for events at a backward
// branch after its time slice ran out, so an event runs some cycles late, and the counter may by then be
// past the value the event was set for. The compare or overflow still happened: its flag and its interrupt
// must come, once, whatever the lateness. All values here are made up; the mode 0x382 with compare 0 is
// what the game sets on timer 2 (one tick every 512 cycles, both interrupts on).
//
// fork-source: runtime/src/hw.cpp

#include "check.hpp"

#include "hw.hpp"

#include "dmac.hpp"
#include "gs/gif.hpp"
#include "gs/gs.hpp"
#include "iop/sif.hpp"
#include "ipu/ipu.hpp"
#include "kernel.hpp"
#include "vif.hpp"
#include "vu_micro.hpp"

#include "ps2/state.hpp"
#include "ps2/system.hpp"

#include <functional>
#include <map>

namespace {

struct Pending {
    std::uint64_t at;
    std::function<void()> action;
};
std::map<int, Pending> g_events;   // stands in for the runtime's event list: one event per id
std::uint64_t g_now = 0;

}

namespace ps2 {

Cpu g_cpu;
Options g_options;
std::uint64_t now() { return g_now; }
std::uint64_t hblank_cycles() { return 18880; }
void schedule(int event, std::uint64_t at, std::function<void()> action) {
    g_events.erase(event);
    if (at != ~0ull) g_events[event] = {at, std::move(action)};
}

namespace gs {
void reset() {}
void vblank(bool) {}
std::uint64_t read_privileged(std::uint32_t) { return 0; }
void write_privileged(std::uint32_t, std::uint64_t) {}
void read_image(std::uint8_t *, std::uint32_t) {}
}
namespace gif {
void reset() {}
std::uint32_t read(std::uint32_t) { return 0; }
void write(std::uint32_t, std::uint32_t) {}
void transfer(int, const std::uint8_t *, std::uint32_t) {}
}
namespace vif {
void reset() {}
std::uint32_t read(int, std::uint32_t) { return 0; }
void write(int, std::uint32_t, std::uint32_t) {}
void transfer(int, const std::uint8_t *, std::uint32_t) {}
}
namespace vu {
void reset() {}
}
namespace dmac {
void reset() {}
bool owns(std::uint32_t) { return false; }
std::uint32_t read(std::uint32_t) { return 0; }
void write(std::uint32_t, std::uint32_t) {}
}
namespace ipu {
void reset() {}
std::uint32_t read32(std::uint32_t) { return 0; }
void write32(std::uint32_t, std::uint32_t) {}
void write_fifo(const std::uint8_t *) {}
void read_fifo(std::uint8_t *) {}
}
namespace sif {
std::uint32_t get_reg(std::uint32_t) { return 0; }
void set_reg(std::uint32_t, std::uint32_t) {}
}
namespace kernel {
void wake_idle() {}
}

}

namespace {

namespace hw = ps2::hw;

constexpr int kTimer = 2;
constexpr std::uint32_t kCount = 0x10001000u, kMode = 0x10001010u, kComp = 0x10001020u;   // timer 2
constexpr std::uint32_t kIntcStat = 0x1000F000u, kTimerLine = 1u << (9 + kTimer);
constexpr std::uint32_t kCompareFlag = 0x400u, kOverflowFlag = 0x800u, kFlags = 0xC00u;
constexpr std::uint64_t kTick = 512, kWrap = 0x10000u * kTick;
constexpr std::uint64_t kStart = 1000;   // the cycle the timer is started on

constexpr int kEvent = hw::kEventTimer0 + kTimer;

void start(std::uint32_t mode, std::uint32_t comp) {
    hw::reset();
    g_events.clear();
    g_now = kStart;
    hw::write32(kComp, comp);
    hw::write32(kMode, mode);   // count 0 from here on
}

bool pending() { return g_events.count(kEvent) != 0; }
std::uint64_t due() { return g_events.at(kEvent).at; }

// Runs the timer's event `late` cycles after the cycle it was set for.
void run_event(std::uint64_t late) {
    Pending e = std::move(g_events.at(kEvent));
    g_events.erase(kEvent);
    g_now = e.at + late;
    e.action();
}

std::uint32_t flags() { return hw::read32(kMode) & kFlags; }
bool interrupt() { return hw::read32(kIntcStat) & kTimerLine; }

// What the game's handler does: acknowledges the line and clears the two flags by writing them back.
void acknowledge() {
    hw::write32(kIntcStat, kTimerLine);
    hw::write32(kMode, hw::read32(kMode));
}

void an_event_on_time_raises_the_wrap() {
    start(0x382, 0);
    CHECK_EQ(due(), kStart + kWrap);
    run_event(0);
    CHECK_EQ(flags(), kFlags);
    CHECK(interrupt());
    CHECK_EQ(due(), kStart + 2 * kWrap);
}

void a_late_event_still_raises_the_wrap() {
    for (std::uint64_t late : {1ull, 511ull, 512ull, 600ull, 3553ull, 20000ull}) {
        start(0x382, 0);
        run_event(late);
        CHECK_EQ(flags(), kFlags);
        CHECK(interrupt());
        CHECK_EQ(due(), kStart + 2 * kWrap);    // and the next one is not moved by the lateness
        acknowledge();
        const std::uint64_t next = due();
        run_event(late);
        CHECK_EQ(flags(), kFlags);
        CHECK(interrupt());
        CHECK_EQ(due(), next + kWrap);
    }
}

void a_late_compare_is_a_compare_and_a_late_overflow_an_overflow() {
    start(0x382, 0x8000);
    CHECK_EQ(due(), kStart + 0x8000 * kTick);
    run_event(3 * kTick + 7);
    CHECK_EQ(flags(), kCompareFlag);
    CHECK(interrupt());
    CHECK_EQ(due(), kStart + kWrap);
    acknowledge();
    run_event(2 * kTick);
    CHECK_EQ(flags(), kOverflowFlag);
    CHECK(interrupt());
}

void both_are_raised_when_the_event_runs_after_both() {
    start(0x382, 1);                      // overflow at count 0, compare one tick later
    CHECK_EQ(due(), kStart + kTick);      // the first compare, on time: no overflow has happened
    run_event(0);
    CHECK_EQ(flags(), kCompareFlag);
    acknowledge();
    CHECK_EQ(due(), kStart + kWrap);      // the overflow, 600 cycles late: the counter already reads 1
    run_event(600);
    CHECK_EQ(hw::read32(kCount), 1u);
    CHECK_EQ(flags(), kFlags);
    CHECK(interrupt());
    CHECK_EQ(due(), kStart + 2 * kWrap);  // the next overflow: the compare that was passed is not raised again
    acknowledge();
    run_event(0);
    CHECK_EQ(flags(), kOverflowFlag);
}

void an_interrupt_that_is_off_is_not_raised() {
    start(0x282, 0x8000);                 // overflow interrupt only
    CHECK_EQ(due(), kStart + kWrap);
    run_event(700);
    CHECK(flags() & kOverflowFlag);
    CHECK(interrupt());
    start(0x182, 0x8000);                 // compare interrupt only: the counter wraps unnoticed
    CHECK_EQ(due(), kStart + 0x8000 * kTick);
    run_event(700);
    CHECK_EQ(flags(), kCompareFlag);
    CHECK(interrupt());
    CHECK_EQ(due(), kStart + kWrap + 0x8000 * kTick);
    acknowledge();
    run_event(700);
    CHECK(flags() & kCompareFlag);
    CHECK(interrupt());
    start(0x082, 0);                      // neither: no event at all
    CHECK(!pending());
}

void a_counter_that_restarts_at_its_compare_keeps_time_when_the_event_is_late() {
    start(0x1C2, 100);                    // compare interrupt, back to 0 at the compare
    CHECK_EQ(due(), kStart + 100 * kTick);
    run_event(3 * kTick + 5);
    CHECK_EQ(flags(), kCompareFlag);
    CHECK(interrupt());
    CHECK_EQ(hw::read32(kCount), 3u);     // three ticks have passed since it went back to 0
    CHECK_EQ(due(), kStart + 200 * kTick);
    acknowledge();
    const std::uint64_t next = due();
    run_event(0);
    CHECK_EQ(hw::read32(kCount), 0u);
    CHECK_EQ(due(), next + 100 * kTick);
    acknowledge();
    run_event(250 * kTick);               // so late that it restarted twice more: 350 = 3 * 100 + 50
    CHECK_EQ(flags(), kCompareFlag);
    CHECK_EQ(hw::read32(kCount), 50u);
    CHECK_EQ(due(), next + 400 * kTick);
}

void a_register_write_does_not_swallow_a_wrap_already_passed() {
    start(0x382, 0);
    g_now = due() + 600;                  // the wrap has happened; its event has not run yet
    hw::write32(kComp, 0);
    CHECK_EQ(flags(), kFlags);
    CHECK(interrupt());
    CHECK_EQ(due(), kStart + 2 * kWrap);
    start(0x382, 0);
    g_now = due() - 600;                  // not yet: a write changes nothing
    hw::write32(kComp, 0);
    CHECK_EQ(flags(), 0u);
    CHECK(!interrupt());
    CHECK_EQ(due(), kStart + kWrap);
}

void a_loaded_state_knows_what_its_event_stands_for() {
    start(0x382, 0);
    run_event(0);
    acknowledge();
    g_now += 12345;
    ps2::state::Writer saved;
    hw::state(saved);
    const std::uint64_t at = due(), saved_at = g_now;
    hw::reset();
    g_events.clear();
    g_now = saved_at;                     // the runtime loads the time and its event list, then the hardware
    g_events[kEvent] = {at, hw::event_action(kEvent)};
    ps2::state::Reader reader(saved.out.data(), saved.out.size());
    hw::state(reader);
    run_event(600);
    CHECK_EQ(flags(), kFlags);
    CHECK(interrupt());
    CHECK_EQ(due(), kStart + 3 * kWrap);
}

}

int main() {
    an_event_on_time_raises_the_wrap();
    a_late_event_still_raises_the_wrap();
    a_late_compare_is_a_compare_and_a_late_overflow_an_overflow();
    both_are_raised_when_the_event_runs_after_both();
    an_interrupt_that_is_off_is_not_raised();
    a_counter_that_restarts_at_its_compare_keeps_time_when_the_event_is_late();
    a_register_write_does_not_swallow_a_wrap_already_passed();
    a_loaded_state_knows_what_its_event_stands_for();
    return check::done("test_ee_timer");
}
