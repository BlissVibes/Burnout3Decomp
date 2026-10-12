#pragma once

// Typed access to guest (EE) memory for decompiled code.
//
// Decompiled functions run beside recompiled ones and share their data, so every object stays in the
// 32 MB guest RAM at its retail offset and is read and written through these accessors, never through
// a host struct. They sit on the fork's ld8..ld128 / st8..st128 (runtime/include/ps2/memory.hpp), so
// the scratchpad and hardware addresses behave as they do for recompiled code.
//
//   ee::ld<u16>(a), ee::st<f32>(a, v)     one scalar at a guest address, little-endian, any alignment
//   ee::guest_ptr<T>                      a 32-bit guest address with a pointee type:
//       *p, p[i]                          read (convert to T) or assign
//       p + n, p - n                      step by the guest size of T
//       p.at<U>(offset)                   the U field at a byte offset from p
//       p.cast<U>()                       the same address as another type
//
// T may be an integer type, ee::f32, ps2::Reg128 (a quadword), another guest_ptr (4 bytes, whatever
// the host pointer size), a struct tag, or void for an address of unknown type. Stepping a pointer to a struct needs its size:
//   struct Wheel;
//   template <> struct ee::guest_size<Wheel> : std::integral_constant<std::uint32_t, 0xC0> {};

#include "f32.hpp"

#include "ps2/cpu.hpp"
#include "ps2/memory.hpp"

#include <cstdint>
#include <type_traits>

namespace ee {

using u8 = std::uint8_t;
using s8 = std::int8_t;
using u16 = std::uint16_t;
using s16 = std::int16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
using u64 = std::uint64_t;
using s64 = std::int64_t;

template <typename T>
struct guest_ptr;

template <typename T>
inline constexpr bool is_guest_ptr = false;
template <typename T>
inline constexpr bool is_guest_ptr<guest_ptr<T>> = true;

// The number of bytes a T occupies in guest memory. Struct tags specialise it.
template <typename T>
struct guest_size;
template <typename T>
    requires std::is_integral_v<T>
struct guest_size<T> : std::integral_constant<std::uint32_t, sizeof(T)> {};
template <>
struct guest_size<f32> : std::integral_constant<std::uint32_t, 4> {};
template <>
struct guest_size<ps2::Reg128> : std::integral_constant<std::uint32_t, 16> {};
template <typename T>
struct guest_size<guest_ptr<T>> : std::integral_constant<std::uint32_t, 4> {};

template <typename T>
inline T ld(std::uint32_t address) {
    if constexpr (std::is_same_v<T, f32>) {
        return f32::from_bits(ps2::ld32(address));
    } else if constexpr (is_guest_ptr<T>) {
        return T{ps2::ld32(address)};
    } else if constexpr (std::is_same_v<T, ps2::Reg128>) {
        ps2::Reg128 value;
        ps2::ld128(address, value);
        return value;
    } else {
        static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "ee::ld: integer, f32, Reg128 or guest_ptr");
        if constexpr (sizeof(T) == 1) return static_cast<T>(ps2::ld8(address));
        else if constexpr (sizeof(T) == 2) return static_cast<T>(ps2::ld16(address));
        else if constexpr (sizeof(T) == 4) return static_cast<T>(ps2::ld32(address));
        else return static_cast<T>(ps2::ld64(address));
    }
}

template <typename T>
inline void st(std::uint32_t address, std::type_identity_t<T> value) {
    if constexpr (std::is_same_v<T, f32>) {
        ps2::st32(address, value.bits);
    } else if constexpr (is_guest_ptr<T>) {
        ps2::st32(address, value.address);
    } else if constexpr (std::is_same_v<T, ps2::Reg128>) {
        ps2::st128(address, value);
    } else {
        static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "ee::st: integer, f32, Reg128 or guest_ptr");
        if constexpr (sizeof(T) == 1) ps2::st8(address, static_cast<std::uint8_t>(value));
        else if constexpr (sizeof(T) == 2) ps2::st16(address, static_cast<std::uint16_t>(value));
        else if constexpr (sizeof(T) == 4) ps2::st32(address, static_cast<std::uint32_t>(value));
        else ps2::st64(address, static_cast<std::uint64_t>(value));
    }
}

// What *p and p[i] give: converts to T by loading, takes a T by storing.
template <typename T>
struct guest_ref {
    std::uint32_t address;

    operator T() const { return ld<T>(address); }
    const guest_ref &operator=(T value) const {
        st<T>(address, value);
        return *this;
    }
    const guest_ref &operator=(const guest_ref &other) const { return *this = static_cast<T>(other); }
};

template <typename T>
struct guest_ptr {
    std::uint32_t address = 0;

    constexpr explicit operator bool() const { return address != 0; }
    friend constexpr bool operator==(guest_ptr a, guest_ptr b) { return a.address == b.address; }

    T load() const { return ld<T>(address); }
    void store(std::type_identity_t<T> value) const { st<T>(address, value); }
    guest_ref<T> operator*() const { return {address}; }
    guest_ref<T> operator[](std::int32_t index) const { return {(*this + index).address}; }

    constexpr guest_ptr operator+(std::int32_t count) const {
        return {address + static_cast<std::uint32_t>(count) * guest_size<T>::value};
    }
    constexpr guest_ptr operator-(std::int32_t count) const {
        return {address - static_cast<std::uint32_t>(count) * guest_size<T>::value};
    }

    template <typename U>
    constexpr guest_ptr<U> at(std::uint32_t offset) const { return {address + offset}; }
    template <typename U>
    constexpr guest_ptr<U> cast() const { return {address}; }
};

// An address whose pointee is not known yet: no load, store or stepping, only at<U>() and cast<U>().
template <>
struct guest_ptr<void> {
    std::uint32_t address = 0;

    constexpr explicit operator bool() const { return address != 0; }
    friend constexpr bool operator==(guest_ptr a, guest_ptr b) { return a.address == b.address; }

    template <typename U>
    constexpr guest_ptr<U> at(std::uint32_t offset) const { return {address + offset}; }
    template <typename U>
    constexpr guest_ptr<U> cast() const { return {address}; }
};

}
