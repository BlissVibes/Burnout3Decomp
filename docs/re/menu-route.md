# Menu route: boot to a Single Event race

Date: 2026-10-12. Fork: branch `burnout3` at 3e42c71, no rebuild. Found with headless
`ps2run` probes (run 011), each with its own new memory card under `~/b3/out/run011/`,
frames written every 25. Screenshots of every page are kept outside the repo (they are
game output): PPM under `~/b3/out/run011/pages/`, PNG copies in the session scratchpad.

A press is written `frame:button:frames held`, as `ps2run --press` takes it.

## Result

From a cold boot with an empty memory card, thirteen presses reach a running Single Event
race. The first in-race frame (position, lap and speed on screen) is **frame 12575**. Two
probes with the same presses up to the car selection showed the same pages at the same
frames.

The race runs, but it is not drawn correctly yet: see "State of the race" below.

## Schedule

```
1900:start:10 2300:start:10 2500:down:6 2600:cross:10 2800:cross:10 3000:cross:10
3150:up:6 3250:cross:10 3600:down:6 3750:cross:10 3950:cross:10 4150:cross:10
4350:cross:10
```
Run at least 12,600 frames to see the race start; the probe ran 14,500. Nothing is pressed
after frame 4350: the Race Training video that follows cannot be skipped with cross or
START and lasts until about frame 11,925.

## Pages

| # | Page | Frame shown | Press that got there | Highlighted / visible |
|---|---|---|---|---|
| - | Intro FMV | 1900 | (boot) | `1900:start` skips the rest of the FMV. |
| - | Title | 2100 | `1900:start` | "Press START button to continue". Only START works here. |
| - | Select Option (profile) | 2400 | `2300:start` | "Burnout 3 requires a Profile…"; LOAD PROFILE highlighted, NEW PROFILE below. |
| - | New Profile keyboard | 2700 | `2500:down`, `2600:cross` | Name pre-filled BURNOUT, cursor on Done. |
| - | Save profile? | 2900 | `2800:cross` | YES / NO, NO highlighted. |
| - | Autosave will be disabled, sure? | 3100 | `3000:cross` | YES / NO, NO highlighted. Cross on NO goes back to the save question. |
| 01 | Main menu | 3575 | `3150:up`, `3250:cross` | BURNOUT 3 WORLD TOUR highlighted; SINGLE EVENT, MULTIPLAYER, ONLINE, DRIVER DETAILS. Appears by frame 3375. |
| 02 | Main menu, Single Event | 3725 | `3600:down` | SINGLE EVENT highlighted; help line "…your own race or crash events". |
| 03 | Single Event / Select Mode | 3925 | `3750:cross` | RACE highlighted; TIME ATTACK, ROAD RAGE, CRASH. |
| 04 | Race / Select Options | 4125 | `3950:cross` | REGION highlighted, value Random; TRACK Random (greyed); RIVALS 5. Prompt "Continue ✕". |
| 05 | Race / Select Car | 4325 | `4150:cross` | COMPACT SERIES, a red car in front in a garage, "COMPACT TYPE 1", top speed and weight bars. Prompts Back, Color, Change, Select. |
| 06 | Race Training video | 4425-11900 | `4350:cross` | Tutorial video ("RULES OF THE ROAD", "NEAR MISS", "ONCOMING", "TAILGATING", "TAKEDOWN!"). Ends on the Burnout 3 logo at about 11900. |
| 07 | Loading | 11925-12350 | (none) | "HAVE YOU TRIED… CRASH AFTERTOUCH? Use aftertouch to steer your crash. Hit Crash pick ups!" over a still of a town street, LOADING at the bottom. |
| 08 | Race intro | 12375-12550 | (none) | Heading RACE, "Fight Through Traffic To Win The Race!" over a sky. |
| 09 | Race, first frame | 12575 | (none) | POS 4/6, LAP 1/3, 49 mph, boost bar; the camera looks at the sky. A rolling start, no countdown seen. |
| 10 | Race | 12700 | (none) | POS 3/6, 52 mph, "CRASHED!" banner, an EA Trax pop-up. |
| 11 | Race | 13200 | (none) | POS 3/6, 10 mph, a town street with a "SLOW" sign. |
| 12 | Race, end of probe | 14500 | (none) | POS 6/6, 0 mph, the car standing against a wall. |

The pages without a number are the profile steps from run 010
(`docs/re/boot-log-010.md`); numbers 01-12 match the screenshot file names
(`01-main-menu`, `02-main-menu-single-event`, `03-single-event-select-mode`,
`04-race-select-options`, `05-race-select-car`, `06-race-training-video`, `07-loading`,
`08-race-intro`, `09-race-first-frame`, `10-race-crashed`, `11-race-later`,
`12-race-end-of-probe`).

Notes on the choices:
- Every page takes its first item, except the main menu (Single Event, as asked) and the
  two profile questions (New Profile, and YES to going on without saving).
- With Region and Track left on Random the game picked a small-town street course. The
  course name is not shown on any captured frame.
- No press was eaten. Each page is up and settled 175 frames after its press (the frames
  looked at); 150 to 200 frames between presses was enough everywhere.
- The Race Training video plays here as well as in World Tour (run 010), so it belongs to
  a profile's first race, not to the mode.

## State of the race

The race loads and the game logic runs: position changes from 4th to 3rd to 6th of 6, the
speed reading moves, "CRASHED!" appears, music track pop-ups show. Nobody is steering or
accelerating, so the car hits something and stops.

The picture is wrong in these ways (all from the probe's frames 12575-14500):
- The player's car is not drawn. Only its shadow is visible on the road.
- Most world textures are garbage: the road and pavements show as white, pink, yellow or
  red noise, and building walls as coloured static. Some surfaces (sky, hills, the "SLOW"
  sign, lamp posts) look right.
- The first frames point at the sky before the camera settles.
- The HUD (position, lap, speed, boost bar) and the 2D banners are drawn correctly.

Nothing in the log points at a cause: the run ends at its frame limit with rc 0,
`[iop] 0 faults`, no misses file (no missing code) and only the old
`[iop] missing modload #18 called from 0x0006C1B4` line. The EE thread dump has the same
shape as run 010's (thread 1 running, `pc 0x001F24E1 ra 0x001F2518`), except that threads
3, 4 and 5 are waiting (status 4) instead of ready. New IOP text appears when the race
starts:
```
[iop-tty] IOP:B3DIST_CmdHandler 2/2 got command ID of 32848
[iop-tty] IOP:B3Route:Finished Initialisation and setup of cores with channel mask of [ff]..
[iop-tty] IOP:B3DIST_CmdHandler 1/2 got command ID of 32800
[iop-tty] IOP:B3Route:started distortion..
```
Totals at 14,500 frames: 49,785,005 primitives (5,077,243 at 12,000 frames in run 010's
longest probe, so nearly all of them come from the race), 71,274,535,001 EE cycles, GIF
path 1 `packed 3013605`, path 3 `packed 2060704 reglist 8546 image 1268346`, 638,931
texture uploads, 125,845 CLUT loads. VIF1 runs commands not seen before the race (codes
14, 15, 17, 30, 4A, 64, 65, 6A, 6C, 6D, 6E in the `[vif1]` summary), and DMA channel 0
(VIF0) starts 22,358 times, where it was 0 in every earlier run.

## Probes

| Probe | Frames | Presses after the main menu | Outcome |
|---|---|---|---|
| `a` | 8000 | `3600:down:6`, then cross at 3750, 3950, 4150, 4350, 4550, 4750 | Pages 01-06; the two extra crosses land in the video and do nothing. |
| `b` | 14500 | `3600:down:6`, cross at 3750, 3950, 4150, 4350 | Pages 01-12, the race. Took 9 minutes of wall time; the race frames are much slower to render on the software GPU than the menus. |

Logs and press lists are in `~/b3/out/run011/a/` and `~/b3/out/run011/b/`
(`probe.log`, `presses.txt`, `shots/`).
