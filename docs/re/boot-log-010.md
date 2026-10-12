# Headless run 010 — 2026-10-12

Fork: branch `burnout3` at 3e42c71 (gaps 001-007 committed), tree clean. Command, from the
fork root in WSL with `PS2_GPU_FALLBACK=1`:
`tools/port.sh --frames 3600 --press 1900:start --press 2300:cross --press 2700:cross
--press 3100:cross --press 3500:cross`, 13:53-13:54 (74 s end to end), port rc 0, neither
limit hit. `entries.txt` held `469ab0`. Wrapper log `~/b3/out/port-010.log`; explore.log,
entries.txt, the scan output and the twelve frames are in `~/b3/out/run010/`.

**Run 010 itself ends on the title screen, because its presses could not get past it. Four
probe runs with other press schedules (below) get further; the best two go through profile
creation, the main menu, the Crash Nav region and event screens, and into the Race Training
video. No halt, no
missing code and 0 IOP faults in any of them, up to 12,000 frames.**

## eerecomp
```
225 units written to /home/mark/b3/ps2-web-recomp/generated/burnout3
```

## Exploring rounds
```
round 1: frame limit reached, 0 new entry points
```
`[5/6]`: none recorded. `[6/6]`: nothing to rebuild; `ps2web.wasm` 24,691,349 bytes.

## Scan
```
frames reached: 3600 of 3600
halted: no (frame limit reached)
primitives drawn: 791207
gaps: 1 distinct, 1 total
  iop_missing  modload #18 called from 0x0006C1B4  x1
result: GAPS
```
The one finding is the old `modload #18` line. No unknown tags.

## Frames
Twelve frames, 640×448, `frame_00300` … `frame_03600`, all different.

| Frame | Visible |
|---|---|
| 300 | The LOADING screen, progress bar full. |
| 600 | EA GAMES logo on blue: "Challenge Everything", "EA GAMES is an ELECTRONIC ARTS brand". |
| 900 | Criterion Games logo (bull emblem) on black. |
| 1200 | Fully black: a transition. |
| 1500 | Intro FMV: the blue holographic globe (Europe and Africa), "CRASH NAV" and a column of numbers on the right. |
| 1800 | Intro FMV: a blue sports car seen from directly above on asphalt, headlights on. |
| 2100 | Title screen: "A Criterion Games Production in association with Electronic Arts", the Burnout 3 Takedown logo over a street scene with a black and a blue car, "Press START button to continue", the copyright line. |
| 2400 | Title screen, same text; the video behind the logo shows a night street with neon signs. |
| 2700 | Title screen; the background video shows a tree-lined road. |
| 3000 | Title screen; the background video shows a street with Chinese shop signs. |
| 3300 | Title screen; the background video shows a blue car crashing in sparks under a steel bridge. |
| 3600 | Title screen; the background video shows a road bend with red warning arrows. |

No menu in this run. The title's background video keeps playing, so the game is not stuck.

### Where the presses landed
- `1900:start` landed in the intro FMV and skipped the rest of it. With no press before the
  title, the title appears about 300 frames later: the first probe's frame 2400 is
  pixel-identical to this run's frame 2100.
- `2300`, `2700`, `3100` and `3500` cross all landed on the title screen. The title only
  takes START, so they did nothing. No press was eaten by a transition; the schedule simply
  had no second START.
- Run 009's frame 1800 already showed the title because its default presses (start at 300,
  cross every 300) skipped the logos and the FMV early.

## What the run did
- explore.log is 10,338 bytes, 199 lines: 3600 frames, 17,698,702,311 EE cycles,
  `[iop] 0 faults`. The lines before the halt are the same as in run 009 (27 modules, the
  same B3ROUTE TTY lines, the same `[iop-io]` lines); nothing new on the IOP or SIF side.
  A new memory card was created (`explore.ps2 (new, unformatted)`).
- GS: 791,207 primitives (206,507 in run 009 at 1800 frames), 1,975 GPU submits, 111,027
  texture uploads, 1,193 CLUT loads, 108,014 raster passes. DMA starts: ch1=1964 ch2=3145
  ch3=1170776 ch4=3922 ch8=110567 ch9=1141395. GIF tags: path 1 none, path 2
  `packed 869194 reglist 244535 image 422400`, path 3 `packed 30148 reglist 1962 image 10828`.
- IPU: `commands: 0:3462 2:1168561 3:4584423 4:1768336 5:210 6:1 7:2215 9:1 stalls 462
  out 156002608 qwords`, the intro FMV and the title's background video.

End state (explore.log from the halt line on; the IOP list is cut to the RWA threads):
```
[ee] halted: frame limit reached
[thread] 1 status 1 prio 50 wait 0/0 pc 0x001F24E1 ra 0x001F2518
[thread]   stack: 001F4480 00132F40 0012ECE8 00100210 00115248 00115150 001001F0
[thread] 2 status 4 prio 0 wait 2/3 pc 0x0010BE68 ra 0x0010CB68
[thread] 3 status 2 prio 54 wait 0/0 pc 0x0010BD48 ra 0x00237188
[thread] 4 status 2 prio 54 wait 0/0 pc 0x0010BD48 ra 0x00237188
[thread] 5 status 2 prio 54 wait 0/0 pc 0x0010BD48 ra 0x00237188
[thread] 6 status 4 prio 33 wait 1/0 pc 0x0010BD48 ra 0x0022E248
[thread] 7 status 4 prio 22 wait 2/19 pc 0x0010BE68 ra 0x002AF330
[thread]   stack: 002A2124 002AF2A0 002AF340 001D2F50 001D2EE0 002FC530
[thread] 8 status 4 prio 1 wait 2/26 pc 0x0010BE68 ra 0x00242AA8
[sema] 3 count 0 waiters 1
[sema] 19 count 0 waiters 1
[sema] 26 count 0 waiters 1
[iop] 0 faults
[iop] thread 17 status 4 prio 115 wait 2/0 pc 0x00041F4C
[iop] thread 18 status 4 prio 114 wait 3/11 pc 0x000412D8
[iop] thread 19 status 4 prio 12 wait 3/15 pc 0x0003D850
[iop] thread 20 status 4 prio 12 wait 3/17 pc 0x0003D850
[iop] thread 21 status 4 prio 9 wait 1/0 pc 0x0003F698
[iop] thread 22 status 4 prio 9 wait 1/0 pc 0x0003F698
[iop] thread 23 status 4 prio 9 wait 1/0 pc 0x0003F698
[sif] 0 packets wait for the EE (processed 1)
[gs] 791207 primitives drawn
[dma] starts: ch0=0 ch1=1964 ch2=3145 ch3=1170776 ch4=3922 ch5=0 ch6=0 ch7=0 ch8=110567 ch9=1141395
[ee] stopped after 3600 frames, 17698702311 cycles: frame limit reached
```
This is the same thread picture as run 009's end state: thread 1 running, threads 3-5
ready, 7 and 8 in their idle semaphore waits.

## Probe runs past the title
Run directly with `build/native-burnout3/ps2run` (no rebuild, fork tree untouched), each
with its own memory card under `~/b3/out/run010/probe*/`, output capped at 20 MB. A press
is written `frame:button:frames held`. All four ran to their frame limit with rc 0,
`[iop] 0 faults`, a log of about 10 KB and no misses file (no missing code).

| Probe | Frames | Presses | Furthest screen |
|---|---|---|---|
| `probe` | 3300 | `2200:start:10 2500:start:30 2800:cross:10 3000:cross:10` | Load Profile list |
| `probe-b` | 6000 | `1900:start:10 2300:start:10 2500:down:6 2600:cross:10`, then cross every 200 from 2800 | "Would you like to save your profile?" loop |
| `probe-c` | 7000 | as probe-b to 2600, then `2800:cross 3000:cross 3150:up:6 3250:cross`, cross every 250 from 3500 | Race Training video |
| `probe-d` | 12000 | as probe-c to 4500, then `5000:start`, cross every 300 from 5300, cross held from 7000 | end of the Race Training video |

What each screen showed, in order of the route (frame numbers from probe-c unless said):

| Frame | Visible |
|---|---|
| `probe` 2200 | START lands in the intro FMV (no earlier press), skipping to the title. |
| `probe` 2600 | After START on the title: SELECT OPTION, "Burnout 3 requires a Profile to track progress. Please load an existing Profile or create a new Profile.", LOAD PROFILE highlighted, NEW PROFILE below; "Change" and "Confirm ✕" prompts. |
| `probe` 2900-3300 | DRIVER DETAILS / LOAD PROFILE: "Empty", PROFILE 1 highlighted, PROFILE 2, PROFILE 3. Cross on the empty profile does nothing. |
| 2700 | After down and cross: DRIVER DETAILS / NEW PROFILE, an on-screen keyboard, the name field pre-filled with BURNOUT, the cursor on Done. |
| 2900 | "Would you like to save your profile?" YES / NO, NO highlighted. |
| 3100 | After cross on NO: "Autosave will be disabled. Are you sure?" YES / NO, NO highlighted. Cross on NO returns to the save question (probe-b loops between the two until its end); up then cross picks YES. |
| 3375 | Main menu: Burnout 3 Takedown logo, SELECT OPTION, BURNOUT 3 WORLD TOUR highlighted, then SINGLE EVENT, MULTIPLAYER, ONLINE, DRIVER DETAILS; a scrolling "RACE AND CRA…" help line and an EA Trax pop-up ("Fall Out Boy, Reinventing The Wheel To Run Myself Over, Take This To Your Grave"). |
| 3750 | CRASH NAV / SELECT REGION: a 3D globe with the USA lit in green, USA highlighted and marked "New", EUROPE and FAR EAST greyed; counters 0/55 and 0/173. |
| 4250 | CRASH NAV / SELECT EVENT, SILVER LAKE: an aerial map with the course drawn on it, RACE highlighted, then COMPACT GRAND PRIX, ELIMINATOR, SPECIAL EVENT; "COMPACT SERIES". |
| 4750-12000 | RACE TRAINING: a tutorial video in a frame ("ONCOMING", "TAKEDOWN!" with a hand-drawn "MORE BOOST!" note, crashes). Neither cross nor START (probe-d, frame 5000) skips it; probe-d's frames up to 7000 are pixel-identical to probe-c's. At frame 12000 the video ends on the Burnout 3 Takedown logo, still under the RACE TRAINING heading. |

Other observations from the probes:
- Pad input works for start, cross, up and down, with 6 to 10 frame holds.
- PATH1 is used for the first time: `[gif] path 1 tags: packed 166636` in probe-c and
  probe-d, none in run 010, `probe` or `probe-b`. So the VU1 microprograms first run
  somewhere between the main menu and the event screen (the 3D globe is the likely place).
  The count is the same at 7,000 and 12,000 frames, so the training video adds none.
- probe-d totals at 12,000 frames: 5,077,243 primitives, 58,986,506,875 EE cycles, IPU
  `out 493937632 qwords`. Its EE thread dump is the same as run 010's above.
- The race itself was not reached: the training video lasts about 7,250 frames (two
  minutes) from the cross on RACE, and the run ended as it finished.

## Where it stops
Nowhere yet. Run 010 waits on the title for a START it was never sent. The probes got as
far as the end of the unskippable Race Training video with nothing logged as wrong. The
next unknown is what follows that video: the loading screen for Silver Lake and the race.

## Verdict
The game boots headless to its menus and takes pad input: title, profile, main menu,
region and event selection all draw, with 0 IOP faults and no missing code. Milestone 1's
written exit condition (a CLEAN scan on a 3600-frame run whose frames show the main menu)
is not met in two respects: the scan still says GAPS for the old `modload #18`, and the
main menu first appears near frame 3375 only with the probe schedule, which the wrapper
run did not use.

A press schedule that reaches the race selection and starts the training video
(frame:button:frames held):
`1900:start:10 2300:start:10 2500:down:6 2600:cross:10 2800:cross:10 3000:cross:10
3150:up:6 3250:cross:10 3500:cross:10 3750:cross:10 4000:cross:10 4250:cross:10
4500:cross:10`. To see the race, press nothing more until after frame 12000 and run at
least 14,000 frames. That run was not made: the gap loop was paused on Mark's instruction
after the probes.
