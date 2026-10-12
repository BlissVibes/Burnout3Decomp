#pragma once

// path_extension_after_dot: the characters after the last '.' of a path, used by the audio / RWA
// client when it names assets (the RWA strings live at 0x002AB120-0x002ACD7C, see
// docs/research/decomp-groundwork.md section 2).
//
// Retail address: 0x002934D0 (game code, 22 words, a leaf: no calls, no stores, no float work).
// Evidence: the recompiler's unit_0064.cpp for 0x002934D0-0x00293528 read in full, and the two call
// sites 0x002A5414 (tests result == argument to mean "no extension") and 0x002AAF68 (passes the
// result on as the first argument of an indirect call next to the string at 0x004C1798).
// Status: draft (oracle pending).
//
// Behaviour. The string is scanned to its terminator, then back towards the start one byte at a
// time. The first byte equal to '.' met on the way back ends the search and the pointer just past
// it is the result, so the LAST dot wins and a trailing dot yields a pointer to the terminator (an
// empty extension). The walk back stops as soon as it reaches the first byte of the string WITHOUT
// examining it, so a dot at index 0 is never found: ".hidden" and "." give back the start pointer,
// as does any string without a dot. Only byte 0x00 terminates and only byte 0x2E matches; bytes
// 0x80 and above never compare equal to '.'. Nothing is written and nothing but the string's bytes
// is read.
//
// Deviation, deliberate and visible: for an empty string the retail code walks backwards before
// the start of the buffer until it happens to meet a 0x2E byte. That has no meaning on host memory,
// so this version returns the start pointer for an empty string. Every non-empty input behaves
// exactly as retail.

namespace b3::audio {

// Returns a pointer into `s`: just past the last '.' at index 1 or later, or `s` itself when there
// is none.
const char *path_extension_after_dot(const char *s);

}
