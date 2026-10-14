# ps2-web-recomp fork

- Upstream: https://github.com/snuri00/ps2-web-recomp (MIT, (c) 2026 Samir Nuri)
- Our fork: https://github.com/BlissVibes/ps2-web-recomp, branch `burnout3`
- Pinned commit: `17a5eee43eb013d43f26ace6dbff5139e5e8043c` (upstream `17a5eee43eb013d43f26ace6dbff5139e5e8043c`, 2026-10-10).
  This is the upstream commit our changes sit on, not the commit a build needs.
- Fork head: `9431274` on 2026-10-13, 35 commits on the pinned one. A build needs this commit.
  `git -C ~/b3/ps2-web-recomp log --oneline 17a5eee..burnout3` lists them and is right when this line is stale.
- Not pushed (checked 2026-10-13 with `git ls-remote --heads origin`). GitHub has only `main` at `17a5eee`
  for the fork: branch `burnout3` exists in the local clone alone, and the clone in `tools/wsl-setup.sh`
  fails on any other machine. This repo's 64 commits after its first (`130c60b`), up to `a6580e6`, are
  not pushed either. A push needs Mark's yes.
- Backups, made 2026-10-13, on the Windows disk outside both repos:
  `C:/Users/Mark/Documents/_vibe coding projects/_backups/burnout3/ps2-web-recomp-burnout3-a854891.bundle`
  (branch `burnout3`) and `Burnout3Decomp-main-a6580e6.bundle` (branch `main`), both older than the heads
  above. They are git bundles:
  `git clone -b burnout3 <bundle> ~/b3/ps2-web-recomp` restores the fork.
- Local clone (WSL): `~/b3/ps2-web-recomp`

Why not a submodule: the fork's `port.sh` writes `games/`, `generated/` and `build/`
inside its own tree. Those hold game-derived files that must never enter this repo,
so the fork lives outside the repo and is pinned here instead.

## Update procedure
1. `cd ~/b3/ps2-web-recomp && git fetch upstream && git rebase upstream/main`
2. Rebuild: `scripts/setup.sh`, then rerun `tools/port.sh` from this repo.
3. If the headless run still passes `tools/scan-log.py`, `git push --force-with-lease`,
   update the pinned commit above, and note the upstream date.

## Our changes on `burnout3` (keep generic, offer upstream)
- `cmake: PS2_WEB_SHELL lets a port supply its own shell page`: `CMakeLists.txt` gains a
  `PS2_WEB_SHELL` cache variable (default: the fork's own `web/shell.html`) used for
  `--shell-file` and the link dependency. `tools/build-web.sh` points it at this repo's shell.
  Commit `414c116`.
- `analysis: seed functions from pointer tables in data`: `recomp/analysis.cpp` gains
  `pointer_tables()`; eerecomp now translates functions reached only through a table of four
  or more code pointers (static constructor lists, vtables). Commit `0797e31`. See
  `docs/re/gap-001-static-constructor-table.md`.
- `hw: deliver INTC interrupts after a short latency so status polls can see the bit`:
  `runtime/src/hw.cpp`, `hw.hpp`, `system.cpp`. An INTC line that rises while enabled is
  delivered 32 EE cycles later, so a tight poll of INTC_STAT (libgraph's `sceGsSyncV`) sees
  the bit before the kernel acknowledges it. State version 2 → 3. This is a behaviour change
  for every game: when offering it upstream, ask for a run against GT4 and God of War II,
  which could not be tested here. Commit `07c4fb7`. See
  `docs/re/gap-002-vblank-poll-interrupt-latency.md`.
- `codegen: remove stale unit files before writing`: `recomp/codegen.cpp`. eerecomp deletes
  `unit_*.cpp` files in its output directory that the current pass did not emit; the build
  globs that directory, and leftovers from a pass with more units broke the compile.
  Commit `15b4c42`.
- `gif: reflect VIF1 MSKPATH3 and GIF_MODE in GIF_STAT`: `runtime/src/gs/gif.cpp`, `gif.hpp`,
  `runtime/src/vif.cpp`. VIF1 code 0x06 now sets a PATH3 mask flag that reads back as
  GIF_STAT bit 1, and GIF_MODE bits 0 and 2 are mirrored into GIF_STAT. PATH3 transfers are
  not actually stalled while masked. Commit `6862f7b`. See
  `docs/re/gap-003-gif-status-path3-mask.md`.
- `gif: hold PATH3 while masked and report the FIFO count`: `runtime/src/gs/gif.cpp`,
  `runtime/src/vif.cpp`. PATH3 data that arrives while PATH3 is masked is held and forwarded
  when the mask clears; GIF_STAT reports the held amount as the FIFO count (capped at 16) and
  the P3Q bit. Only the EE pass of the VIF1 stream changes the mask. Commit `8aca713`. See
  `docs/re/gap-004-path3-stall-and-gif-fifo.md`.
- `kernel: Deci2Call send requests fail like a retail console without a host`:
  `runtime/src/kernel.cpp`. Syscall 0x7C sub-commands 3 (ReqSend) and 7 (ExReqSend) return -1,
  so the kernel-library debug-TTY writer takes its error path instead of polling for a
  completion that never comes; other sub-commands stay no-ops. Commit `0995576`. See
  `docs/re/gap-005-deci2-tty-send-never-completes.md`.
- `iop: implement the IOP heap service (AllocSysMemory, LoadIopHeap, block queries) and end a
  guest call on a fault`: `runtime/src/iop/sif.cpp`, `kernel.cpp`, `kernel.hpp`, `cpu.cpp`,
  `libs.cpp`. SIF RPC server 0x80000003 now answers all nine libkernel heap methods (4 is
  `AllocSysMemory(size, type, addr)` on the existing allocator; 3 loads a disc file to an IOP
  address). A fault inside `call_guest` ends the call and returns 0; fault logging stops after
  256 faults and `dump()` prints the total; sysclib memory writes below `kUserBase` are
  reported (at most 16 lines). Commit `fca7bf4`. See
  `docs/re/gap-006-iop-heap-alloc-sysmemory-unhandled.md`.
- `web: read the disc from Module.discFile when the page supplies one`: `host/web.cpp`.
  `ps2web_fetch` reads each disc chunk with `Module.discFile.slice(...).arrayBuffer()` when the
  page has set `Module.discFile` (a `File`), and falls back to the Range request on
  `Module.discUrl` otherwise. This repo's shell sets it from the picked ISO or the cached copy,
  so the server never holds the disc. Commit `bbb10d4`.
- `spu2: write the mixer output areas every sample and test IRQA against them (RWA's IRQ
  clock)`: `runtime/src/iop/spu2.cpp`, `runtime/src/system.cpp`. Each 32-sample mixer batch
  now stores voice 1 and voice 3, the dry and wet L/R mixes (wet as silence, reverb is not
  modelled) and core 0's output into their SPU2 RAM write-back areas at a 0x200-halfword
  output position, and tests both cores' IRQA against every write, so an IRQA pointed into
  those areas raises the SPU2 IRQ periodically as on a console. Host audio is unchanged.
  State version 3 → 4. Commit `3e42c71`. See `docs/re/gap-007-spu2-output-areas-irq-clock.md`.
- `harness: periodic snapshots, single-call function harness and a runtime hook registry`:
  `host/main.cpp`, `CMakeLists.txt`, `runtime/include/ps2/system.hpp`, `generated.hpp`,
  `runtime/src/system.cpp`, `kernel.cpp`, `memory.cpp`, `dispatch.cpp`; new
  `runtime/include/ps2/harness.hpp`, `hook.hpp`, `runtime/src/harness.cpp`, `hooks.cpp`.
  `--snapshot-every N --snapshot-dir DIR` saves a state every N frames (and a state due on the
  last frame is now saved). `--call ADDR --snapshot FILE` runs one translated function on a
  loaded state with nothing but the EE running (no events, no IOP, frozen time; blocking,
  thread-changing and custom-handler syscalls abort) and writes the registers, changed memory
  ranges, I/O stores and syscalls as JSON plus a `.mem` file. `-DPS2_HOOKS=<dir>` compiles a
  folder of C++ functions that can replace translated ones when enabled with `--hook`; the
  check is in `dispatch()` only, so direct calls are not replaced yet. State version stays 4.
  Commit: `c31e314`. See `docs/re/function-harness-spec.md` and `docs/re/harness-log-001.md`.
- `harness: --fcr31 and 64-bit argument registers for --call`: `runtime/include/ps2/harness.hpp`,
  `runtime/src/harness.cpp`, `host/main.cpp`. `--fcr31 HEX` sets the FPU control/status
  register before the call, so sticky flags can be tested from a clear state; `--a0 x64:HEX`
  (any of `a0`-`a3`, `t0`-`t3`) sets all 64 bits of an argument register, the plain form stays
  32 bits sign-extended. Commit: `5b782b3`. See the follow-up in `docs/re/harness-log-002.md`.
- `gif: MSKPATH3 releases one PATH3 packet per unmask (race texture batches)`:
  `runtime/src/gs/gif.cpp`, `gif.hpp`, `runtime/src/vif.cpp`. PATH3 masking now works on GS
  packets (a packet ends with the data of the tag that has EOP): lifting the mask forwards
  exactly one held packet, a mask that arrives mid-packet lets that packet finish, and the rest
  drains only once PATH3 has demonstrably stayed open (a working VIF1 code after the unmask,
  new channel-2 data while unmasked, or the EE reading GIF_STAT). Before, the whole held buffer
  went at the first unmask, which overwrote all but the last of a frame's texture batches.
  New switch `PS2_TRACE_PATH3`. State version stays 4. Commit `c7abe06`. See
  `docs/re/gap-008-path3-mask-releases-one-packet.md`.
- `fpu, vu: MAX and MIN compare raw sign-magnitude patterns, no denormal flush`:
  `runtime/include/ps2/fpu.hpp` (new `fp::order`; `fp::max`/`fp::min` use it),
  `runtime/include/ps2/vu_ops.hpp` (`vmax`/`vmin`), `runtime/src/vu_jit.cpp` (the WebAssembly
  VU JIT emits the same order). MAX, MINI and the EE's `max.s`/`min.s` return one operand
  untouched, chosen by comparing the bit patterns as sign-magnitude integers, so a denormal
  (an integer moved through a float register with `MAXi vf0, I`) survives and -0 orders below
  +0. This is a behaviour change for every game; the JIT path ran in Chrome on 2026-10-12
  (`docs/re/web-log-001.md`). State version stays 4. Commit `1334998`. See
  `docs/re/gap-009-vu-max-mini-flushed-denormals.md`.
- `vu: Q and P results appear after their latency in the interpreters (cycle-scheduled programs)`:
  `runtime/src/vu_micro.cpp`. In both VU interpreter loops the result of DIV, SQRT and RSQRT (Q)
  and of the EFU operations (P) becomes visible a fixed number of instructions after the
  instruction that started it (7, 7 and 13 for Q; 11 to 54 for P), at once on WAITQ/WAITP, at
  program end, or when another operation of the same kind starts. Programs scheduled around the
  latency (a read of the old Q or P inside the window) now get the old value, as on hardware.
  Counted in instructions: pipeline stalls are not modelled. vurecomp still delivers Q and P at once (the WebAssembly VU JIT follows in the next entry). State version stays 4. Commit `82584e5`. See
  `docs/re/gap-010-vu-q-and-p-result-latency.md`.
- `vu jit: Q and P results appear after their latency in programs that read them early`:
  `runtime/src/vu_jit.cpp`. In programs that can read Q or P inside a latency window the
  WebAssembly VU JIT keeps a produced result in a wasm local with a countdown and delivers it
  when the countdown ends, on WAITQ/WAITP, when another operation of the same kind starts, and
  on every exit to the interpreter or return; other programs are generated as before. Falling
  back to the interpreter was rejected: 27 of 61 VU1 programs seen in a race read early. Costs
  1 to 4 points of VU-thread idle time in the race, no frame rate. Native output is unchanged
  (the JIT is not used natively). Commit `547315b`. See `docs/re/web-log-001.md`.
- `spu2: mix switches, effect-send write-back, input area played from RAM, core 0 into core 1`:
  `runtime/src/iop/spu2.cpp`. The mixer now honours the per-voice switches VMIXL/VMIXR (dry) and
  VMIXEL/VMIXER (effects), writes the effect-send voice mixes to their write-back areas (they were
  silence), plays each core's sound-data input from its input area in SPU2 RAM at the output
  position whoever wrote it (auto-DMA now fills that ring a half at a time instead of a private
  queue), applies MMIX to voices, input and external input, and sends core 0's output through
  core 1's external-input volume (AVOL) instead of adding it to the output directly. Reading the
  input area tests the address IRQ. Volume sweeps step every sample whether the voice plays or
  not. After reset the switches read all-on (MMIX `0xFFCF`), so software that never writes them
  sounds as before. Reverb itself is still not modelled: nothing returns from the effects mix.
  `PS2_TRACE_SPU2` also logs each DMA transfer (core, direction, address, size, output position).
  This is a behaviour change for every game that uses the switches. The saved-state layout is
  unchanged (version stays 4); `Core::input` and `input_read` stay in it unused. Commit `b9afd4a`. See
  `docs/re/gap-011-spu2-mix-switches-and-input-area.md`; tests in `tests/runtime/test_spu2_mixer.cpp`.
- `spu2: reverb; state files version 5 (version 4 still loads)`:
  `runtime/src/iop/spu2.cpp`, `runtime/include/ps2/state.hpp`, `runtime/src/system.cpp`. Each
  core's effects mix (voices by their effect-send switches, the sound-data input and, on core 1,
  core 0's output, by the `MMIX` effects bits) goes through the SPU reverb in the core's work
  area of SPU2 RAM (`ESA` to the end of the block `EEA` names) and comes back scaled by `EVOL`
  before the master volume: half-rate, one side per sample, a 39-tap half-band filter in and out,
  two reflection filters, four comb taps and two all-pass filters per side. It always runs and is
  heard; it writes the work area and tests the address IRQ there only with ATTR bit 7 set; it is
  skipped when effects are disabled with `EVOL` 0 or the work area is empty. The ten reverb gains
  of each core's volume block are kept (they were discarded) and `PS2_TRACE_SPU2` logs the
  volume block. State files: `state::kVersion` is 5 and `kOldestVersion` 4; `Stream::version`
  carries the file's version so a section can skip fields old files lack (the SPU2 section's
  reverb state). This is a behaviour change for every game that enables effects. Written from
  the published formula (PSX-SPX) and PCSX2's description of the PS2 register layout; the
  `0x324`/`0x328` sources are taken by position for both cores, where PCSX2 swaps them on
  core 1. Commit `40e47c7`. See `docs/re/audio-log-002.md`; tests in
  `tests/runtime/test_spu2_reverb.cpp`.
- `spu2: four-point sample interpolation`: `runtime/src/iop/spu2.cpp`. A voice's output sample is
  four neighbouring decoded samples weighted by the SPU's 512-entry table (index: the top eight
  bits of the fraction of the read position), where it was a straight line between two. The
  table is hardware data as published in PSX-SPX. The four samples run from the read position
  onwards, so a voice sounds two samples earlier than on hardware; the voice record and the
  saved-state layout are unchanged. This changes the tone of every voice in every game (a
  steady sample is 0.03 dB lower, the top octave slightly softer). Commit `e1ebb50`. See
  `docs/re/audio-log-002.md`.
- `spu2: a block's end flag acts when the block has been played; state files version 6`:
  `runtime/src/iop/spu2.cpp`, `runtime/include/ps2/state.hpp`. A voice decodes a block when it
  reaches it (it was one block ahead) and keeps the block before it for the interpolation, which
  now takes the sample at the read position and the three before it, as on hardware. When the
  read position leaves a block, its end flag acts: ENDX, the jump to the loop address, and for a
  sound without a loop the release and cut. Before, that happened a block before the end block
  was played, so one-shot sounds lost their last 56 samples and two-block ones were silent.
  The voice record has the same layout with another meaning (`Voice::buffer`, `position`), so
  `state::kVersion` is 6; older files load, a playing voice going on from the block it had
  decoded ahead. Behaviour change for every game. Commit `1072cf9`. See
  `docs/re/audio-log-002.md`.
- `vu: FMOR and FSOR OR the flags into the register (they had FCOR's all-ones test)`:
  `runtime/src/vu_micro.cpp`, `runtime/src/vu_jit.cpp`, `recomp/vurecomp.cpp`. FMOR stores the
  MAC flags ORed with `VI[is]` and FSOR the status flags ORed with the 12-bit immediate; all
  three implementations answered 1 when the OR was all ones and 0 otherwise, which is FCOR's
  rule. Burnout 3's segment-against-triangle test (a VU0 microprogram) gathers six flag words
  with FMOR, so it accepted every hit and cars were thrown off the map. Behaviour change for
  every game whose VU code uses either instruction; recorded VU1 programs that use them need
  recompiling (Burnout 3's do not). State version unchanged. By Opus 2. Commit `76eb759`. See
  `docs/re/gap-012-vu-fmor-fsor-wrong-meaning.md`; guard in `tests/test_vurecomp_flag_or.py`.
- `recomp, hooks: generated code offers every function entry to the hooks; shadow mode`:
  `recomp/codegen.cpp`, `runtime/src/hooks.cpp`, `dispatch.cpp`, `system.cpp`,
  `runtime/include/ps2/generated.hpp`, `hook.hpp`, `system.hpp`, `host/main.cpp`. Part 3b of the
  function harness. The entry switch of a generated unit calls `enter()` for an address that is
  a function when `g_hooks` is set, so an enabled hook replaces the function for direct callers
  too and `--trace-calls` lists directly called functions; `dispatch()` calls `offer()` and the
  unit's check does not count that entry twice. `ps2run` prints a `[hook]` line per enabled hook
  at exit. `--shadow N`: hooks do not serve; every Nth call of each hook runs the recompiled
  function and then the hook on a copy of the call and compares registers and memory, keeping
  the recompiled outcome (`PS2_SHADOW_LIVE=hook` keeps the hook's instead, with the recompiled
  function's cycles). Generated code must be regenerated to get the check (one line per
  function); old generated code keeps working. No cost measured with hooks off. Commit
  `b2c8a15`. See `docs/re/harness-log-003.md`; tests `tests/test_recomp_hook_check.py`,
  `tests/runtime/test_hooks.cpp`.
- `vu: the fast C++ paths add as the VU adder does (one guard bit, truncated), not as the host rounds`:
  `runtime/include/ps2/vu_ops.hpp`, `runtime/src/vu_micro.cpp`. ADD, SUB, MADD, MSUB and the forms
  into ACC share one adder that keeps one guard bit and truncates; the helpers of the recompiled
  VU1 programs (scalar and four-lane) and the fast interpreter used the host's sum. The host sum
  is now corrected by at most one unit in the last place from its exact error term and matches
  `fp::add`. Multiplication is still the host's. Behaviour change for every game. By Opus 2.
  Commit `a765dc8`. See `docs/re/gap-013-vu-adder-host-rounding.md`; test
  `tests/unit/test_vu_adder.cpp`.
- `vu jit: the ADD family adds as the VU adder does; SUB, MADD, MSUB keep host rounding for now`:
  `runtime/src/vu_jit.cpp`. The WebAssembly VU JIT emits the same correction for ADD, ADDi, ADDq,
  ADDbc and their forms into ACC only: the whole rule left the VU thread no idle time in the race.
  Costs 8 to 10 points of VU-thread time in Burnout 3's race. By Opus 2. Commit `f2f31d2`. Same doc.
- `gif: the PATH3 hold buffer keeps a read position instead of erasing from the front`:
  `runtime/src/gs/gif.cpp`. Data held while PATH3 is masked was erased from the front of a byte
  vector at each release (moving everything behind it), and the vector's storage was given away
  when it emptied. A release now moves a read position; the storage is kept; the passed part is
  dropped only before the storage would grow and before a state is written, so states are
  unchanged. Burnout 3's race, per frame: 10 reallocations and 20.6 MB moved before, none now.
  No behaviour change. Commit `5849b06`. See `docs/re/web-log-003.md`; test
  `tests/runtime/test_gif_path3.cpp`.
- `dispatch: an idle call takes whole turns of a waiting loop off the time slice`:
  `runtime/src/dispatch.cpp`, `runtime/include/ps2/system.hpp`, `runtime/src/system.cpp`,
  `host/web.cpp`. `ps2::add_idle_call(function, return address, waiting register)` names a call a
  game makes on every turn of a waiting loop. Once two turns in a row have cost the same cycles
  the dispatcher subtracts whole turns from the slice and leaves the last one or two to run, so
  the slice ends on the same cycle as with the loop spinning. `PS2_NO_IDLE=1` turns it off;
  `ps2run` prints the number of turns skipped at exit. The web host now calls `register_hooks()`
  as the native host does (a game registers its idle calls there), so the page must be built
  with `PS2_HOOKS` to get them. No behaviour change for a game that registers nothing. Burnout 3:
  identical cycles, primitives and audio; with the hold buffer change the page's emulation
  thread does 23% less work in the race. Commit `a854891`. Same doc; test
  `tests/runtime/test_dispatch_idle.cpp`.
- `vu0: a fast loop over decoded instructions with exact arithmetic from host floats`:
  `runtime/src/vu_micro.cpp`, `vu_micro.hpp`, `runtime/include/ps2/vu_exact.hpp`, `memory.cpp`,
  `system.cpp`, `CMakeLists.txt`. VU0 microprograms ran through the exact loop (decode every time,
  integer float routines). The new loop runs decoded images kept by content and takes its arithmetic
  from `vu_exact.hpp` (product exact in double and cut as the VU cuts it, sum from a two-sum with a
  one-unit correction, undecidable lanes to the integer routine); flags, latency and order are the
  exact loop's code. `PS2_VU0_EXACT=1` keeps the old loop, `PS2_VU0_CHECK=1` runs both and compares.
  `-ffp-contract=off` for every target. No behaviour change (proven on every call of a race).
  Native 24.4 to 10.2 ns an instruction; the page saves 3.9 ms a frame. Commit `d9ae500`. See
  `docs/re/web-log-004.md`; tests `tests/unit/test_vu_exact.cpp`, `tests/runtime/test_vu0_loop.cpp`,
  `tests/wasm/run.sh`.
- The audit of 2026-10-13 (`docs/re/open-issues.md`), six fixes made on branch `fixes01` and picked
  onto `burnout3`:
  - `spu2: the mix is clamped before the master volume; exponential envelope steps round down;
    cheaper reverb addresses`: `runtime/src/iop/spu2.cpp`, `spu2_work_area.hpp`. Behaviour change for
    every game (clip level follows the master volume; exponential decays end); reverb output
    bit-identical. Commit `1a07150`. Tests in `tests/runtime/test_spu2_mixer.cpp`, `test_spu2_reverb.cpp`.
  - `hw: a late EE timer event no longer loses its compare or overflow`: `runtime/src/hw.cpp`.
    Behaviour change for every game; Burnout 3's race to 14100 ends 9 cycles later. State layout
    unchanged. Commit `d313391`. Test `tests/runtime/test_ee_timer.cpp`.
  - `gs queue: a message of any size up to the ring's capacity gets through; GIF data is posted in
    pieces; rings of 16 MiB`: `runtime/src/gs/queue.cpp`, `queue.hpp`, `gif.cpp`, `gs.cpp`,
    `system.cpp`. Page only. Commit `46e093b`. Tests `tests/runtime/test_gs_ring.cpp`,
    `test_gif_path3.cpp`.
  - `gpu: the upload cache keeps more than one entry`: `runtime/src/gs/gpu/gpu.cpp`,
    `upload_cache.hpp`. Commit `f0c99aa`. Test `tests/runtime/test_upload_cache.cpp`.
  - `hooks: shadow mode compares hi1, lo1 and sa, and counts FPU status differences on their own`:
    `runtime/src/hooks.cpp`. Commit `e543da1`. Test `tests/runtime/test_hooks.cpp`.
  - `recomp: an entry known only from an address constant or a data word is a function when the code
    before it returns`: `recomp/analysis.cpp`. Burnout 3: 6,113 functions become 7,932; takes effect
    when the game is regenerated (not done yet). Commit `f79a015`. Test `tests/test_recomp_hook_check.py`.
- `gs queue: a poster's wait for room ends after 2 ms; a dump of what each thread waits for`:
  `runtime/src/gs/queue.cpp`, `queue.hpp`, `gs.cpp`, `gs.hpp`, `vif.cpp`, `vif.hpp`, `system.cpp`,
  `runtime/include/ps2/system.hpp`. In the page a futex wake from the VU thread to the waiting poster
  was lost and the emulation thread froze once the 16 MiB VU ring filled; the wait now also ends
  after 2 ms (4 to 7 lost wakes a race are counted). `describe_threads()` prints what each thread
  waits for. Page only. Commit `4c108df`. See `docs/re/web-log-004.md` section 7.
- `web host: the disc cache never hands over another chunk; game pace independent of the display;
  input on every refresh`: `host/web.cpp`, `host/disc_cache.*`, `host/pace.*`, `host/sha256.*`,
  `CMakeLists.txt`. The disc cache could return the next chunk's 256 KB once full; pacing no longer
  slows the game on displays under 60 Hz and survives a stalled audio device (`?pace=old` keeps the
  old waits); held and disc time counted apart from idle; `ps2web_describe`, `ps2web_stack_used`,
  `ps2web_heap_top` exported; `PS2_WEB_ELF_FROM_DISC` (off) reads the executable from the disc.
  Commit `9431274`. Tests `tests/runtime/test_disc_cache.cpp`, `test_web_pace.cpp`,
  `test_web_sha256.cpp`, `tests/pad.test.mjs`, `tests/page.test.mjs`.
