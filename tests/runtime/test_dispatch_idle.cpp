// The fork's dispatcher alone: an "idle call". A game that waits in a loop, calling some function through a
// pointer on every turn, can be told to the runtime as (function, return address, register that is non-zero
// while it waits). Once two turns in a row have cost the same number of cycles, the runtime takes whole turns
// off the time slice at once and leaves the last one or two to run, so the slice ends exactly where it would
// have ended with the loop spinning.
//
// fork-source: runtime/src/dispatch.cpp

#include "check.hpp"

#include "ps2/generated.hpp"
#include "ps2/harness.hpp"
#include "ps2/system.hpp"

#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kBase = 0x00100000u, kCallback = 0x00100040u, kOther = 0x00100080u, kSite = 0x00100518u;

std::vector<std::int32_t> g_budgets;   // the budget each entry of the stand-in unit saw
std::uint16_t g_entries[0x100];

// Stands in for a generated unit: the function uses three cycles and returns.
void unit(ps2::Cpu &c, std::uint32_t, std::uint32_t) {
    g_budgets.push_back(c.budget);
    c.budget -= 3;
    c.pc = c.r[31].w[0];
}

}

namespace ps2 {

std::uint8_t g_ram[kRamSize];
std::uint8_t g_spr[kScratchpadSize];
Options g_options;
bool g_hooks = false;
bool offer(Cpu &, std::uint32_t) { return false; }
void halt(const std::string &) {}
std::uint32_t slow_ld32(std::uint32_t) { return 0; }

namespace harness {
bool g_active = false;
}

}

namespace {

ps2::Cpu call_from(std::uint32_t target, std::uint32_t site, std::uint64_t a0, std::int32_t budget) {
    ps2::Cpu c{};
    c.r[4].d[0] = a0;
    c.r[31].d[0] = site;
    c.budget = budget;
    ps2::call(c, target, 1);
    return c;
}

// One turn of a waiting loop that costs `turn` cycles: the call arrives with `budget` left.
std::int32_t turn_at(std::int32_t budget, std::uint64_t a0 = 1, std::uint32_t site = kSite, std::uint32_t target = kCallback) {
    g_budgets.clear();
    call_from(target, site, a0, budget);
    return g_budgets.at(0);
}

void whole_turns_are_skipped_once_two_turns_cost_the_same() {
    ps2::forget_idle_progress();
    CHECK_EQ(turn_at(5000), 5000);             // first turn of the slice: nothing known
    CHECK_EQ(turn_at(4979), 4979);             // one turn measured: 21 cycles
    const std::uint64_t before = ps2::idle_calls_taken();
    CHECK_EQ(turn_at(4958), 23);               // the same again: 235 turns of 21 go at once, between one and two turns stay
    CHECK_EQ(ps2::idle_calls_taken() - before, 235u);
    CHECK_EQ(turn_at(2), 2);                   // the last turn runs as it is; the loop's own check ends the slice
}

void the_caller_sees_the_slice_end_where_spinning_would_have_ended_it() {
    // With a check 11 cycles after the call, spinning from 4958 ends the slice at 4958 - 236 * 21 - 11 = -9.
    ps2::forget_idle_progress();
    turn_at(5000);
    turn_at(4979);
    std::int32_t budget = turn_at(4958);
    while (budget - 11 >= 0) budget -= 21;     // the turns that are left, spinning
    CHECK_EQ(budget - 11, -9);
}

void a_turn_of_another_length_starts_the_measuring_again() {
    ps2::forget_idle_progress();
    turn_at(5000);
    turn_at(4979);
    CHECK_EQ(turn_at(4955), 4955);             // 24 this time, not 21: not steady
    CHECK_EQ(turn_at(4931), 35);               // 24 twice: 204 turns of 24 go, 35 cycles stay
}

void a_new_time_slice_starts_the_measuring_again() {
    ps2::forget_idle_progress();
    turn_at(100);
    turn_at(79);
    CHECK_EQ(turn_at(20000), 20000);           // the budget went up: a new slice
    CHECK_EQ(turn_at(19979), 19979);
    CHECK_EQ(turn_at(19958), 19958 - 21 * ((19958 - 21) / 21));
}

void calls_that_do_not_match_are_left_alone_and_end_the_measuring() {
    ps2::forget_idle_progress();
    turn_at(5000);
    turn_at(4979);
    CHECK_EQ(turn_at(4958, 0), 4958);          // the register says the caller is not waiting any more
    CHECK_EQ(turn_at(4937), 4937);             // so the next waiting turn is a first turn again
    ps2::forget_idle_progress();
    turn_at(5000);
    turn_at(4979);
    CHECK_EQ(turn_at(4958, 1, kSite + 8), 4958);          // the same function called from somewhere else
    CHECK_EQ(turn_at(4958, 1, kSite, kOther), 4958);      // another function from the waiting loop's call
}

void a_slice_with_less_than_two_turns_left_is_not_touched() {
    ps2::forget_idle_progress();
    turn_at(80);
    turn_at(59);
    CHECK_EQ(turn_at(38), 38);
    CHECK_EQ(turn_at(17), 17);
    CHECK_EQ(turn_at(-4), -4);
}

void nothing_is_skipped_inside_a_harness_call() {
    ps2::forget_idle_progress();
    ps2::harness::g_active = true;
    turn_at(5000);
    turn_at(4979);
    CHECK_EQ(turn_at(4958), 4958);
    ps2::harness::g_active = false;
}

}

int main() {
    g_entries[(kCallback - kBase) >> 2] = 1;
    g_entries[(kOther - kBase) >> 2] = 2;
    static const ps2::UnitInfo units[] = {{kBase, 0x400, &unit, g_entries}};
    ps2::register_units(units, 1);
    ps2::add_idle_call(kCallback, kSite, 4);
    whole_turns_are_skipped_once_two_turns_cost_the_same();
    the_caller_sees_the_slice_end_where_spinning_would_have_ended_it();
    a_turn_of_another_length_starts_the_measuring_again();
    a_new_time_slice_starts_the_measuring_again();
    calls_that_do_not_match_are_left_alone_and_end_the_measuring();
    a_slice_with_less_than_two_turns_left_is_not_touched();
    nothing_is_skipped_inside_a_harness_call();
    return check::done("test_dispatch_idle");
}
