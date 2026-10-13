// The fork's hook registry alone: serving a call, counting an entry once whether it comes through dispatch()
// or straight from generated code, and shadow mode, in which the recompiled function and the hook both run on
// a call and are compared. The "recompiled function" here is a function of this file standing in for a
// generated unit; it is entered the way generated code enters one.
//
// fork-source: runtime/src/hooks.cpp

#include "check.hpp"

#include "kernel.hpp"
#include "ps2/generated.hpp"
#include "ps2/harness.hpp"
#include "ps2/hook.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

constexpr std::uint32_t kFunction = 0x00123400u, kOther = 0x00125600u, kThird = 0x00127800u, kReturn = 0x00200008u;
constexpr std::uint32_t kCell = 0x00300000u;   // where the function stores a byte

std::vector<std::uint32_t> g_entered;
std::uint64_t g_preempts = 0;
bool g_events_inside = false;   // the stand-in reports that the runtime ran events during the call
int g_bodies = 0;               // how often the stand-in's body ran

// The stand-in for a generated unit: the hook check of a function entry, then the function.
// v0 = a0 + 1, a byte stored at kCell, a scratch register left behind, five cycles used.
void unit(ps2::Cpu &c, std::uint32_t address) {
    if (ps2::g_hooks && ps2::enter(c, address)) return;
    ++g_bodies;
    c.r[2].d[0] = c.r[4].d[0] + 1;
    c.r[8].d[0] = 0x1234;
    ps2::g_ram[kCell] = 0x55;
    c.budget -= 5;
    if (g_events_inside) ++g_preempts;
    c.pc = c.r[31].w[0];
}

void dispatch(ps2::Cpu &c, std::uint32_t address) {
    if (ps2::g_hooks && ps2::offer(c, address)) return;
    unit(c, address);
}

void right(ps2::Cpu &c) {
    c.r[2].d[0] = c.r[4].d[0] + 1;
    ps2::g_ram[kCell] = 0x55;
}

void stores_another_byte(ps2::Cpu &c) {
    c.r[2].d[0] = c.r[4].d[0] + 1;
    ps2::g_ram[kCell] = 0x66;
}

void changes_a_kept_register(ps2::Cpu &c) {
    right(c);
    c.r[16].d[0] ^= 1;
}

}

namespace ps2 {

std::uint8_t g_ram[kRamSize];
std::uint8_t g_spr[kScratchpadSize];

bool save_state(const std::string &) { return true; }
std::uint64_t frame_count() { return 0; }
std::uint64_t preempt_count() { return g_preempts; }
void call(Cpu &c, std::uint32_t target, std::uint32_t) {
    c.pc = target;
    ::dispatch(c, target);
}

namespace kernel {
bool in_handler() { return false; }
}

namespace harness {
bool g_active = false;
void entered(std::uint32_t address) { g_entered.push_back(address); }
void hooked(std::uint32_t) {}
}

}

namespace {

std::string g_log;   // stderr goes here for the whole run

// What the registry has printed since the last call.
std::string printed() {
    static std::streamoff seen = 0;
    std::fflush(stderr);
    std::ifstream in(g_log, std::ios::binary);
    in.seekg(seen);
    std::ostringstream text;
    text << in.rdbuf();
    seen += static_cast<std::streamoff>(text.str().size());
    return text.str();
}

std::string report() {
    printed();
    ps2::report_hooks();
    return printed();
}

bool has(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

ps2::Hook g_hook{kFunction, "stand_in", &right};

// A fresh registry state: one hook registered at kFunction, enabled; shadow as given.
ps2::Cpu fresh(ps2::HookFn fn, std::uint32_t shadow) {
    g_hook.fn = fn;
    ps2::register_hooks(&g_hook, 1);
    ps2::enable_hook("stand_in");
    ps2::set_hook_shadow(shadow);
    ps2::set_entry_trace(false);
    g_entered.clear();
    g_events_inside = false;
    g_bodies = 0;
    ps2::g_ram[kCell] = 0;
    ps2::Cpu c{};
    c.r[4].d[0] = 5;
    c.r[16].d[0] = 0x77;
    c.r[31].d[0] = kReturn;
    c.r[29].d[0] = 0x01F00000;
    c.budget = 100;
    return c;
}

void a_registered_hook_serves_nothing_until_it_is_enabled() {
    g_hook.fn = &right;
    ps2::register_hooks(&g_hook, 1);
    ps2::set_hook_shadow(0);
    ps2::set_entry_trace(false);
    CHECK(!ps2::g_hooks);
    CHECK(!ps2::enable_hook("no_such_hook"));
    CHECK(ps2::enable_hook("0x00123400"));
    CHECK(ps2::g_hooks);
}

void an_enabled_hook_serves_a_call_from_generated_code_and_from_dispatch() {
    ps2::Cpu c = fresh(&right, 0);
    unit(c, kFunction);
    CHECK_EQ(g_bodies, 0);                    // the function itself did not run
    CHECK_EQ(c.r[2].d[0], 6u);
    CHECK_EQ(c.pc, kReturn);
    CHECK_EQ(c.budget, 100);                  // a hook uses no cycles
    CHECK_EQ(c.r[8].d[0], 0u);                // and leaves no scratch register behind
    c.r[4].d[0] = 9;
    c.pc = 0;
    dispatch(c, kFunction);
    CHECK_EQ(g_bodies, 0);
    CHECK_EQ(c.r[2].d[0], 10u);
    CHECK_EQ(c.pc, kReturn);
    CHECK(has(report(), "stand_in: 2 calls"));
}

void an_entry_is_counted_once_however_it_is_reached() {
    ps2::Cpu c = fresh(&right, 0);
    ps2::set_entry_trace(true);
    dispatch(c, kOther);                       // dispatch() offers it, the unit's own check must not count it again
    CHECK_EQ(g_entered.size(), 1u);
    unit(c, kOther);                           // two direct calls: two entries
    unit(c, kOther);
    CHECK_EQ(g_entered.size(), 3u);
    ps2::offer(c, kThird);                     // an address dispatch() offered that has no check of its own...
    unit(c, kOther);                           // ...does not swallow the next entry
    CHECK_EQ(g_entered.size(), 5u);
    CHECK_EQ(g_entered.back(), kOther);
    CHECK_EQ(g_bodies, 4);
}

void shadow_mode_runs_both_and_keeps_what_the_recompiled_function_did() {
    ps2::Cpu c = fresh(&right, 1);
    unit(c, kFunction);
    CHECK_EQ(g_bodies, 1);
    CHECK_EQ(c.r[2].d[0], 6u);
    CHECK_EQ(c.r[8].d[0], 0x1234u);            // the scratch register only the recompiled function writes
    CHECK_EQ(c.budget, 95);                    // and its cycles
    CHECK_EQ(c.pc, kReturn);
    CHECK_EQ(ps2::g_ram[kCell], 0x55);
    const std::string text = report();
    CHECK(has(text, "1 calls, 1 compared: 0 wrong"));
    CHECK(has(text, "v0 differs in 0, f0 in 0, other scratch registers in 1; 0 not compared"));
}

void shadow_mode_reports_a_hook_that_stores_something_else() {
    ps2::Cpu c = fresh(&stores_another_byte, 1);
    printed();
    unit(c, kFunction);
    CHECK(has(printed(), "stand_in call 1 wrong (recompiled/hook): memory at 0x00300000 55/66"));
    CHECK_EQ(ps2::g_ram[kCell], 0x55);         // the recompiled outcome stays
    CHECK(has(report(), "1 compared: 1 wrong"));
}

void shadow_mode_reports_a_hook_that_changes_a_register_it_must_keep() {
    ps2::Cpu c = fresh(&changes_a_kept_register, 1);
    printed();
    unit(c, kFunction);
    CHECK(has(printed(), "wrong (recompiled/hook): s0 77/76"));
    CHECK_EQ(c.r[16].d[0], 0x77u);
    CHECK(has(report(), "1 compared: 1 wrong"));
}

void shadow_mode_does_not_compare_a_call_the_runtime_ran_events_in() {
    ps2::Cpu c = fresh(&stores_another_byte, 1);
    g_events_inside = true;
    unit(c, kFunction);
    CHECK_EQ(g_bodies, 1);
    CHECK_EQ(c.r[2].d[0], 6u);
    CHECK_EQ(c.budget, 95);
    CHECK_EQ(ps2::g_ram[kCell], 0x55);
    CHECK(has(report(), "1 calls, 0 compared: 0 wrong (kept registers or memory), v0 differs in 0, f0 in 0, other scratch registers in 0; 1 not compared"));
}

void shadow_mode_compares_every_nth_call_and_the_function_serves_them_all() {
    ps2::Cpu c = fresh(&right, 3);
    for (int i = 0; i < 6; ++i) {
        c.pc = 0;
        unit(c, kFunction);
        CHECK_EQ(c.pc, kReturn);
    }
    CHECK_EQ(g_bodies, 6);
    CHECK_EQ(c.budget, 70);
    CHECK(has(report(), "6 calls, 2 compared: 0 wrong"));
}

}

int main() {
    char path[] = "/tmp/b3-test-hooks-XXXXXX";
    const int file = mkstemp(path);
    if (file < 0) return 2;
    g_log = path;
    dup2(file, 2);
    a_registered_hook_serves_nothing_until_it_is_enabled();
    an_enabled_hook_serves_a_call_from_generated_code_and_from_dispatch();
    an_entry_is_counted_once_however_it_is_reached();
    shadow_mode_runs_both_and_keeps_what_the_recompiled_function_did();
    shadow_mode_reports_a_hook_that_stores_something_else();
    shadow_mode_reports_a_hook_that_changes_a_register_it_must_keep();
    shadow_mode_does_not_compare_a_call_the_runtime_ran_events_in();
    shadow_mode_compares_every_nth_call_and_the_function_serves_them_all();
    unlink(path);
    return check::done("test_hooks");
}
