# Gap 012: FMOR and FSOR were given FCOR's meaning, so the game's segment-against-triangle test accepted every hit and cars were thrown off the map

Date: 2026-10-13. Found while looking at the race oddities listed at the end of `docs/re/gap-010-vu-q-and-p-result-latency.md`. Analysis, fix and verification: Opus 2, in the worktree `~/b3/ps2-gs008` (branch `gap008`). Frames, dumps and traces are under `~/b3/out/odd01/`; nothing from the game is in this repository.

## Symptom

Four of the listed oddities, and one found later, are this one fault:

- The "CRASHED!" banner at the start of the race while the car drives off normally.
- The speed readout of 4205 mph for one sampled frame. Frame by frame it is 107 mph, then 6594 in the next frame, then 6066, 5494, 5003 and so on down: a single kick that a filter in the readout lets decay by 8 percent a frame.
- The "crash sequence" of a view from inside a roof and then mountains over a dark blue plane with no car and no road.
- The EA Trax song banner drawn as a blank white bar (seen in the browser at frame 13000).

Because of it, every race measured before this fix, pictures and sound, was a race in which cars were being thrown off the map: the player's car within eight seconds of the start, the AI cars beside it with it, and at least one AI car before that. The frame rates, the idle figures of `docs/re/web-log-001.md`, the "first crash" audio of `docs/re/gap-011-spu2-mix-switches-and-input-area.md` and the race01 snapshots are all of that race.

The fault was there before gaps 008 to 010: the frames saved with the race01 snapshots, made before any of them, show 4205 mph at frame 13050 as well. It does not depend on the VU path: the same frames come out with `PS2_VU_INTERPRET=1`, `PS2_VU_EXACT=1` and `PS2_NO_VU_THREAD=1`.

## What the Xbox reimplementation says

reB3 was read first for each oddity (`docs/RE_TAKEDOWN_FX.md`, `RE_POSTFX.md`, `RE_FRONTEND.md`, `RE_NOTES.md`, `src/burnout3_hud.c`). What it gives is the game's intent, and it pointed away from the camera and the HUD:

- Retail has no crash camera mode. The player-crash view is the ordinary chase camera still running over the wreck's transform; its eye is always about 7 m from a point 2 m ahead of the car (`RE_TAKEDOWN_FX.md` sections 8 and 9, the chase law run under emulation there). So a view of distant mountains means the car is there, not that the camera was cut away.
- "CRASHED!" is callout message `0x7F`, accepted only while the racecar that owns the callout slot has its own crashed flag set (section 4 there). A rival's crash does not show it to the player. So a banner at the start means the player's car was put into the crashed state at the start.
- reB3 did not recover the source of the speed readout or any clamp in it, so it cannot say whether 999 is a limit; it notes scalar speed is the length of the velocity vector. 4205 mph is a real velocity, not a formatting fault.
- reB3 met the same class of fault in its own port: a wreck given +105.7 m/s of vertical speed by its contact code on a kerb (`RE_NOTES.md` section 16.2). Its cause there was its own harness contact; it is not ours, but it is where to look.

- On the suspension, reB3 says retail holds the body up at its four wheel stations with a spring pass that has a bottom-out cut averaged over the wheels that contribute (its `FUN_00123FD0`, which it describes and did not port). That is the averaging seen in step 2 below. It has nothing on how the wheel finds the ground.

reB3 has nothing on VU microcode: the Xbox build has none.

## Cause

The chain was followed backwards from the picture with a temporary write watch in the worktree build (the host page of a guest address is write-protected, each write is single-stepped and logged with the guest `ra`, `sp` and argument registers). All of it is from the run that starts at the race01 snapshot of frame 12900, which repeats exactly.

1. The player's vehicle is at `0x0057BAF0` and its frame matrix at `0x0064D3B0`. In frame 13040 the frame's height goes from 148.966 to 388.865 in one physics substep. The write is in `rigid_body_integrate` (`0x001AC470`) at `0x001ACC94`, where the vehicle's position-correction vector (vehicle `+0x130`) is added to the frame position and cleared.
2. That vector was set to (-0.09, 239.90, 0.25) by `suspension_pass` (`0x001CC900`) in the same substep. `suspension_pass` adds, for each wheel whose spring is compressed past a quarter of its travel, the wheel's contact normal times the excess, and averages. The excess for the front right wheel was 364.8 m.
3. The wheels' contact points are written by the wheel ground query at `0x001CE1A0`, called from `wheel_prepass` (`0x001CDB70`). In that substep the front right wheel at height 149.02 was given a contact point at height 513.47, and the rear right wheel one at 263.75. The left wheels had 148.66, which is the road. The car was on the kerb beside a railing, right wheels on the kerb.
4. In the next substep the car is 240 m up, the wheels find the road 191 m below them, and `suspension_pass` adds a force of -2.8e8 on the vertical axis. `rigid_body_integrate` turns that into a velocity of -2947.6 m/s, which is 6594 mph.
5. The two AI cars beside the player (vehicles `0x00587110` and `0x00588690`) are lifted 27 m and tipped 72 degrees four frames later and marked crashed. They are in the cheap simulation that `authority_ladder` selects, which re-seats a car on the ground every frame with the function at `0x001BFE80`; that function places the car with a ground query of its own (the call at `0x001C0B7C`), which was not traced further.

The ground query walks the triangles gathered near the car and calls the function at `0x00274C90` for each, keeping the hit with the smallest distance along the segment. It starts from a best distance of 999, so it relies on `0x00274C90` to refuse a hit that is outside the triangle or beyond the ends of the segment.

`0x00274C90` is a wrapper round a VU0 microprogram, run with `vcallms` at microprogram address 984 (`0x3D8`): segment ends in `vf2` and `vf4`, triangle corners in `vf5` to `vf7`, the answer in `vi1` (hit or not) and `vf24` (distance along the segment and the two barycentric coordinates). The program makes its decisions with flag instructions. Its last part (the instruction words are in the trace under `~/b3/out/odd01/`, not here):

- At `0x4A8` an FMAND sets `vi8` to the MAC flags ANDed with `vi3` (`vi3` holds `0x0088`, the sign and zero bits of x), and the branch at `0x4B0` leaves without a hit if `vi8` is not zero.
- At `0x4E0` an FMOR sets `vi8` to the MAC flags ORed with `vi0`, and five more from `0x4E8` to `0x508` OR the MAC flags into `vi8` again. Six flag words in a row are gathered: the results of the six subtractions that compare the distance and the two coordinates with their limits.
- At `0x510` `vi9` is set to `vi8` ANDed with `vi3`, and the branch at `0x520` leaves without a hit if `vi9` is not zero. Otherwise `0x530` sets `vi1` to 1.

FMOR writes the MAC flags ORed with the source integer register. FSOR writes the status flags ORed with a 12-bit immediate. FCOR is different in kind: it sets `vi1` to 1 if the clip flags ORed with its 24-bit immediate are all ones, and to 0 otherwise. The fork had given FMOR (lower opcode `0x1B`) and FSOR (`0x17`) FCOR's form: the register was set to 1 if the OR was all ones and to 0 otherwise. MAC flags are never all ones here, so `vi8` stayed 0 through the six FMORs, `vi9` was 0, and the program reported a hit whenever the first test passed.

The first accepted hit that should have been refused, from a trace of the program (frame 13040): segment from (5050.515, 149.380, 1802.505) to (5050.515, 148.622, 1802.504), distance along the segment 1.2586 (beyond the end), coordinates 0.331 and 1.201 (outside the triangle), `vi1` = 1.

Source for the three definitions: PCSX2's VU interpreter (`pcsx2/VUops.cpp`, `_vuFMOR`, `_vuFSOR`, `_vuFCOR`), read for behaviour only; row in `docs/research/PROVENANCE.md`.

## Fix (generic, in the fork: `runtime/src/vu_micro.cpp`, `runtime/src/vu_jit.cpp`, `recomp/vurecomp.cpp`)

The same mistake was in all three places that implement the lower instructions, and all three are corrected:

- The interpreter (`vu_micro.cpp`, the `lower` switch): case `0x17` stores `(status | imm12) & 0xFFF`, case `0x1B` stores `(mac | vi[is]) & 0xFFFF`.
- The WebAssembly JIT (`vu_jit.cpp`): the same two cases emit the OR and the mask and no longer the comparison with all ones.
- The VU recompiler (`vurecomp.cpp`): the same two cases emit the same two expressions.

FCOR (`0x13`) was right and is unchanged.

### What this changes elsewhere

It changes the result of FMOR and FSOR for every game, in VU0 and VU1 microcode, on every path. No saved-state field changes and `kStateVersion` stays as it is. The pre-recompiled VU1 programs of Burnout 3 (`generated/burnout3`) contain neither instruction, so nothing has to be regenerated for this game; a game whose recorded VU1 programs use them would need its programs recompiled.

The race01 snapshots (`~/b3/out/snaps/race01`) were saved with the fault in effect and hold its results (an AI car already on its side). The pre-race state (`~/b3/out/run011/pre-race.state`, frame 12300) is from before the start and is clean.

## Verification

Native, CPU rasteriser, from the pre-race state with accelerate held from frame 12600 to frame 14100, one frame every 25, the build without the change and the build with it side by side (`~/b3/out/odd01/full-before`, `full-after`).

Without the change: "CRASHED!" at the start, the launch, the four-digit speed and the sky sequence as described.

With the change:

- The start shows "Get Ready!" and "GO".
- The speed climbs from 50 mph at 12600 to 131 mph at 13750 with no jump.
- Rivals are on the road round the car with their position tags; the position changes between 4th and 2nd; the game awards Tradin' Paint, Took 3rd, Took 2nd, Extreme Escape and Takedown 2 in a row; a rival's crash is shown in the takedown cinematic ("Psyche Out!"), which reB3 describes as the one case where the view leaves the player's car.
- The EA Trax banner shows the song and artist.
- No frame shows the car off the road surface.

The race is a different race from the first frame on, so later frames cannot be compared one for one with the earlier survey.

### Browser

The page built from the worktree with the fix (`~/b3/out/odd01/web-gap012`, `ps2web.wasm` 24,688,062 bytes) was run in Chrome with the faked pad to frame 14100: it boots, reaches the race and ends with the status at "Running" and no JavaScript error. The screenshots (`~/b3/out/odd01/web/`, frames 12625 to 14000) show the same race as the native run: "Get Ready!", rivals with their position tags, the song banner with its text, the Psyche Out cinematic, Tradin' Paint, Took 3rd, a wreck at 14000, and no launch.

The frame rate of that run is not a measurement: the page showed 35 to 45 frames a second in the race with the emulation thread at 0% idle, but eight other emulator runs were going on the machine at the time (load average 29 to 35 on 16 threads). The corrected race does more work than the old one (six cars and the traffic are simulated instead of being parked in the sky), and the native run with the fix was slower than the one without, so some drop is expected; how much has to be measured on a quiet machine.

### Guard

`tests/test_vurecomp_flag_or.py` runs the fork's `vurecomp` on a made-up five-instruction program with FMOR, FSOR and FCOR and reads the generated C++: plain OR for the first two, the all-ones test only for FCOR. It fails on the recompiler as it was (three of four tests) and passes with the fix; it is skipped unless `B3_VURECOMP` names the tool. It guards one of the three places. The interpreter does not build alone (it needs the GS queue, the kernel and the CPU state), so the interpreter and the WebAssembly JIT are covered only by the race itself.

## Status

Fixed in the fork: commit `76eb759` on `burnout3` ("vu: FMOR and FSOR OR the flags into the register"). Patch as reviewed: `~/b3/out/odd01/gap012-fmor-fsor.patch`.

## Still to look at

- The row of red crosses is still drawn (12600 and 13450 with the change). reB3 identifies it as world geometry: road-closed boards (`Barrier` material of the static chevron group) at side-street mouths, drawn last, with the ordinary depth test. Whether ours are hidden correctly by nearer geometry is not checked yet.
- White-outs of the 3D view at 13350 and 13800, and a blue wash at 12900. reB3 says retail changes no post-processing on a slam, crash or takedown, and that the composite is additive and over unity by design, so a wrong blur strength would do this.
- The strength of the blur and the boost bar. reB3's reading of the boost bar is that an empty bar is a strip of tyre-tread blocks, so the dotted strip is probably right.
