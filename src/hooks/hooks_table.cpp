#include "ps2/hook.hpp"

// Every decompiled function that can replace a recompiled one. The table is explicit: a static
// library drops objects that are reached only through static initialisers.
namespace b3 {
void hook_pack_fixed16(ps2::Cpu &c);
}

namespace {

const ps2::Hook kHooks[] = {
    {0x00389700u, "pack_fixed16", &b3::hook_pack_fixed16},
};

}

void register_hooks() { ps2::register_hooks(kHooks, sizeof kHooks / sizeof kHooks[0]); }
