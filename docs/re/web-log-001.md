# Web run 001: the race in Chrome — 2026-10-12

Fork: branch `burnout3` at 82584e5 (gaps 008, 009 and 010 in). Two builds of the page were run: the main checkout as it is (`tools/build-web.sh`, output `~/b3/out/web`), and the worktree `~/b3/ps2-gs008` with one uncommitted change to the WebAssembly VU JIT (built with the same cmake options into a folder of its own, deleted afterwards). Run by Opus 2. Screenshots and logs are under `~/b3/out/gap008/web/`; nothing from the game is in this repository.

**The browser reaches the race and holds 60 frames per second through all of it. Textures, cars and traffic are right in both builds. The car's reflection is wrong with the main checkout and right with the JIT change.**

## How it was run

- `python3 scripts/serve.py <build folder> <build folder>/boot.txt` from the fork, so the server holds no disc; page at `http://localhost:8620/`.
- Chrome with a fresh `--user-data-dir` profile and `--remote-debugging-port`, a 1100 by 800 window, the three flags that stop background throttling, driven over the debugging port. The disc image was handed to the page's file input (2.67 GB copied into the browser's storage), the page reloaded, Start clicked.
- Input: a standard-mapping gamepad faked through `navigator.getGamepads()`, pressed by frame number (`Module._ps2web_frames()`), with the schedule of `docs/re/menu-route.md` and each press held 12 to 20 frames; cross held from frame 12600.
- Chrome, the server and the profile folder were closed and removed afterwards.

## Result

Both runs took the same route at the same frames as the native runs: title, New Profile, main menu, Single Event, Race, car select at 4300, the Race Training video, loading at 12450, race from about 12575.

| Frames | Main checkout, fps | Worktree with the JIT change, fps |
|---|---|---|
| 1000-4300 (boot, title, menus) | 59.7 to 59.9 | 59.6 to 60.2 |
| 4300-12450 (training video) | 60.0 | 60.0 |
| 12450-12700 (loading into the race) | 57.2 | 57.5 |
| 12760-13000 (race) | 60.7 | 59.2 |
| 13000-13300 (crash sequence) | 60.3 | 60.7 |
| 13300-13660 (race) | 58.7 | 58.4 |
| 13660-14000 (race) | 63.5 | 63.6 |

The averages are frames counted between two screenshots, so short spans are noisy; the page's own counter read 58 to 61 at every sample. No JavaScript error in either run. Heap 256 MB up to the race, 307 MB in it.

The page's idle figures show where the time goes. In the menus the emulation thread is idle 51 to 59 percent of the time and the VU and GS threads 96 to 99 percent. In the race the emulation thread is idle only 4 to 16 percent at the busy samples (frames 12740, 12980, 13650), the VU thread 24 to 41 percent and the GS thread 56 to 61 percent, with the GPU process at 71 to 87 percent. The race runs at full speed on this machine with little room to spare on the emulation thread.

What the pictures show:

- Main menu (frame 3500), car select (4300): correct in both builds. On the car-select page the main checkout draws yellow and green streaks on the bonnet, windscreen and side of the large car; the JIT build does not.
- Race (12760, 13000, 13660, 14000): road, verges, fences, buildings, billboards, HUD, rival cars and the "1st" marker are right in both builds, so the gap 008 change and the gap 009 change to the JIT's MAX/MINI work in the browser.
- Reflection: with the main checkout the player's roof and rear window carry blocks of yellow, orange and grey, the fault of gap 010; the browser's JIT compiles the VU1 programs and still delivered Q and P at once. With the JIT change the roof and window show the sky reflection, as in the native build.

## The JIT change (fork worktree, `runtime/src/vu_jit.cpp`, uncommitted)

Leaving the affected programs to the interpreter was tried first and rejected: of the 61 VU1 programs recorded in fifty race frames, 27 read Q or P inside a latency window, among them the main world and car programs (they start the divide for the next vertex and use the Q of the current one), so nearly all vertex work would have been interpreted.

The JIT now models the delay itself. When a program can read Q or P inside a window, the generated code keeps a produced result in a local with a countdown, restores the visible value, and delivers the result when the countdown ends, on WAITQ or WAITP, when another operation of the same kind starts, and wherever the generated code returns or hands over to the interpreter. The countdown is stepped only at instructions a pending result can reach. Programs that never read inside a window are generated as before.

- All 61 recorded programs translate, and every module validates (`new WebAssembly.Module` in node with tail calls enabled).
- Speed: the frame rate is unchanged (table above). At the same race samples the VU thread's idle share was 24, 28 and 37 percent with the change against 25, 29 and 41 percent without, so the cost is between one and four points of that thread.
- Not changed: `vurecomp`, the ahead-of-time VU recompiler, still writes Q and P at once. No program is precompiled for this game, natively or in the browser.

## Differences between native and browser

None found in the picture once the JIT change is in. Not compared: audio (the page reports 56 to 93 ms queued throughout, and nobody listened), and input from a real controller.

## Still open

- The six race oddities listed in `docs/re/gap-010-vu-q-and-p-result-latency.md` are the same in the browser (the "CRASHED!" banner at the start is on screen at 12760 here too).
- Headroom: the emulation thread is nearly saturated in the race on this machine; a slower machine will drop frames there first.
