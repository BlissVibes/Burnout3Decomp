# Gap 011: the SPU2 mixer ignored the mix switches, so the engine was played raw instead of through the game's distortion module

Date: 2026-10-12. Found by the audio check (`docs/re/audio-log-001.md`), fixed the same day by Opus 1 in the fork's main checkout (commit `b9afd4a` on 547315b). Recordings and traces are under `~/b3/out/audio01/` and `~/b3/out/audio02/`; nothing from the game is in this repository. Nobody has listened to the result yet: everything below is measurement.

## Symptom

In a race the mixer's output was 10 dB louder than anything before it and clipped: 1.0% of the samples at full scale in six seconds of plain driving (frames 12660 to 13020), left and right uncorrelated (-0.05), and a constant offset of -9 dBFS on the left channel.

## Cause

`B3ROUTE.IRX`, the game's own IOP module ("B3 distortion router"), takes the engine sound out of the SPU2, distorts it on the IOP and puts it back. It uses four pieces of SPU2 behaviour that the runtime's mixer did not have.

What the module does, from the register and DMA trace of a race (`PS2_TRACE_SPU2`):

- At race start it clears the dry switches of core 0's voices 0 to 7 (`VMIXL`/`VMIXR` = `0x00FFFF00`) and sets their effect-send switches (`VMIXEL`/`VMIXER` = `0x0000FFFF`). Those eight voices are the engine: reB3's notes on the car sound banks say each car has four `eng_<rpm>` loops and four `ex_<rpm>` (exhaust) loops, and the trace shows two groups of four looping voices, one group hard left and one hard right, whose pitches move with the revs.
- It points core 1's IRQ address at `0x1C00`, `0x1C80`, `0x1D00` and `0x1D80` in turn. These are in core 1's effect-send write-back area, so it gets an interrupt every 128 samples, four per turn of the 512-sample output ring.
- On the interrupts at output positions `0x000` and `0x100` it reads 768 halfwords by DMA from `0x1500` and from `0x1400`: the half of core 0's effect-send mix that has just been written, left (the first 256 halfwords of the block) and right (the last 256).
- On the interrupts at `0x080` and `0x180` it writes 1,024 halfwords by DMA to `0x2400`: the whole of core 1's sound-data input area, left ring and right ring, with the newly processed half in the half that is not being played.
- Core 1 plays its input area into the output (`MMIX` = `0x0FFC`: voices, input and core 0, dry). Core 0's `MMIX` is `0x0FF0`.

So on a console the eight engine voices are never heard directly. Their sum goes to the effect-send write-back area, the module reads it, and its distorted version comes out of core 1's input 384 samples (8 ms) later.

The mixer (`runtime/src/iop/spu2.cpp`) departed from the hardware in five places:

1. It ignored the per-voice switches. Every voice went to the dry mix, so the engine voices were heard raw.
2. It wrote silence to the effect-send write-back areas, so the module read silence.
3. It played a core's input only from a private queue filled by auto-DMA transfers. What is written into the input area of SPU2 RAM was never played, so the module's output went nowhere.
4. It ignored `MMIX` and added core 0's output straight to the final output instead of passing it into core 1 through core 1's external-input volume.
5. It stepped a volume in sweep mode only while its voice was playing. RWA uses sweeps as ramps: it writes a sweep, polls the current volume and writes the fixed target when the ramp arrives. On a stopped voice the ramp never moved (one second of polling at the start of the race before RWA gave up).

## What the Xbox reimplementation says

reB3 has no counterpart of the distortion router: the IOP modules do not exist on the Xbox side, and reB3's own mixer is marked as harness code, not recovered. What it does give is the game's intent around it:

- The engine is four `eng_` and four `ex_` loops per car chosen by rev band (`docs/AUDIO_NOTES.md` section 2.1 there). That identifies the eight voices.
- The game's mix state machine solves fifteen group volumes that all sit at 1.0, and a crash raises one group to 1.1 and ducks nothing (`docs/RE_SFX.md` section 6.1). So a crash is meant to be louder than driving, not quieter.
- In a race the crash bed stream plays at 0.70 against the song's 0.30 (`docs/RE_MUSIC.md` section 6.3, confirmed there by emulation of the Xbox function).

The last point does not match what the PS2 build writes to the SPU2 in a crash: the crash bed voices (core 1, voices 20 and 21) get volume `0x3979` and the song's voices (16 and 17) keep `0x2E14`, which are 0.90 and 0.72 of full scale, a ratio of 1.25 where the Xbox figures give 2.33. Either RWA maps gain to the register through a curve, or the PS2 build has other constants. Not resolved; see the open points.

## Fix

In the fork, `runtime/src/iop/spu2.cpp`:

- Each voice is rendered to its own left and right samples and added to up to four sums by its switches: dry left, dry right, effects left, effects right. The four sums are clamped to 16 bits and written to the write-back areas (`0x1000`/`0x1200`/`0x1400`/`0x1600` for core 0, `0x1800`/`0x1A00`/`0x1C00`/`0x1E00` for core 1).
- A core's input is read from its input area (`0x2000` for core 0, `0x2400` for core 1, right channel `0x200` higher) at the output position, scaled by the input volume, whoever wrote it. The read tests the address IRQ. Auto-DMA now writes each block (256 left samples, then 256 right) into the half of that ring that is not being played, one block per half; the private queue is gone.
- `MMIX` gates voices, input and external input into each core's output, per side. Core 0's output after its master volume is core 1's external input, scaled by core 1's `AVOL`.
- Volume sweeps step every sample for every voice.
- After reset the switch registers read all-on (`MMIX` = `0xFFCF`), so software that never writes them sounds as before.
- `PS2_TRACE_SPU2` also logs every DMA transfer with its address, size and the output position.

The saved-state layout is unchanged: the switches were already kept in the register array, and the queue's fields stay in the record unused. States saved before the fix load (the race recordings below start from one).

Reverb was still not modelled at this commit: the effects mix is formed and written back, but nothing returned from it into the output. It was added next; see `docs/re/audio-log-002.md`, which also corrects what this note first said here: the voices' effect-send mix on core 1 was silent in the 20 seconds measured, but core 1's `MMIX` sends its input, the distorted engine, to the reverb all through the race.

## Verification

`tests/runtime/test_spu2_mixer.cpp` (new; run with `tests/runtime/run.sh <fork>`) builds the fork's `spu2.cpp` alone and drives it through its registers: 12 tests, 59 checks. Before the fix 36 checks failed; all pass after it. The auto-DMA test passed before and after: a four-block stream comes out once, in order, without a gap, and the transfer's completion is signalled.

The race, recorded from the pre-race state with accelerate held (`~/b3/out/audio02/`), six seconds of plain driving, frames 12660 to 13020:

| | RMS dBFS L / R | Samples at full scale | Left/right correlation |
|---|---|---|---|
| before | -9.0 / -8.6 | 1.00% | -0.05 |
| after | -13.8 / -14.2 | 0% | 0.90 |

The parts of the mix in the same six seconds, from a temporary dump (kept as a patch next to the recordings, not in the fork):

| Part | RMS dBFS L / R | Peak dBFS | Note |
|---|---|---|---|
| core 0 effect send (the eight engine voices) | -9.8 / -9.3 | 0.0 | 0.66% of its samples reach the 16-bit limit of the write-back |
| core 1 input (the module's output) | -16.1 / -16.1 | -4.1 | mono (correlation 1.000); no step at the half or quarter seams of the ring |
| core 1 dry voices | -17.9 / -19.0 | -3.1 | song, effects |
| core 0 output | -28.4 / -29.8 | -6.6 | |

A cold boot along the menu route to frame 14100 with the fixed mixer (`full-fixed.wav`), against the recording of `docs/re/audio-log-001.md`:

| Frames | What is on screen | RMS dBFS L / R before | after | Full scale before | after |
|---|---|---|---|---|---|
| 450-620 | EA logo | -29.2 / -28.2 | -29.2 / -28.2 | 0 | 0 |
| 1220-1900 | intro FMV | -22.1 / -22.6 | -22.0 / -22.6 | 0 | 0 |
| 1900-3300 | title | -18.8 / -19.6 | -18.8 / -19.5 | 0 | 0 |
| 4425-11900 | Race Training video | -21.6 / -21.5 | -21.6 / -21.5 | 0 | 0 |
| 12375-12550 | race intro | -17.1 / -22.3 | -26.3 / -24.6 | 0 | 0 |
| 12575-13050 | race, driving | -9.5 / -9.6 | -12.5 / -12.6 | 0.81% | 0.002% |
| 13050-13250 | race, first crash | -9.1 / -9.7 | -10.0 / -9.8 | 1.73% | 1.17% |
| 13250-14100 | race, after the crash | -9.1 / -9.3 | -11.5 / -11.2 | 0.79% | 0.22% |

Nothing before the race moved. The race intro (the car revving on the grid) lost its 5 dB lean to the left that the audio log listed as something for ears to judge: most likely it was the raw engine voices, which are routed away from the dry mix by then; not checked voice by voice. The run reaches the race with 0 IOP faults, as before.

The main menu (frames 3300 to 4400) is left out of the table on purpose. Its level differs by up to 14 dB between runs of the same code (five recordings, two of the old mixer and three of the new: -17, -22, -22, -27 and -32 dBFS in the same 50 frames, and the two at -22 are one old and one new). The envelope of the sound does not match between runs either, while the title music before it matches to 20 ms. The menu plays a different piece from run to run; the -28.8 dBFS in the audio log's table was one of them. Why two runs with the same presses differ is the run-to-run variation already noted for the primitive counts, not investigated.

The module ran its cycle throughout: in 500 frames 685 reads at each half and 1,369 writes, one per 256 samples once it had started, with no IOP fault.

## Browser

Checked by Opus 2 on 2026-10-13 in Chrome, by the method of the Browser section of `docs/re/audio-log-001.md`: the page's own audio worklet replaced over the debugging port by a copy that also records what it outputs, a faked standard gamepad, output muted, a separate browser profile. Two pages were run one after the other on the same machine: the page built at 23:31 on 2026-10-12 from the fork's main checkout with the mixer change (`ps2web.wasm` 24,681,285 bytes; built before the change was committed, so it is the change as reviewed, not a build of the commit), and a baseline built from fork 547315b without it (`ps2web.wasm` 24,680,356 bytes, the size of the page measured in `audio-log-001.md`). Captures, logs and scripts are under `~/b3/out/audio02/web/` (`fixed/` and `base/`).

**The browser shows the same change as the native build. Plain driving no longer clips and the engine is no longer split hard left and right. Speed, underruns and the audio delay did not move. The title, menu and training-video audio is unchanged.**

Both pages booted, reached the race and ran to frame 14100 with no JavaScript error and the status line at "Running".

The race, browser against native, same frame windows (native after: `race-fixed.wav`, from the pre-race state; native before: `~/b3/out/audio01/full.wav`):

| Frames | Build | Where | RMS dBFS L / R | Samples at full scale | Left/right correlation |
|---|---|---|---|---|---|
| 12660 to 13020, driving | before | native | -8.9 / -8.6 | 1.02% | -0.05 |
| | before | browser | -8.3 / -8.1 | 1.50% | 0.11 |
| | after | native | -13.8 / -14.2 | 0 | 0.90 |
| | after | browser | -14.1 / -14.1 | 29 samples | 0.99 |
| 13020 to 13200, first crash | before | browser | -7.8 / -8.6 | 2.36% | 0.63 |
| | after | native | -9.1 / -9.1 | 1.32% | 0.88 |
| | after | browser | -9.4 / -9.1 | 1.43% | 0.89 |
| 13200 to 14100 | before | browser | -9.0 / -9.1 | 0.86% | 0.22 |
| | after | native | -12.2 / -11.7 | 0.21% | 0.82 |
| | after | browser | -13.2 / -12.6 | 0.18% | 0.92 |

The browser and native figures are from different runs of a race that is not repeatable (other traffic, other song), so they agree to about 1 dB and not sample for sample; the cold-boot native recording (`full-fixed.wav`) gives -11.6 / -11.7 dBFS and 12 samples at full scale for the same driving window. The crash still clips in the browser as it does natively (the first open point below). The constant offset on the right channel that the old mix had while driving (a mean of -108 in the browser, -86 natively) is gone (-28).

Speed and underruns, both pages measured the same way in the same session:

| | before | after |
|---|---|---|
| Frame rate in the race (frames 12650 to 14100), mean / lowest sample | 59.5 / 55.2 | 59.6 / 55.2 |
| Emulation thread idle, driving 12650 to 13050 | 12.6% | 12.1% |
| Emulation thread idle, crash 13050 to 13400 | 28.2% | 27.7% |
| Emulation thread idle, driving 13400 to 14100 | 12.6% | 11.9% |
| VU thread idle in the race | 37.3% | 36.3% |
| Samples output with the queue empty | 8,320 (173 ms) in 105 quanta | 7,296 (152 ms) in 89 quanta |
| Where | frames 12723 to 12884 | frames 12746 to 12886 |
| Queue depth in the race, mean | 63.4 ms | 63.9 ms |
| Samples written per frame | 800 | 800 |

The mixer's extra work costs about half a point of the emulation thread, which is inside the spread between runs (the idle figure is sampled about every three seconds, 23 samples per race). The underruns are where they were, in the first three seconds of driving, when the emulation thread is at 0% idle; the 113 ms measured in `audio-log-001.md` and the 173 and 152 ms here are three runs of the same thing. These two runs also took a screenshot at frames 12700, 13000 and 14000, which the earlier one did not. Outside that stretch nothing is lost in either page. The audio context reports the same latency as before (10 ms base, 40 ms output) and the queue sits at the same 80 ms, so sound still leaves about 130 ms after the emulation produces it, about 100 ms behind the picture.

Outside the race nothing changed. The two training-video pieces captured from the page with the change (frames 4500 to 5700 and 10700 to 11900, 953,696 and 952,160 stereo samples) are bit for bit the native recording made before the change, and equally the native recording made after it, largest difference 0. The title and menu piece (frames 2000 to 3700) is 68% bit-identical to the old native recording and 75% to the new one with one fixed shift; the baseline page gives 70% and 69%. The rest differs for the reason given in `audio-log-001.md` (menu sounds and the music start fall at slightly different moments in each run, and the menu song is not the same in every run), not because of the mixer. No underrun before the race in either page.

reB3 has nothing on the browser side of this: its mixer is harness code and there are no IOP modules on Xbox (see "What the Xbox reimplementation says" above).

Nobody has listened to the browser either; the output was muted and everything here is measurement.

## Open points

- **The crash still clips, and that may be right.** In the three seconds of the first crash (frames 13020 to 13200) 3.2%, 0.4% and 0.3% of the samples are at full scale. All of it is in core 1's own dry voice sum: the crash bed stream at volume `0x3979` peaks at 29,300 by itself, on top of the song at `0x2E14` and the impact effects. The SPU2 clamps that sum as the mixer does, so a console given the same registers clips the same way. Whether the registers are right is the question above: the PS2 twin of the function that sets the stream volumes (reB3's `FUN_0014B3D0` and `FUN_00150E80`) should be found and run in the harness.
- **Reverb.** Core 1 has effects enabled in the race with `EVOL` = `0x101D`, and reB3 lists reverb boundaries among the track's trigger volumes, so some stretches of track do send voices to the reverb. Modelled since; see `docs/re/audio-log-002.md`.
- **The engine send clamps.** The eight engine voices sum past 16 bits 0.66% of the time before the module reads them. The hardware write-back is 16 bits too, and the module is a distortion, so this is probably as intended; not confirmed.
- The master volume in sweep mode is still read as full volume, and noise and pitch modulation are not implemented (the race trace shows both registers at 0).
- Nobody has listened.
