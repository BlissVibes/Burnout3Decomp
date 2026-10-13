# Decomp log 001 — ten pure leaves, 2026-10-12

First batch of game functions decompiled into `src/hooks/`. Scope was deliberately narrow: small
leaves in `owner=game` ranges of the boundary table (`docs/research/decomp-groundwork.md` section 2)
with no calls (one exception: the ValueDB hash calls the Sony `strlen`), no globals, no DMA, IOP,
threads or VU0, so that each one has a data-free unit test now and a clean oracle run later. The
oracle (the `--call` harness of `docs/re/function-harness-spec.md`, see `docs/re/harness-log-001.md`)
has not yet been run on any of them: **every function below is `status=draft`, evidence has no `C`.**

Workflow per function: a finder scanned the ELF for data-free leaves and listed candidates; a reader
derived the behaviour from the fork's generated C++ and the raw instruction words (two independent
decodes, required to agree); a coder wrote the header, the implementation and a unit test; two
skeptics re-derived the behaviour on their own, built the test, wrote register-level models or
differential fuzzers in the scratchpad and tried to refute the C++; a fixer applied what survived.
Nothing game-derived is in the repo: the one lookup table (CRC-32) is recomputed from its polynomial,
all test vectors are outputs of our own model, and scratch models that read the ELF stayed in the
scratchpad.

Unit tests: every `tests/unit/test_<name>.cpp` builds and passes with
`g++ -std=c++20 -O2 -Wall -Wextra -I<fork>/runtime/include -I src <test> <impl>` in WSL (and through
`tests/unit/run.sh <fork>`, which links every `src/hooks/*/*.cpp`). No warnings. Several were also
run at `-O0`, `-O3` and under `-fsanitize=address,undefined` by the skeptics with the same result.

Directory names. Several stage prompts named directories after boundary-table row labels (slashes,
spaces, parentheses); those are not identifiers or sane paths, so the coders used one slug per
subsystem as section 9 of the groundwork prescribes. The workflow produced three overlapping slugs
(`front_end`, `frontend`, `ring_buffers`); after the run they were merged into `frontend` (the
wide-string compare) and `ring_buffers` (both ring-index helpers), so the set is `valuedb`,
`physics`, `audio`, `frontend`, `load_screen_sprites`, `ring_buffers`, `registry`.

## Summary

| Retail address | Our name | Subsystem (dir, namespace) | Size | Unit test | Skeptic verdicts | Status |
|---|---|---|---|---|---|---|
| `0x0021BFA0` | `valuedb_hash_key` | `valuedb` (`b3::valuedb`) | 0x70 | passes (vectors + 21k model compares) | 1 refuted (out of domain only), 1 not | verified |
| `0x001B9820` | `sap_compare_x` | `physics` (`b3::physics`) | 0x28 | passes (30x30 pool) | not refuted x2 | verified |
| `0x00299550` | `lowest_set_bit_index` | `audio` (`b3::audio`) | 0x38 | passes (6,250 checks) | not refuted x2 (2^32 sweep) | verified |
| `0x0035EFA0` | `compare_wide_strings` | `frontend` (`b3::frontend`) | 0x78 | passes (45 checks) | not refuted x2 | verified |
| `0x003713A0` | `build_texture_uv_rect` | `load_screen_sprites` (`b3::load_screen_sprites`) | 0x7C | passes (234 checks) | not refuted x2 | verified |
| `0x001478C0` | `ring_index_is_at_or_ahead` | `ring_buffers` (`b3::ring_buffers`) | 0x90 | passes (2,374 checks) | not refuted x2 | verified |
| `0x00145E70` | `ring_index_advance` | `ring_buffers` (`b3::ring_buffers`) | 0x74 | passes (57 checks) | not refuted x2 | verified |
| `0x002518E0` | `find_key64_slot` | `registry` (`b3::registry`) | 0x3C | passes (36 checks) | not refuted x2 | verified |
| `0x001C6F30` | `select_gear_under_rev_limit` | `physics` (`b3::physics`) | 0x60 | passes (223 checks) | not refuted x2 | verified |
| `0x002934D0` | `path_extension_after_dot` | `audio` (`b3::audio`) | 0x58 | passes (39 checks) | not refuted x2 | verified |

Sizes are the code words from the entry to the delay slot of the return, excluding alignment
padding. Evidence codes: `S` string or constant anchor, `X` call graph, `R` reB3 inference,
`C` oracle-confirmed (all ten, see below).

Update 2026-10-12: the oracle run of `docs/re/harness-log-002.md` passed all ten (22,464 cases, 0 fail, 0 skip over the twelve menu01 snapshots, fork c31e314), so every row is now `verified` with evidence `C`; that log records two caveats: the divide-by-zero flag path of `select_gear_under_rev_limit` and `build_texture_uv_rect` is not exercised because every snapshot already carries `0x30000` in `fcr31`, and `find_key64_slot` was searched with sign-extended 32-bit keys only.

## Functions

### 0x0021BFA0 `valuedb_hash_key` (ValueDB)

Files: `src/hooks/valuedb/valuedb_hash_key.{hpp,cpp}`, `tests/unit/test_valuedb_hash_key.cpp`.
Evidence `S X R` (reB3 crosswalk row `0x001AF250`; the CRC table at `0x0049B110` is the constant
anchor; callee `strlen` `0x001273E8`). Not a leaf: it calls the Sony `strlen` once.

What it does. Hashes a NUL-terminated key with a table-driven CRC-32 (reflected, polynomial
`0xEDB88320`, initial value -1, **no final invert**) over exactly `strlen(key)` bytes. The twist that
makes it not the textbook CRC: the running state is a *signed* 32-bit value and the per-byte shift
is arithmetic, so whenever the state is negative the top byte is filled with ones before the table
word is XORed in. A `uint32_t` reimplementation with a logical shift differs on every non-empty key.
The 256-entry table in the ELF was checked against the table recomputed from the polynomial (all
256 match), so our code recomputes it at compile time and copies nothing.

Signature: `std::int32_t valuedb_hash_key(const char *key)`; result is the 32-bit state,
sign-extended to 64 bits in the return register. Helpers: `valuedb_hash_key_is_in_domain(key)` (true
when every byte is below 0x80) and `valuedb_hash_table_entry(i)`.

Our core (from the .cpp):
```cpp
const std::int32_t len = static_cast<std::int32_t>(static_cast<std::uint32_t>(std::strlen(key)));
std::int32_t state = -1;
for (std::int32_t i = 0; i < len; ++i) {
    const std::uint8_t byte  = static_cast<std::uint8_t>(key[i]);
    const std::uint8_t index = static_cast<std::uint8_t>(byte ^ (static_cast<std::uint32_t>(state) & 0xFFu));
    state = (state >> 8) ^ static_cast<std::int32_t>(kTable[index]);   // arithmetic shift
}
return state;
```

Edge cases. Empty key returns -1 (all ones). The key byte is fetched sign-extended, so for bytes
0x80..0xFF the retail index is negative and the lookup reads the 256 words of Sony library data
that sit just below the table; that result depends on unrelated ELF data we may not copy, so such
keys are **out of domain**: the C++ uses the unsigned index and `valuedb_hash_key_is_in_domain`
reports them. All real ValueDB keys are ASCII. A length of 0x80000000 or more would be read as
negative and give zero iterations (unreachable on 32 MB, but modelled). Null key is a precondition,
not special-cased. Model-derived vectors (our own model, not yet oracle-checked; the multi-word keys
are made up, not keys the game uses): `""` -> `0xFFFFFFFF`, `"a"` -> `0xE84841BC`,
`"abc"` -> `0x3524413D`, `"Example Key"` -> `0xD69FB90F`, `"Sample/Entry"` -> `0x65B79227`.

Verification. Unit test: fixed vectors; the full 64-bit view of results; a console-width model of the
rule (64-bit values, 32-bit arithmetic where retail is 32-bit) compared on all 127 single ASCII bytes, all 16,129 ASCII pairs, 5,000 random full-ASCII keys of
length 1..300, control characters, a 70,000-byte key; the sign-fill property (a step from a
non-negative state equals the textbook step, from a negative state the textbook step XOR
`0xFF000000`); misaligned key pointers; embedded NUL; whole table recomputed by the 8-step rule plus
CRC linearity; domain boundaries. Skeptic 1 refuted on **out-of-domain input only** (a 3-byte key
with high bytes differs from retail, as documented); its Python model fed by the ELF agreed with our
C++ on all 3,011 ASCII vectors. Skeptic 2 did not refute; it raised the null-key and >= 2^31-length
corners. Fixer: switched the length and counter to the retail signed-32-bit rule, documented the two
corners in the header, folded every skeptic case into the test. In-domain behaviour was already
exact; no in-domain bug existed.

Shim note. Argument `a0` = guest address of the key (translate to host); result -> `v0`
sign-extended from 32 bits. No float registers; retail uses a few integer scratch registers that the
shim need not reproduce. The retail `strlen` reads whole aligned 8/16-byte blocks, so a memory-access
trace would show reads past the NUL within the block; harmless.

### 0x001B9820 `sap_compare_x` (physics, sweep-and-prune)

Files: `src/hooks/physics/sap_compare_x.{hpp,cpp}`, `tests/unit/test_sap_compare_x.cpp`. Evidence
`X R` (reB3 `0x00110AD0`; the only caller found is `qsort` `0x00125338` from the physics frame
`0x001B8F00`, which passes element size 8 and this address as the comparator).

What it does. `qsort` comparator over 8-byte records whose first word is a float key: returns -1 when
`key(a) < key(b)` under EE single-precision ordered-less-than, otherwise +1. **No zero result**: ties
return +1 in either order, so the comparator is not antisymmetric and tied records keep the order
`qsort` happens to give them. The compare is the EE's, not IEEE: no NaN or infinity (exponent-255
patterns are huge finite values), denormals flush to signed zero before comparing, -0 == +0.

Signature: `std::int32_t sap_compare_x(std::uint32_t key_a_bits, std::uint32_t key_b_bits)` plus an
overload on `const SapRecord *` (key at +0, unread payload at +4). Our one line:
```cpp
return ps2::fp::key(key_a_bits) < ps2::fp::key(key_b_bits) ? -1 : 1;
```

Verification. Unit test: a 30x30 pool of special patterns against an independent key model, tie and
sign-of-zero cases, the 64-bit view of -1. Skeptic 1 re-derived from the raw words and the generated
C++, confirmed the `qsort` call site and the fork's `set32` sign extension; no issues. Skeptic 2
swept both signs x all 256 exponents x 9 mantissas (21.2 million ordered pairs) and 2.5 million
random pairs against a numeric oracle built with `ldexp`, ran 50 `qsort` rounds (46,249 comparator
calls, zero 0-returns), and showed host-float `<` disagrees with the EE on 5 of 5 probe pairs
(NaN patterns, denormals); nothing refuted. Caveat both noted: equivalence is to the fork's
`ps2::fp` model of the ordered compare; hardware denormal handling is for the oracle to confirm.

Shim note. `a0`, `a1` = guest addresses of records A and B; load the first 32-bit word of each (or
translate both to host `SapRecord *`) and pass the bit patterns. Result -> `v0` sign-extended
(`-1` is all ones). Retail also leaves the FPU condition bit set by the compare and uses two float
scratch registers; not reproduced.

### 0x00299550 `lowest_set_bit_index` (audio / RWA client range)

Files: `src/hooks/audio/lowest_set_bit_index.{hpp,cpp}`, `tests/unit/test_lowest_set_bit_index.cpp`.
Evidence `X` (126 direct call sites in `0x00297C7C`-`0x002B38D4`, in nine runs of 14 calls; the
constants passed are mostly 64, also 40, 48, 1, or a sign-extended byte).

What it does. Count-trailing-zeros over 32 positions with a byte-sized counter: returns the index of
the lowest set bit of the 32-bit argument, or **32 when the argument is zero**. Pure, no memory.
Callers mask the result to a byte and typically compute `(1 << result) & 0xFFFF` afterwards; the
shape suggests a compiler- or macro-generated helper (inference, unverified).

Signature: `std::uint8_t lowest_set_bit_index(std::uint32_t mask)`.

Edge cases. Zero -> 32. Sign- versus zero-extension of the argument is irrelevant (the upper half of
the register is non-zero only when bit 31 is set, which already answers 31); the only divergence is a
malformed 64-bit register with a zero low word and a non-zero high word, which retail answers 31 and
no 32-bit caller can produce. Result is 0..32, never negative, so the extension of the return is
moot. Caller note (not this function): the callers' `1 << result` for result 32 relies on the
MIPS 5-bit shift count (gives 1); transcribing a caller to C++ must mask the count.

Verification. Unit test: 6,250 checks. Both skeptics wrote 64-bit register-level models and swept
**all 2^32 inputs**, each fed sign- and zero-extended: 0 mismatches (about 13-16 s). One listed every
caller's argument source from the generated units (all canonical 32-bit values) and confirmed no
data word in the ELF holds this address (no function-pointer calls). Nothing refuted, nothing fixed;
one documentation note that constant 1 is also passed and the "sign-extended byte" comes from
arithmetic shifts by 24/16/8.

Shim note. `a0` low 32 bits -> `mask`; result -> `v0` (0..32, upper bits zero either way). Retail
leaves its loop temporaries in three integer scratch registers and preserves the argument register;
not reproduced. No stack, no FPU.

### 0x0035EFA0 `compare_wide_strings` (front end / online UI text)

Files: `src/hooks/frontend/compare_wide_strings.{hpp,cpp}`, `tests/unit/test_compare_wide_strings.cpp`.
Evidence `X` (five callers: `0x00366E00`, `0x003A5234`, `0x003A53EC`, `0x003A56BC`, `0x003A572C`;
no data-word references).

What it does. Three-way compare of two 16-bit-unit, 16-bit-zero-terminated strings in plain code-unit
order (unsigned 16-bit; not code points, not case-folded, not locale-aware). Returns exactly -1, 0
or +1, never the difference. A strict prefix sorts first; both empty -> 0. Reads `s1[k]` then `s2[k]`
for `k` up to the first mismatch or terminator and nothing beyond; when `s1` ends, `s2[k]` is read
exactly once.

Signature: `std::int32_t compare_wide_strings(const std::uint16_t *s1, const std::uint16_t *s2)`.

Edge cases. Units are zero-extended, so 0x8000..0xFFFF sort after 0x0001..0x7FFF (an `int16_t`
version would be wrong). The terminator is a 16-bit zero unit; 0x0100 and 0x0001 do not terminate.
Surrogates are not decoded (0xD800..0xDFFF sort below 0xE000..0xFFFF). -1 is sign-extended to all
64 bits in the result register, 0 and +1 are plain. Null or odd pointers: retail dereferences
without a guard (an odd pointer would be an address error on hardware; the fork does not check).

Verification. Unit test: 45 checks pinning zero-extension (0x7FFF vs 0x8000), the 16-bit terminator,
the read bound and the 64-bit view. Skeptic 1 re-derived from the raw words and generated C++,
confirmed the five callers by scanning for the call word; no issues. Skeptic 2 added 70,000-unit
strings, surrogate and signed-char traps, guard-page (PROT_NONE) read-bound tests, misaligned
copies, and a 200,000-pair differential run against a register-level model on the full 64-bit
result (0 mismatches) at `-O2`, `-O0` and `-O3 -fsanitize=undefined`. Nothing refuted or fixed.

Shim note. `a0`, `a1` = guest addresses of `s1`, `s2` (2-byte aligned); translate to host; result
-> `v0` sign-extended from `int32_t`. Retail advances both argument registers in place and uses
three integer scratch registers; not reproduced. The translation only needs to cover the extent
actually read.

### 0x003713A0 `build_texture_uv_rect` (front end / load-screen sprites)

Files: `src/hooks/load_screen_sprites/build_texture_uv_rect.{hpp,cpp}`,
`tests/unit/test_build_texture_uv_rect.cpp`. Evidence `X` (49 call sites: 11 in the front-end code
`0x00351840`..`0x003B18A8`, 38 in the Metrowerks static-initialiser block `0x004D7EF8`-`0x004DD254`
where constant UV rects are baked at startup; one site passes a 64x32 texture size).

What it does. Converts a pixel rectangle `(x, y, w, h)` on a texture of size `(tex_w, tex_h)` into
four UV floats `u_left = x/tex_w`, `v_top = y/tex_h`, `u_right = (x+w)/tex_w`,
`v_bottom = (y+h)/tex_h`, written interleaved as `u0, v0, u1, v1`. Flag bit 0 swaps the two U values
(horizontal flip), bit 1 swaps the two V values (vertical flip), after the divides; other bits are
ignored. The adds and divides are EE single precision through the fork's `fp::add` / `fp::div`
(truncation, denormal flush, no NaN or infinity; a zero or denormal divisor yields the signed
maximal pattern and raises the sticky divide-by-zero flags). Retail also writes the same four
floats to a dead 16-byte temporary in its own stack frame, which we omit; the harness spec ignores
writes below the stack pointer.

Signature: `bool build_texture_uv_rect(std::uint32_t *out_uv, std::uint32_t flip_flags, std::uint32_t x,
std::uint32_t y, std::uint32_t w, std::uint32_t h, std::uint32_t tex_w, std::uint32_t tex_h)`; all
floats as raw bit patterns; returns true when any divisor flushed to zero (the caller ORs
`kDivideByZeroFlags = 0x30000` into `fcr31`).

Edge cases. Divide order is v_bottom, u_left, v_top, u_right (only matters for which divide sets
the sticky flag first). `w = 0` gives `u1 == u0` exactly. `-0` inputs yield `-0` UVs. The flip
exchanges within the U pair or the V pair only; a reimplementation must not swap x and x+w before
dividing (same values anyway) and must keep the interleaved layout. No return register: retail does
not write `v0`.

Verification. Unit test: 234 checks. Skeptic 1 built a literal fp-model re-derivation of the
instruction sequence and ran a 5,000,000-case differential test (signed zeros, denormals,
exponent-255 patterns, overflow/underflow quotients, zero divisors, flag words with high bits):
0 mismatches; also ran the unit test under ASan/UBSan. Skeptic 2 ran 41.3 million cases
(exhaustive triples over a 48-pattern pool plus random), added 150 unit checks (flip applied to
divide-by-zero results, negative quotients truncating toward zero, exponent-255 divisors, addition
overflow and far-apart operands, cancellation to signed zero, denormal divisor signs), and noted a
spec inconsistency: retail does scratch `v1` (the flag tests), so "v0/v1 untouched" is true only
of `v0`. Nothing refuted or fixed.

Shim note. `a0` = guest address of the 16-byte output (4-byte aligned); `a1` low word = flags;
`f12..f17` = x, y, w, h, tex_w, tex_h as bit patterns. On return do not write `v0`; when the bool is
true OR `0x30000` into `fcr31` (never assign). Retail leaves its flag test in `v1` and the
intermediate sums and quotients in six float scratch registers; a full-state oracle will see those
and the dead stack copy.

### 0x001478C0 `ring_index_is_at_or_ahead` (front-end core / loader ring buffers)

Files: `src/hooks/ring_buffers/ring_index_is_at_or_ahead.{hpp,cpp}`,
`tests/unit/test_ring_index_is_at_or_ahead.cpp`. Evidence `X` (eight callers in `0x0014BCE4`..
`0x0014D738`; seven load the descriptor from object+0x1D4 of a saved pointer).

What it does. Given a ring descriptor (whose second word points at a header holding a 16-bit slot
count at +12 and a wrap flag byte at +14) and two 16-bit indices, answers whether `idx_a` is at or
ahead of `idx_b`. Non-wrapping ring: `d >= 0` where `d = (a & 0xFFFF) - (b & 0xFFFF)` as a signed
value. Wrapping ring: with `half = count / 2`, returns 1 when `0 <= d < half` or `d < -half` (a
wrapped past b), else 0. Returns 0 or 1.

Signature: `std::int32_t ring_index_is_at_or_ahead(const RingHeaderView &header, std::uint32_t idx_a,
std::uint32_t idx_b)`, plus an overload on `(std::uint16_t slot_count, std::uint8_t wraps, idx_a,
idx_b)`. The C++ takes the header, not the descriptor; the shim does the one indirection.

Edge cases. Indices are masked, not sign-extended; `d` is in -65535..65535. Count is unsigned 16-bit
and halved by truncation (0..32767). Any non-zero wrap byte (0x80..0xFF included) selects wrap mode.
`half == 0` (count 0 or 1) in wrap mode: equal indices return **0**, any negative `d` returns 1.
Strict at both boundaries (`d == half` -> 0, `d == -half` -> 0, `d == -half-1` -> 1). Non-wrap mode
ignores the count but retail still reads it (the header must be readable).

Verification. Unit test: 2,374 checks. Skeptic 1 wrote a literal 64-bit register model of the
routine and swept 93.5 million cases (all 65,536 `idx_a` x 11 `idx_b` x 17 counts x 6 wrap bytes with
garbage in the upper bits, plus 20 million random): 0 mismatches. Skeptic 2: 9,096 checks and about
34 million model comparisons including every count at the boundaries, every wrap byte, raw header
layouts with filler bytes, and six deliberately wrong implementations each caught (8 to 2.75 million
mismatches). Nothing refuted or fixed. Both flagged the integration point: the hook must load the
header pointer from descriptor+4 itself.

Shim note. `a0` = guest descriptor; read the 32-bit word at `a0+4`, translate it, view as
`RingHeaderView` (slot count u16 at +12, wraps u8 at +14; `static_assert` size 16). `a1`, `a2` low
words -> `idx_a`, `idx_b` (the function masks to 16 bits). Result 0 or 1 -> `v0` (extension moot).
Retail uses four integer scratch registers, the first argument register among them; not reproduced.

### 0x00145E70 `ring_index_advance` (front-end core / loader ring buffers)

Files: `src/hooks/ring_buffers/ring_index_advance.{hpp,cpp}`, `tests/unit/test_ring_index_advance.cpp`.
Evidence `X` (callers at `0x0014AA14`, `0x0014B7FC`, `0x0014B814`, `0x0014BC08` pass 16-bit indices
and sign-extended 16-bit deltas and consume the result as 16 bits).

What it does. Steps a 16-bit ring index by a signed 16-bit delta. Delta 0 returns the index
untouched without reading memory. Otherwise, with the header's capacity (u16 at +12) and wrap flag
(u8 at +14): wrapping ring -> `(index + delta + capacity) % capacity` with a truncating remainder
(so one wrap below zero is handled; a deeper negative gives a negative remainder masked to
`0xFFxx`); clamping ring -> `clamp(index + delta, 0, capacity - 1)` on signed 32-bit values
(capacity 0 gives -1 -> `0xFFFF`). The result is masked to 16 bits and zero-extended. The clamp
path uses the EE's packed min/max; the wrap path uses the integer divider.

Signature: `std::uint16_t ring_index_advance(const RingDesc *desc, std::uint16_t index, std::int16_t delta)`;
`RingDesc` holds an unread first word and the header pointer.

Edge cases. Only the low 16 bits of the delta register are used (0x10000 acts as 0, 0x8000 as
-32768); in the non-zero path only the low 16 bits of the index. Wrap with capacity 0 is a divide
by zero: hardware result unpredictable, the fork's `mmi::div` convention (remainder = dividend) is
what we match, so it is not an oracle case. The 16-bit sign extension before the final mask in
retail is dead code. Loop-free; no FPU.

Verification. Unit test: 57 checks. Skeptic 1 built a register-level model on the fork's own
`cpu.hpp` / `mmi.hpp` primitives and compared 588 million cases (exhaustive 16-bit delta and index
sweeps over 62 capacities and 6 flag bytes, plus 3 million random with garbage upper bits):
0 mismatches. Skeptic 2: 61 million cases against a literal transcription of the generated code with
random 128-bit garbage in every register, plus 33 extra unit checks (most negative and positive
steps, capacity boundaries 0x7FFF/0x8000/0xFFFF, negative remainders at small capacities, the
register contract) and an ASan/UBSan run. Nothing refuted or fixed. One note: the function proper is
29 words; 32 with alignment padding.

Shim note. `a0` = guest descriptor (translate; its header pointer at +4 is a guest address to
translate too); `a1` -> `index` low 16 bits; `a2` -> `static_cast<int16_t>(a2 & 0xFFFF)`. When that
delta is 0 the shim must copy the **entire 64-bit `a1` register** into `v0` and touch no memory;
otherwise `v0` = the 16-bit result zero-extended (no `set32`). `tests/unit/test_ring_index_advance.cpp`
has a `call_like_shim()` that states this contract.

### 0x002518E0 `find_key64_slot` (loaded-asset / id registry)

Files: `src/hooks/registry/find_key64_slot.{hpp,cpp}`, `tests/unit/test_find_key64_slot.cpp`.
Evidence `X` (callers per the finder: `0x002BCAE8`, `0x0030BD98`-`0x0030C87C`, `0x00332F94`,
`0x0037BFB4`, `0x003BAB90`-`0x003BAE80`, `0x003C29D0`, `0x003C34C0`; not re-verified).

What it does. Linear search of a 64-bit key array for an exact 64-bit match; returns the first
matching index or -1. The object layout, inferred from this function and its three siblings: a
signed 32-bit count at +4, two 32-bit per-slot arrays at +8 and +520, and the 64-bit key array at
+1032; 128 slots by spacing (size 2056). The capacity is layout arithmetic, not a verified bound:
retail has no bounds check against it.

Signature: `std::int32_t find_key64_slot(const KeyTable &table, std::uint64_t key)`; `KeyTable`
has `static_assert`s for the offsets above.

Siblings read but not implemented: `0x002519B0` and `0x00251EF0` (byte-identical) run the same
search and return 1 on a miss, else whether the 32-bit word at +8+4*i is non-zero; `0x00251920`
runs the search twice and uses the array at +520. Noted here so the layout and the asymmetric
miss-returns-1 convention are on record.

Edge cases. Count is signed: zero or negative means no iterations and -1. The compare is all 64
bits of the key register (a zero-extended and a sign-extended 32-bit id are different keys). First
match wins. Index arithmetic is 32-bit. -1 is sign-extended to all ones in the result register. The
64-bit loads need an 8-aligned table. A count above 128 reads past the object in retail and is
undefined behaviour in our C++ (documented; not exercised).

Verification. Unit test: 36 checks. Skeptic 1 re-derived from the raw words and generated C++ and
looked for an inserter or bounds check (none found in the unit; the 128 remains inferred). Skeptic 2
added 196 checks (huge counts stop at the first match, every negative count magnitude, bit flips in
all 64 key positions, sign-mangling traps, result widening for every index, table unchanged, and an
informational count-overrun probe not for the repo) at `-O0`/`-O2`/`-O3` and under sanitizers.
Nothing refuted or fixed. Both noted no shim exists yet and it must sign-extend the result.

Shim note. `a0` = guest address of the object (8-aligned; translate and pass as `KeyTable &`);
`a1` = the **full 64-bit** key register; result -> `v0` sign-extended from `int32_t`. Retail uses
two integer scratch registers; no stack, no FPU.

### 0x001C6F30 `select_gear_under_rev_limit` (vehicle physics / drivetrain)

Files: `src/hooks/physics/select_gear_under_rev_limit.{hpp,cpp}`,
`tests/unit/test_select_gear_under_rev_limit.cpp`. Evidence `S X` (the `gp`-relative constant at
`0x004E0D80` is exactly `float(pi/30)`, rpm to rad/s; three callers `0x001BEEA4`, `0x001C0A68`,
`0x001D1770`, all passing vehicle+0x1448 and storing the result to the block's word 32, the
"current gear" field by usage).

What it does. Over a transmission block: computes `r = dividend / divisor` from the two float
arguments and `limit = (pi/30) * block[10]` (a rev limit in rpm), then starting at entry 1 steps
through the ratio table at words 1..count-1, computing `value = block[8] * (r * ratio[i])` and
stopping at the first entry whose `value <= limit` under the EE compare. Returns that 1-based index
(or `count` when none qualifies) and **always** stores the last computed `value` to word 21 of the
block (`+84`). Plausible reading (unverified): wheel angular speed times gear ratio times final
drive, compared with the rev limit, selecting the lowest gear that stays under it.

Signature: `std::int32_t select_gear_under_rev_limit(std::uint32_t *block, std::uint32_t divisor_bits,
std::uint32_t dividend_bits, std::uint32_t inherited_value_bits, bool *divide_by_zero = nullptr)`.
`block` is a word view over guest RAM so that indices past the table alias the following scalars
exactly as retail (the table can hold at most entries 0..6 before index 7 aliases word 8).

Edge cases. `count <= 1`: the loop never runs and retail stores the caller's **stale FPU register**
to +84; the C++ takes that as `inherited_value_bits`, and oracle tests must seed it. No entry
qualifies: returns `count`, +84 holds the value for entry `count-1`. Count is signed. Grouping is
`mult * (r * ratio)`, two truncating multiplies; do not fold. Divide by zero yields the signed
maximal pattern and the sticky `fcr31` flags `0x30000`; the compare is the sign-magnitude key
compare (`-0 <= +0`). `block[8]` and the table entry are re-read every iteration, so aliasing at
counts >= 8 falls out automatically.

Verification. Unit test: 223 checks. Skeptic 1: 3,000,000-case differential fuzz against a literal
transcription of the generated code using the same `ps2::fp` primitives (special patterns, counts
-1/0/1/INT32_MIN..4, aliasing counts to 62): 0 mismatches in return, stored word, flag and every
other word. Skeptic 2: 2,000,000-case fuzz with a reference emulation (word memory, 64-bit
registers, `fcr31`, explicit entry FPU state), 15 targeted cases (saturation without flag,
underflow to signed zeros, truncating quotients `2/3` and `1/10`, saturated ratios, negative limits,
`pi/30` bit-exact equality, count 47 reaching the last block word, multiply carry), ASan/UBSan clean.
Nothing refuted or fixed. Integration notes: OR the flag into `fcr31`, never assign; the condition
bit and scratch registers are not reproduced.

Shim note. `a0` = guest transmission block (vehicle+0x1448; pass a host `std::uint32_t *` over guest
RAM, 4-byte aligned); `f12` -> `divisor_bits`, `f13` -> `dividend_bits`; `f3` on entry ->
`inherited_value_bits`. Return -> `v0` via `set32` (always >= 1). If `*divide_by_zero`, OR
`0x30000` into `fcr31`. Retail also leaves the count, the loop test and its float intermediates (the
quotient, the limit, the last loads and products) in scratch registers, and the FPU condition bit
holds the last loop test when the loop ran; a full-state oracle will see those.

### 0x002934D0 `path_extension_after_dot` (audio / RWA client asset naming)

Files: `src/hooks/audio/path_extension_after_dot.{hpp,cpp}`, `tests/unit/test_path_extension_after_dot.cpp`.
Evidence `X` (exactly two callers, `0x002A5414` and `0x002AAF68`; the first tests `result == s` to
mean "no extension", the second passes the result to an indirect call together with a string from
`0x004C1798`).

What it does. Returns a pointer to the text after the **last** `.` in a NUL-terminated path, or the
start pointer when there is none. Scans forward to the terminator, then backward testing each byte
against `.`; the byte at index 0 is never examined, so `".hidden"` and `"."` return the start, and a
trailing dot returns a pointer to the terminator (empty extension). Only byte 0x2E matches, only
0x00 terminates. Read-only.

Signature: `const char *path_extension_after_dot(const char *s)`.

Edge cases. **Empty string**: retail walks backward before the buffer until it meets a 0x2E byte;
on host that is an out-of-bounds read with no defined answer, so the C++ returns `s`, a documented
deviation; keep empty strings out of oracle vectors. Null pointer: retail reads guest address 0;
host would crash; precondition only. Not-found returns the caller's pointer register verbatim,
found returns a 32-bit pointer sign-extended; identical for conventional EE addresses.

Verification. Unit test: 39 checks. Skeptic 1 built a register-level model of the routine and
fuzzed 1,916,687 random non-empty strings (dots, high bytes, arbitrary non-zero bytes) with dots
planted before and after the buffer: 0 mismatches; confirmed the two call words in the ELF.
Skeptic 2 added high-byte-only strings, result invariants, misaligned starts, bytes-before-start
independence, 100,000-byte strings, and a 200,000-string differential run, built with
`-funsigned-char`, `-fsigned-char` and ASan/UBSan: 200,138 checks pass in each. Nothing refuted or
fixed.

Shim note. `a0` = guest string address (translate); translate the host result back to a guest
address and write it to `v0` sign-extended from 32 bits (or copy `a0` through when the result equals
the input). Retail uses two integer scratch registers; no stack, no FPU, no writes.

## Skipped, deferred and finder misses

No candidate in the batch was skipped after selection: all ten built and passed. The following were
seen and not taken.

Finder misses (searched for, not found):
- **Base-40 track/vehicle id codec.** No game function multiplies or divides by 40 by the usual
  reciprocal idiom: the one `0xCCCCCCCD` immediate is inside RenderWare (`0x0020F8A0`, in the
  untranslated sky2 gap), `0x66666667` appears nowhere, no base-40 alphabet string of 36..48
  characters exists, and every game immediate 40 is a call argument, a 40-byte stride or a switch
  constant. Either the ids are compared as whole words on PS2 or the codec is inlined with
  shift-add multiplies; unverified. Groundwork task 6 should be re-planned around this.
- **m/s to mph helper.** No standalone function: `2.23693629` is loaded inline at 49 sites in 40
  functions and `0.44704` at 11 sites in 7; `2.2374146` lives at `0x004E0D78`/`0x004E0DE8` and is
  used only inside `0x001C0F00` and the force pass `0x001C1B20`. The conversion is an inlined
  expression.

Runner-ups not taken this batch (candidates for the next):
- `0x00389580` (64 bytes, 65 callers from vehicle audio): returns the int at `(*(a0+8))+16`
  converted as unsigned to float; a tiny, widely used getter.
- `0x002B6EC0` (40 bytes, 2 callers): pool byte count with alignment rounding, exercises the
  integer multiplier.
- `0x00387360` (96 bytes, 9 callers): 0..1 float to a byte via `*255` with the `>= 2^31`
  saturation idiom, stored at `(*(a0+4))+132`.
- `0x003E8C20` (420 bytes, 1 caller): id-to-constant map in a stride-20 numbering; semantics
  unverified and over the size guideline.
- `0x0022D760` (124 bytes, 1 caller): 16-bit sequence-number "is newer" test with one-third /
  two-thirds windows; in the Gt network range (`0x00217750`-`0x00237270`, medium confidence), so
  game-adjacent rather than game.
- Siblings of `find_key64_slot`: `0x00251920`, `0x002519B0`, `0x00251EF0` (described above).

Excluded on provenance or harness fit:
- `0x00410D20` (signed decimal string parser, 96 callers): callers are mostly in the DirtySock
  string window `0x00405528`-`0x00417F28`; treated as EA DirtySock.
- `0x001AB010` (44 bytes, 15 callers) and `0x00251740` (184 bytes, 20 callers): COP2 / VU0-macro
  leaves, not oracle-friendly until `--compare-vu` exists.
- The physics window `0x001AC000`-`0x001D0000` and AI window `0x00268000`-`0x00275000` have almost
  no data-free leaves: their small functions are jump-table switches or global-table lookups. The
  ValueDB window has only 8-20-byte stubs besides the hash. The scoring window leaves read global
  tables or relocate pointers in place.

Finder method, for the record: every call target in `0x00100000`-`0x0046A000` taken as a function
start, extent found by following forward branch targets to the first return, kept if no call,
syscall or out-of-range jump; 1,109 leaves under 200 bytes in game ranges, 317 with no global or
absolute address; call sites checked to lie in game ranges.

## Symbol rows

Section 9 format of `docs/research/decomp-groundwork.md` (tab-separated:
`address name kind size type owner evidence status note`). Two rows replace existing ones in
`game/symbols/SLUS_210.50.tsv`: `0x0021BFA0` (type and status change; the existing size 0x70 is
kept) and `0x001B9820` (size 0x24 -> 0x28 to include the return's delay slot, status draft).

```
0x00145E70	ring_index_advance	func	0x74	u16 ring_index_advance(const RingDesc *desc, u16 index, s16 delta)	game	X	draft	steps a 16-bit ring index; wrap = (i+d+cap)%cap truncating, clamp = [0,cap-1]; delta 0 returns a1 unchanged; docs/re/decomp-log-001.md
0x001478C0	ring_index_is_at_or_ahead	func	0x90	s32 ring_index_is_at_or_ahead(const RingDesc *desc, u32 idx_a, u32 idx_b)	game	X	draft	header via desc+4 (count u16 +12, wrap u8 +14); non-wrap d>=0, wrap d in [0,half) or d<-half; docs/re/decomp-log-001.md
0x001B9820	sap_compare_x	func	0x28	s32 sap_compare_x(const void *a, const void *b)	game	XR	draft	reB3 0x00110AD0; qsort comparator on the first float of 8-byte records, EE ordered-less-than, -1 or +1, never 0; docs/re/decomp-log-001.md
0x001C6F30	select_gear_under_rev_limit	func	0x60	s32 select_gear_under_rev_limit(TransmissionBlock *t, f32 divisor, f32 dividend)	game	SX	draft	gp constant pi/30 at 0x004E0D80; first 1-based ratio entry with mult*(r*ratio) <= limit, stores value at +84 (stale f3 when count<=1); callers 0x001BEEA4 0x001C0A68 0x001D1770; docs/re/decomp-log-001.md
0x0021BFA0	valuedb_hash_key	func	0x70	s32 valuedb_hash_key(const char *key)	game	SXR	draft	reB3 0x001AF250; reflected CRC-32 table from -1, no final invert, signed state with arithmetic shift; bytes >= 0x80 index below the table (out of domain); docs/re/decomp-log-001.md
0x002518E0	find_key64_slot	func	0x3C	s32 find_key64_slot(const KeyTable *table, u64 key)	game	X	draft	linear search of u64 keys at +1032 over signed count at +4, first match or -1; siblings 0x00251920 0x002519B0 0x00251EF0 return 1 on miss; docs/re/decomp-log-001.md
0x002934D0	path_extension_after_dot	func	0x58	const char *path_extension_after_dot(const char *s)	game	X	draft	pointer after the last '.', start pointer if none; index 0 never tested; callers 0x002A5414 0x002AAF68; docs/re/decomp-log-001.md
0x00299550	lowest_set_bit_index	func	0x38	u8 lowest_set_bit_index(u32 mask)	game	X	draft	ctz over 32 positions, 32 for zero; 126 callers in 0x00297C7C-0x002B38D4; docs/re/decomp-log-001.md
0x0035EFA0	compare_wide_strings	func	0x78	s32 compare_wide_strings(const u16 *s1, const u16 *s2)	game	X	draft	UTF-16 code-unit order, unsigned, returns -1/0/+1; callers 0x00366E00 0x003A5234 0x003A53EC 0x003A56BC 0x003A572C; docs/re/decomp-log-001.md
0x003713A0	build_texture_uv_rect	func	0x7C	void build_texture_uv_rect(f32 *out_uv, u32 flip_flags, f32 x, f32 y, f32 w, f32 h, f32 tex_w, f32 tex_h)	game	X	draft	UV rect u0,v0,u1,v1 by EE divides; bit0 swaps U, bit1 swaps V; dead stack copy; 49 callers incl. 38 static initialisers; docs/re/decomp-log-001.md
```

## Hook registrations

For `src/hooks/hooks_table.cpp`, in its `{address, name, &b3::hook_<name>}` form. The
`hook_<name>(ps2::Cpu &)` shims that translate registers as described in each "Shim note" above do
not exist yet (only `pack_fixed16` has one); these rows are what to register once they do. The
hooks file `game/hooks/burnout3.hooks` is exported from rows with `status=verified`, so none of
these should be exported until the oracle has confirmed them.

```
0x00145E70 ring_index_advance
0x001478C0 ring_index_is_at_or_ahead
0x001B9820 sap_compare_x
0x001C6F30 select_gear_under_rev_limit
0x0021BFA0 valuedb_hash_key
0x002518E0 find_key64_slot
0x002934D0 path_extension_after_dot
0x00299550 lowest_set_bit_index
0x0035EFA0 compare_wide_strings
0x003713A0 build_texture_uv_rect
```

## Next

1. Run each function through the `--call` harness against the snapshot corpus
   (`docs/re/function-harness-spec.md`) with the shim contracts above; seed `f3` for
   `select_gear_under_rev_limit`, keep empty strings and high-byte keys out of the vectors, and
   compare `fcr31` with OR semantics. On a pass, add `C` to the evidence and move the row to
   `verified`.
2. Write the ten `hook_*` shims and register them.
3. (done after the run) the `front_end`/`frontend`/`ring_buffers` directories were merged into `frontend` and `ring_buffers`.
4. Next batch from the runner-ups above; re-plan the base-40 codec and the m/s-to-mph helper, which
   do not exist as functions.
