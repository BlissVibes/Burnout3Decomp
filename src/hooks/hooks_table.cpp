#include "ps2/hook.hpp"

#include "ee/abi.hpp"
#include "hooks/audio/lowest_set_bit_index.hpp"
#include "hooks/audio/path_extension_after_dot.hpp"
#include "hooks/frontend/compare_wide_strings.hpp"
#include "hooks/load_screen_sprites/build_texture_uv_rect.hpp"
#include "hooks/physics/sap_compare_x.hpp"
#include "hooks/physics/select_gear_under_rev_limit.hpp"
#include "hooks/registry/find_key64_slot.hpp"
#include "hooks/ring_buffers/ring_index_advance.hpp"
#include "hooks/ring_buffers/ring_index_is_at_or_ahead.hpp"
#include "hooks/valuedb/valuedb_hash_key.hpp"

// Every decompiled function that can replace a recompiled one, and the shims that move its
// arguments and results between the EE registers and its C++ signature. The register contract of
// each shim is the "Shim note" of the function in docs/re/decomp-log-001.md. The table is
// explicit: a static library drops objects that are reached only through static initialisers.
namespace b3 {

void hook_pack_fixed16(ps2::Cpu &c);

namespace {

using ee::abi::arg;

// A guest address as a host pointer into EE RAM. Pointers the game stores inside its objects are
// guest addresses too and go through here again.
template <typename T>
T *host(std::uint32_t guest_address) { return reinterpret_cast<T *>(ps2::host_ram(guest_address)); }

void set_fpu_condition(ps2::Cpu &c, bool condition) {
    c.fcr31 = condition ? (c.fcr31 | ps2::kFpuCondition) : (c.fcr31 & ~ps2::kFpuCondition);
}

// 0x00145E70. a0 = descriptor, a1 = index, a2 = delta (low 16 bits, signed). A zero delta hands the
// whole a1 register back and reads nothing; otherwise v0 is the 16-bit result, zero-extended.
void hook_ring_index_advance(ps2::Cpu &c) {
    const auto delta = static_cast<std::int16_t>(arg<std::uint16_t>(c, 2));
    if (delta == 0) {
        ee::abi::ret64(c, c.r[5].d[0]);
        return;
    }
    const auto descriptor = arg<std::uint32_t>(c, 0);
    const ring_buffers::RingDesc desc{ps2::ld32(descriptor), host<const ring_buffers::RingHeader>(ps2::ld32(descriptor + 4u))};
    ee::abi::ret_zx32(c, ring_buffers::ring_index_advance(&desc, arg<std::uint16_t>(c, 1), delta));
}

// 0x001478C0. a0 = descriptor (the header address is its second word), a1 and a2 = the indices.
void hook_ring_index_is_at_or_ahead(ps2::Cpu &c) {
    const auto *header = host<const ring_buffers::RingHeaderView>(ps2::ld32(arg<std::uint32_t>(c, 0) + 4u));
    ee::abi::ret_sx32(c, static_cast<std::uint32_t>(
                             ring_buffers::ring_index_is_at_or_ahead(*header, arg<std::uint32_t>(c, 1), arg<std::uint32_t>(c, 2))));
}

// 0x001B9820. a0, a1 = two records; the keys are their first words. The original's compare leaves
// the FPU condition bit set exactly when it answers -1.
void hook_sap_compare_x(ps2::Cpu &c) {
    const std::int32_t order = physics::sap_compare_x(ps2::ld32(arg<std::uint32_t>(c, 0)), ps2::ld32(arg<std::uint32_t>(c, 1)));
    set_fpu_condition(c, order < 0);
    ee::abi::ret_sx32(c, static_cast<std::uint32_t>(order));
}

// 0x001C6F30. a0 = transmission block, f12 = divisor, f13 = dividend; f3 is whatever the caller
// left there and is what gets stored when the loop never runs. The divide-by-zero flags are sticky:
// ORed into fcr31, never assigned. When the loop ran, the condition bit holds its last test, which
// was true exactly when an entry qualified (the result is then below the count).
void hook_select_gear_under_rev_limit(ps2::Cpu &c) {
    auto *block = host<std::uint32_t>(arg<std::uint32_t>(c, 0));
    const auto count = static_cast<std::int32_t>(block[physics::kGearCountWord]);
    bool divide_by_zero = false;
    const std::int32_t gear = physics::select_gear_under_rev_limit(block, c.f[12], c.f[13], c.f[3], &divide_by_zero);
    if (divide_by_zero) c.fcr31 |= 0x30000u;
    if (count > 1) set_fpu_condition(c, gear < count);
    ee::abi::ret_sx32(c, static_cast<std::uint32_t>(gear));
}

// 0x0021BFA0. a0 = the key string.
void hook_valuedb_hash_key(ps2::Cpu &c) {
    ee::abi::ret_sx32(c, static_cast<std::uint32_t>(valuedb::valuedb_hash_key(host<const char>(arg<std::uint32_t>(c, 0)))));
}

// 0x002518E0. a0 = the table object, a1 = the key, all 64 bits of the register.
void hook_find_key64_slot(ps2::Cpu &c) {
    const auto *table = host<const registry::KeyTable>(arg<std::uint32_t>(c, 0));
    ee::abi::ret_sx32(c, static_cast<std::uint32_t>(registry::find_key64_slot(*table, arg<std::uint64_t>(c, 1))));
}

// 0x002934D0. a0 = the path. No dot: the whole a0 register comes back; otherwise a 32-bit guest
// address, sign-extended.
void hook_path_extension_after_dot(ps2::Cpu &c) {
    const auto path = arg<std::uint32_t>(c, 0);
    const char *start = host<const char>(path);
    const char *extension = audio::path_extension_after_dot(start);
    if (extension == start) ee::abi::ret64(c, c.r[4].d[0]);
    else ee::abi::ret_sx32(c, path + static_cast<std::uint32_t>(extension - start));
}

// 0x0035EFA0. a0, a1 = the two strings of 16-bit units.
void hook_compare_wide_strings(ps2::Cpu &c) {
    ee::abi::ret_sx32(c, static_cast<std::uint32_t>(frontend::compare_wide_strings(host<const std::uint16_t>(arg<std::uint32_t>(c, 0)),
                                                                                    host<const std::uint16_t>(arg<std::uint32_t>(c, 1)))));
}

// 0x003713A0. a0 = the four output floats, a1 = flip flags, f12-f17 = x, y, w, h, texture width
// and height. Nothing is returned in v0; a zero divisor ORs the sticky flags into fcr31.
void hook_build_texture_uv_rect(ps2::Cpu &c) {
    if (load_screen_sprites::build_texture_uv_rect(host<std::uint32_t>(arg<std::uint32_t>(c, 0)), arg<std::uint32_t>(c, 1),
                                                   c.f[12], c.f[13], c.f[14], c.f[15], c.f[16], c.f[17]))
        c.fcr31 |= load_screen_sprites::kDivideByZeroFlags;
}

const ps2::Hook kHooks[] = {
    {0x00145E70u, "ring_index_advance", &hook_ring_index_advance},
    {0x001478C0u, "ring_index_is_at_or_ahead", &hook_ring_index_is_at_or_ahead},
    {0x001B9820u, "sap_compare_x", &hook_sap_compare_x},
    {0x001C6F30u, "select_gear_under_rev_limit", &hook_select_gear_under_rev_limit},
    {0x0021BFA0u, "valuedb_hash_key", &hook_valuedb_hash_key},
    {0x002518E0u, "find_key64_slot", &hook_find_key64_slot},
    {0x002934D0u, "path_extension_after_dot", &hook_path_extension_after_dot},
    {0x00299550u, "lowest_set_bit_index", &ee::abi::hook<&audio::lowest_set_bit_index>},
    {0x0035EFA0u, "compare_wide_strings", &hook_compare_wide_strings},
    {0x003713A0u, "build_texture_uv_rect", &hook_build_texture_uv_rect},
    {0x00389700u, "pack_fixed16", &hook_pack_fixed16},
};

}

}

void register_hooks() { ps2::register_hooks(b3::kHooks, sizeof b3::kHooks / sizeof b3::kHooks[0]); }
