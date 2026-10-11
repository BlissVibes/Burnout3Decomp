# Gap 001: functions reached only through the static-constructor table

Date: 2026-10-10. Found in headless run 001 (`docs/re/boot-log-001.md`). Analysis: Fable. Confirmation against the ELF and the hand seed: Opus 1.

## Symptom

Every exploring round halted after 2 frames with `[ee] halted: missing code at 0x004Dxxxx (ra 0x00100844)`, each round adding exactly one new entry point at a rising address. The 8-round cap in `port.sh` stopped at the eighth of 158.

## Cause

`0x00100810` is the C runtime's constructor runner. Its loop, at `0x00100838..0x00100850`, loads the next function pointer from the slot register `$16` points at, calls it through a register (`jalr` at `0x0010083C`), then advances `$16` by 4 and repeats until `$16` reaches the end address held in `$17`. The instruction after the call's delay slot is `0x00100844`, which is the `ra` of every halt.

The table lives at `0x004DD820..0x004DDA94` (158 words, zero word before and after; `r16 = 0x004DD83C`, `r17 = 0x004DDA98` in the halt dump). Its targets are the compiler-generated static initializers at `0x004D3E00..0x004DD7E0`. Nothing else references them.

`eerecomp` (`recomp/analysis.cpp`, `analyse()`) does scan every data word as a candidate entry, but then drops any candidate outside `[text_begin, text_end)`, where `text_end` is the highest address of code the explorer has already found. This ELF has one RWX segment and no `.text` section, so `image.text()` contributes nothing, and the initializers lie above every function reachable from the entry point and from address constants. All 158 were discarded by the range check before `tentative()` ever saw them.

## Fix (generic, in the fork, `recomp/analysis.cpp`)

Recognise pointer tables: four or more consecutive non-code words that each point at plausible code (`valid_target` and `tentative()` pass, not previously rejected). Accept their targets as functions even beyond the current code range, because functions reached only through a table routinely sit past everything else. Memoise `tentative()` per target so the scan stays cheap in a single-segment image where every word that points anywhere in the segment passes `valid_target`.

Add after `computed_branches()`:

```cpp
// Tables of function pointers in data: static constructor lists, virtual tables, callback arrays.
// Four or more consecutive non-code words that each point at plausible code. Their targets are
// accepted even outside the code range found so far, because functions reached only through a
// table often lie past everything the explorer can reach from the entry point.
std::vector<std::uint32_t> pointer_tables(const Image &image, const Program &p, Explorer &explorer) {
    std::vector<std::uint32_t> found;
    std::map<std::uint32_t, bool> verdict;
    const auto plausible = [&](std::uint32_t target) {
        if (!explorer.valid_target(target) || p.rejected.count(target)) return false;
        if (p.is_code(target)) return true;
        const auto it = verdict.find(target);
        if (it != verdict.end()) return it->second;
        return verdict[target] = explorer.tentative(target);
    };
    for (const auto &segment : image.segments()) {
        std::vector<std::uint32_t> run;
        const auto flush = [&] {
            if (run.size() >= 4) found.insert(found.end(), run.begin(), run.end());
            run.clear();
        };
        for (std::size_t offset = 0; offset + 4 <= segment.bytes.size(); offset += 4) {
            const std::uint32_t at = segment.address + static_cast<std::uint32_t>(offset);
            if (p.is_code(at)) {
                flush();
                continue;
            }
            const std::uint32_t target = *image.word(at);
            if (plausible(target)) run.push_back(target);
            else flush();
        }
        flush();
    }
    return found;
}
```

(`#include <map>` at the top of the file.)

In `analyse()`, inside the `for (int round ...)` loop, after the existing candidate loop and before `explorer.drain();`:

```cpp
        for (std::uint32_t target : pointer_tables(image, p, explorer)) {
            if (p.entries.count(target) || p.rejected.count(target)) continue;
            p.entries.insert(target);
            if (!p.is_code(target)) explorer.strong(target, true);
            ++added;
        }
```

## Verification

1. Rebuild the tool: `ninja -C ~/b3/ps2-web-recomp/build/tools eerecomp`.
2. Run it with an **empty** entries file into a scratch directory and confirm all 158 table targets are translated without any hand seed:
   ```bash
   cd ~/b3/ps2-web-recomp
   : > /tmp/empty.txt
   time build/tools/eerecomp games/burnout3/disc/SLUS_210.50 --entries /tmp/empty.txt --out /tmp/gen-check | tail -1
   missing=0; for a in $(cat ~/b3/out/ctor-table.txt); do grep -qil "0x0*${a}\b" /tmp/gen-check/*.cpp || { missing=$((missing+1)); echo "not translated: $a"; }; done; echo "missing $missing of 158"
   ```
   Expected: `missing 0 of 158`; the `functions` count on the summary line rises by about 158 over run 001's; eerecomp still finishes in seconds.
3. Run 001's halt must not recur: `tools/port.sh --frames 1800` with `games/burnout3/entries.txt` emptied (`: > entries.txt` after saving a copy) should pass the two-frame mark in round 1.
4. Risk to watch: a table of pointers to data that happens to decode as code could add false functions. `rejected seeds` and the unit count on the summary line should not jump by more than the table's size; if they do, raise the run length from 4 to 8.

## Result (Opus 1, 2026-10-10)

Patch applied to `recomp/analysis.cpp` exactly as written above; `ninja -C build/tools eerecomp` built it without warnings.

eerecomp summary with an empty entries file:

| | instructions | functions | entries | rejected seeds | units | time |
|---|---|---|---|---|---|---|
| before | 845105 (3301 KiB) | 5893 | 66174 | 0 | 219 | 1.3 s |
| after | 858300 (3352 KiB) | 6151 | 66903 | 0 | 225 | 12.8 s |

- All 158 table targets are translated with no seed: `missing 0 of 158`. The grep in Verification step 2 does not work as written: generated code names an address as a label, `L_004D3E00:`, never as `0x004D3E00`, so that command reports 158 missing both before and after. The count above checks for the label of each target among the `^L_XXXXXXXX:` lines of the unit files.
- Functions rose by 258, not 158, and units by 6. `tools/find-pointer-tables.py` lists 392 pointer runs of 4+ in this ELF, so the pass seeds from other tables too. Rejected seeds stayed 0.
- eerecomp is about ten times slower (1.3 s → 12.8 s), still seconds.
- Headless run 003 (`docs/re/boot-log-003.md`): `entries.txt` emptied, `tools/port.sh --frames 1800` → `round 1: frame limit reached, 0 new entry points`. Its explore.log is byte-identical to run 002's.
- The hand-seeded build translated more than the patched one does: 237 units and 110381 labels against 225 units and 109014 labels. The 12 extra units are at `0x00488000`, `0x00498000..0x004A4000`, `0x004B0000..0x004C0000`, `0x004C8000`, `0x004CC000`; 1367 labels exist only in the seeded build and none only in the patched one. Not investigated: seeding entries above the old `text_end` raises `text_end`, which lets the original single-word candidate pass accept targets in that range, and nobody has checked whether those are real functions or data that decodes as code. Run 003 never needed them in 1800 frames.

A second problem surfaced while verifying, not caused by the patch: eerecomp does not delete unit files it no longer emits, and the fork's CMake globs `generated/<name>/*.cpp`. Going from 237 units to 225 left 12 stale `unit_*.cpp` files that referenced units no longer declared, and `[4/6]` failed with `use of undeclared identifier 'unit_00F3'`. Deleting the generated files and rerunning fixed it. Until eerecomp or port.sh cleans up, empty `generated/burnout3` whenever the unit count can shrink.

## Status

- Hand seed (158 entries in `games/burnout3/entries.txt`): applied by Opus 1 for run 002, removed for run 003. A copy is in `~/b3/out/run002/entries.txt`.
- Generic fix: applied in the fork on branch `burnout3` and verified as above; commit message `analysis: seed functions from pointer tables in data`.
- Open: the stale-unit cleanup, and whether the 12 units only the seeded build had contain real code.
