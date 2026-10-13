#pragma once

// decode_id_base40: packs the first twelve characters of an id string (track, vehicle, asset and
// front-end ids) into one 64-bit base-40 key, most significant character first.
//
// Retail address 0x0021AEF0 (game code, ValueDB range). A pure leaf: no calls, no stack frame, no
// stores, no floating point, no globals, no gp access. Evidence: the recompiler's generated C++ for
// unit 0x0046 (labels 0x0021AEF0-0x0021AFE8) read in full and checked against the reader's
// specification; 81 direct call sites, among them 0x0021BA90, 0x0021BC60 and 0x0021BE74 in the
// ValueDB range, a run of 25 calls at 0x002818B0-0x00281C0C that each decode a string literal and
// compare the whole 64-bit result with a saved key, and 0x0030BC9C which stores the result with a
// 64-bit store. The call-site survey is the reader's, not re-verified here. No data word in the ELF
// holds the address. Status: draft (oracle pending). Fixed globals used: none.
//
// Behaviour. Characters are read one byte at a time from position 0 up to and including the first
// NUL or position 11, whichever comes first. Each non-NUL character multiplies the running value by
// 40 (as two 64-bit shifts and an add, so plain modular uint64_t arithmetic) and adds its digit;
// each remaining position up to twelve, the NUL position included, multiplies by 40 and adds
// nothing. The result is therefore
//
//     sum over k = 0..11 of digit(text[k]) * 40^(11-k)      (digit = 0 for k at or past the NUL)
//
// and a buffer of twelve or more bytes without a NUL in its first twelve bytes is legal input: byte
// 12 and beyond are never read.
//
// The digit of a byte, with the byte taken as the signed 8-bit value the EE loads:
//     '_'                        -> 39     (tested first, before the range tests)
//     0x61..0x7F ('a'..'z', ...) -> byte - 84   ('a'..'z' = 13..38; '{' '|' '}' '~' DEL = 39..43)
//     0x41..0x60 ('A'..'Z', ...) -> byte - 52   ('A'..'Z' = 13..38; '[' '\' ']' '^' '`' = 39..42, 44)
//     0x30..0x40 ('0'..'9', ...) -> byte - 45   ('0'..'9' = 3..12; ':' ';' '<' '=' '>' '?' '@' = 13..19)
//     '/'                        -> 2
//     '-'                        -> 1
//     anything else              -> 0      (space, '.', ',', '+', control bytes, and every byte
//                                           0x80..0xFF, which is negative and fails every range test)
//
// Consequences worth knowing. Letters fold case by construction (97-84 = 65-52 = 13), so "TRACK_01"
// and "track_01" give the same key. Space and the pad digit are both 0, so trailing spaces are
// invisible: "ABC" and "ABC         " (space-padded to twelve) decode identically. Out-of-alphabet
// ASCII inside the three ranges yields digits 13..19 (aliasing 'a'..'g') or 39..44; a digit of 40
// or more makes the result not a clean base-40 number, and twelve such bytes can exceed 2^64 and
// wrap, which the uint64_t arithmetic here reproduces. In-alphabet strings never wrap
// (40^12 < 2^64). The empty string decodes to 0.

#include <cstdint>

namespace b3::valuedb {

// Number of character positions an id key holds.
inline constexpr int kIdBase40Length = 12;

// The digit a single byte contributes, per the table above. `byte` is the signed value the EE
// loads; results are 0..44.
std::uint32_t decode_id_base40_digit(std::int8_t byte);

// Decodes `text` (host pointer onto the guest string) to its 64-bit key. Reads bytes text[0] up to
// text[min(index of first NUL, 11)] inclusive and nothing else, so a 12-byte buffer without a
// terminator is fine. `text` must not be null: retail would read guest address 0 unguarded; here it
// is undefined behaviour. On the EE the full 64-bit value is returned in the result register with no
// 32-bit truncation or sign extension.
std::uint64_t decode_id_base40(const char *text);

}
