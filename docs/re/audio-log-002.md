# Audio 002: the SPU2 reverb — 2026-10-13

Fork: branch `burnout3`, commit `40e47c7` on top of b9afd4a (gap 011). Work by Opus 1 in the fork's main checkout. Recordings and traces are under `~/b3/out/audio02/`; nothing from the game is in this repository. Nobody has listened yet: everything below is measurement.

**The mixer now runs the SPU2's reverb. In a race the game puts the engine through a hall reverb all the time, about 18 dB under the mix, and the runtime played none of it. Outside the race nothing is sent to the reverb in the route we run, and the output there is bit for bit what it was.**

## What the game does with the reverb

From the register trace (`PS2_TRACE_SPU2`, which now also logs the volume block):

- At boot the sound library sets both cores' work areas to the last eight halfwords of their blocks (`ESA` `0xEFFF8` and `0xFFFF8`, `EEA` `0xE` and `0xF`), which is "reverb off".
- When a race loads, the game writes a full preset to core 1: 22 work-area offsets, 10 gains, and `ESA` = `0xFA910`, a work area of `0x56F0` halfwords up to `0xFFFFF`. The values are the hall preset of the published PlayStation SPU documentation with every offset multiplied by four. It sets ATTR bit 7 (effects enabled) and then raises the effect volume `EVOL` from 0 to `0x101D` (0.126, -18 dB) in about 3 dB steps.
- Core 1's `MMIX` is `0x0FFC`: voices and the sound-data input go to the output and to the effects. The input is the engine after the distortion module (gap 011). So the engine has hall reverb for the whole race, not only in tunnels.
- Core 1's per-voice effect sends (`VMIXEL`/`VMIXER`) change during the race. reB3 lists reverb boundaries among the track's trigger volumes (trigger group `0x23`), which fits: some stretches of track send effect voices to the reverb as well. In the 20 seconds where the effect-send mix was dumped, no voice was sent.
- The same preset is written once in the first 700 frames of a cold boot and switched off again; nothing is sent to it there.

reB3 does not model the reverb: its mixer is harness code. What it adds is that the reverb is tied to places on the track.

## What was missing

The mixer formed the effects mix (since gap 011) but nothing came back from it. The ten gain registers in each core's volume block (`0x774` to `0x786`, and `0x79C` to `0x7AE`) were discarded on write.

## Fix

In the fork, `runtime/src/iop/spu2.cpp`:

- The reverb is the SPU's, as on the first PlayStation: it runs at half the sample rate, one side per output sample (left on even samples), through a 39-tap half-band filter on the way in and out. Each side has two reflection filters (one fed from its own side, one from the other), four comb taps and two all-pass filters, all reading and writing SPU2 RAM in the work area, whose origin moves on one halfword every two samples. The formula and the filter coefficients are the published ones (PSX-SPX); the PS2 register positions and the work-area arithmetic follow PCSX2's description. Nothing was copied from either.
- Its input is the core's effects mix: the voices with their effect-send switch set, the sound-data input and, on core 1, core 0's output, each by its `MMIX` effects bit. What comes back is scaled by `EVOL` and added to the dry mix before the master volume.
- The network always runs and is heard. It writes to the work area, and tests the address IRQ there, only while ATTR bit 7 is set. It is skipped when effects are disabled and `EVOL` is 0, and when the work area is empty.
- The gain registers are kept. After reset the work areas are the power-on ones.

Register positions: the offsets at `0x324` and `0x328` are the two "different side" sources. By position they are the left and right ones of the published formula, and each side's filter reads the other side's, as the formula says. The hall preset the game writes is only coherent that way (each source then sits 0x1000 and 0xFF8 halfwords under the other side's destination, next to the same-side delays of 0xFE8 and 0xFE0). PCSX2's register table swaps the two for core 1 only, which would undo the cross-feed there; that has not been checked against a console.

Saved states: the reverb's own state (gains, filter histories, the sample clock) is new, so state files are now version 5. Version 4 files still load, with the reverb cleared; a section can test `Stream::version` for fields old files lack. The snapshot corpora `menu01` and `race01` and the pre-race state are version 4 and keep working (spot runs of the function harness on both: 1,166 and 312 cases, 0 fail). A state saved before this change has no gains in it, so the reverb is silent after loading one until the game writes a preset again, which it does when a race loads.

## Verification

`tests/runtime/test_spu2_reverb.cpp` (new) drives the fork's `spu2.cpp` through its registers with a made-up preset and signal and compares the output with a slow, plain transcription of the formula written in the test: 6,400 output samples and the 1,024 halfwords of the work area agree exactly. 11 tests, 27 checks: also that nothing outside the work area is written, that with effects disabled nothing is written and what is in the work area still plays, the effect switches, the effect volume, the sum with the dry mix, the address IRQ, a state round trip and a version 4 state. 13 of the checks failed before the reverb existed. `spu2_fixture.hpp` holds what the two SPU2 tests share.

The race from the pre-race state, with and without the reverb (the two runs took the same number of cycles and are sample-aligned, so the difference between the recordings is what the reverb adds):

| Seconds after frame 12300 | Mix without reverb, RMS dBFS | What the reverb adds, RMS dBFS | Left/right correlation of the addition |
|---|---|---|---|
| 2 to 6 (grid) | -24.9 | -40.8 | 0.25 |
| 6 to 12 (driving) | -14.0 | -31.7 | 0.26 |
| 12 to 15 (crash) | -9.1 | -38.3 | 0.11 |
| 18 to 30 (driving) | -11.2 | -32.4 | 0.24 |

The reverb is 18 to 21 dB under the mix while driving, as `EVOL` says, and it is wide: the engine goes in as a mono signal and comes back with a left/right correlation of 0.25. The mix level rises by 0.1 dB. Clipping is unchanged.

A cold boot along the menu route to frame 14100: boot, EA logo, Criterion logo, intro FMV and the Race Training video are sample for sample identical to the recording made before the reverb (100.00% of samples in each section; 98.5% in the title section). The run reaches the race with 0 IOP faults. Wall time is unchanged (258 s against 247 s for 1,800 frames with the register trace on).

## Not done

- Not compared with a console or with PCSX2 on the same stretch. The formula is the documented one and the implementation matches a second transcription of it, which catches slips, not a misreading shared by both.
- No stretch of track that sends effect voices to the reverb has been recorded.
- The browser page is rebuilt with the reverb but has not been run.
- Still missing in the mixer: the hardware's four-point Gaussian interpolation (the mixer interpolates linearly), master-volume sweeps, noise and pitch modulation (the race trace has both of those registers at 0).
- Nobody has listened.
