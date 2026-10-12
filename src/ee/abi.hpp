#pragma once

// The game's calling convention, for decompiled functions that replace recompiled ones.
//
// As observed in the generated code (docs/re/function-harness-spec.md, behaviour notes; the ELF is
// CodeWarrior-built): integer and pointer arguments arrive in a0-a3 then t0-t3, float arguments in
// f12-f19, each class counted on its own; results leave in v0 (and v1) or f0. Registers are 64 bits
// wide in this ABI and the comparison with the recompiled function is over all 64, so a result must
// carry the extension the original left:
//   - a 32-bit value produced by addu, lw, a shift or a move of such a value is sign-extended, whatever
//     its C type (0x80000000 is 0xFFFFFFFF80000000 in the register);
//   - a value produced by andi or ori with a zero-extended immediate keeps zeros above bit 31;
//   - a flag argument is tested with bnez/beqz, which looks at all 64 bits.
//
// Piece by piece:  abi::arg<T>(c, i)  abi::farg(c, i)  abi::ret_sx32  abi::ret_zx32  abi::ret64  abi::ret_f32
// Whole function:  abi::hook<&fn> is a ps2::HookFn for a typed function, e.g. for
//     u32 pack_fixed16(f32 x, s8 fraction_bits, bool sign_magnitude);
// it reads x from f12, fraction_bits from a0 and sign_magnitude from a1 and writes the result to v0.
// A leading ps2::Cpu & parameter receives the register file. The result type picks the extension:
// f32 goes to f0; 64-bit integers are written whole; abi::zx32 is zero-extended; bool is 0 or 1; every
// other integer and guest_ptr is sign-extended from 32 bits. A hook touches no other register.

#include "f32.hpp"
#include "guest.hpp"

#include "ps2/cpu.hpp"

#include <cstdint>
#include <tuple>
#include <type_traits>

namespace ee::abi {

inline constexpr int kFirstArgument = 4;        // a0
inline constexpr int kFirstFloatArgument = 12;  // f12
inline constexpr int kArgumentRegisters = 8;    // a0-a3, t0-t3 and f12-f19
enum ResultRegister { v0 = 2, v1 = 3 };

// The index-th integer argument (0-7) as a T: the register narrowed to T's width, a guest_ptr from its
// low word, or for bool whether any of the low 64 bits is set.
template <typename T>
inline T arg(const ps2::Cpu &c, int index) {
    const std::uint64_t value = c.r[kFirstArgument + index].d[0];
    if constexpr (std::is_same_v<T, bool>) {
        return value != 0;
    } else if constexpr (is_guest_ptr<T>) {
        return T{static_cast<std::uint32_t>(value)};
    } else {
        static_assert(std::is_integral_v<T>, "abi::arg: integer, bool or guest_ptr; floats come from abi::farg");
        return static_cast<T>(value);
    }
}

// The index-th float argument (0-7).
inline f32 farg(const ps2::Cpu &c, int index) { return f32::from_bits(c.f[kFirstFloatArgument + index]); }

inline void ret_sx32(ps2::Cpu &c, std::uint32_t value, ResultRegister reg = v0) {
    c.r[reg].sd[0] = static_cast<std::int32_t>(value);
}
inline void ret_zx32(ps2::Cpu &c, std::uint32_t value, ResultRegister reg = v0) { c.r[reg].d[0] = value; }
inline void ret64(ps2::Cpu &c, std::uint64_t value, ResultRegister reg = v0) { c.r[reg].d[0] = value; }
inline void ret_f32(ps2::Cpu &c, f32 value) { c.f[0] = value.bits; }

// A 32-bit result the original left zero-extended (its last instruction was an andi or ori).
struct zx32 {
    std::uint32_t value;
};

namespace detail {

template <typename T>
inline decltype(auto) take(ps2::Cpu &c, int &integers, int &floats) {
    if constexpr (std::is_same_v<T, ps2::Cpu &>) {
        return (c);
    } else if constexpr (std::is_same_v<T, f32>) {
        return farg(c, floats++);
    } else {
        return arg<T>(c, integers++);
    }
}

template <typename R>
inline void give(ps2::Cpu &c, R value) {
    if constexpr (std::is_same_v<R, f32>) {
        ret_f32(c, value);
    } else if constexpr (std::is_same_v<R, zx32>) {
        ret_zx32(c, value.value);
    } else if constexpr (is_guest_ptr<R>) {
        ret_sx32(c, value.address);
    } else if constexpr (std::is_same_v<R, bool>) {
        ret64(c, value ? 1u : 0u);
    } else {
        static_assert(std::is_integral_v<R>, "abi::hook: unsupported result type");
        if constexpr (sizeof(R) == 8) ret64(c, static_cast<std::uint64_t>(value));
        else ret_sx32(c, static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
    }
}

template <typename R, typename... A>
inline void invoke(ps2::Cpu &c, R (*fn)(A...)) {
    static_assert((0 + ... + (std::is_same_v<A, f32> ? 1 : 0)) <= kArgumentRegisters, "abi::hook: more than eight float arguments");
    static_assert((0 + ... + (std::is_same_v<A, f32> || std::is_same_v<A, ps2::Cpu &> ? 0 : 1)) <= kArgumentRegisters,
                  "abi::hook: more than eight integer arguments (the rest are on the stack)");
    [[maybe_unused]] int integers = 0, floats = 0;
    std::tuple<A...> arguments{take<A>(c, integers, floats)...};   // braces: evaluated left to right
    if constexpr (std::is_void_v<R>) std::apply(fn, arguments);
    else give<R>(c, std::apply(fn, arguments));
}

}

// void hook<&fn>(ps2::Cpu &): unpacks fn's arguments from the register file, calls it, packs the result.
template <auto Fn>
inline void hook(ps2::Cpu &c) { detail::invoke(c, Fn); }

}
