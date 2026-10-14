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
- Still missing in the mixer: master-volume sweeps, noise and pitch modulation (the race trace has both of those registers at 0). The hardware's sample interpolation was added next; see the section below.
- Nobody has listened.

## Sample interpolation (added the same day, fork commit `e1ebb50`)

The mixer drew a straight line between two decoded samples. The SPU2, like the first PlayStation's SPU, weights four neighbouring samples with a 512-entry table indexed by the top eight bits of the fraction of the read position. The table is hardware data, published in PSX-SPX; PCSX2's own 256 by 4 table holds the same values in another arrangement (its first row is entries `0xFF`, `0x1FF`, `0x100` and `0` of the published table). The fork now uses the published table: taken from the document by script, 512 values, and the document's own check holds for it (the four weights of every index add up to `0x7F7F` to `0x7F81`).

Effect, as the table predicts: a steady sample comes out 0.03 dB lower, and the top of the spectrum is rounded off a little. In six seconds of driving the share of energy from 2 to 6 kHz goes from 5.9% to 5.5%, from 6 to 12 kHz from 0.8% to 0.6%, and above 12 kHz from 0.1% to under 0.05%; the level moves by 0.1 dB and clipping in the crash drops slightly (3.18% to 3.02% in its worst second). This changes the tone of every voice in every game.

A cold boot along the menu route to frame 14100 reaches the race with 0 IOP faults, and every section before the race is within 0.1 dB of the recording made before the change (EA logo -29.3 / -28.3 against -29.2 / -28.2 dBFS, intro -22.1 / -22.7 against -22.0 / -22.6, title -18.8 / -19.6 against -18.8 / -19.5, training video -21.6 / -21.6 against -21.6 / -21.5).

At that commit the four samples were taken from the read position onwards (the block after was already decoded) and not from the three before it, so a voice sounded two samples early. The next section removes that.

Tests (`tests/runtime/test_spu2_mixer.cpp`, now 14 tests and 67 checks): a steady sample keeps the level the table gives at every index, and a single sample comes out as the bell the table describes (its centre weight `0x59B3` and the weights one sample either side, `0x1307` and `0x12C7`), wider than a straight line would make it. The expected levels of the other tests moved by the 0.03 dB.

Seen while doing this: a voice whose data ends without a loop was silenced when its end block was decoded, a block before that block was played. Fixed in the next section.

## Sounds without a loop were cut short (fork commit `1072cf9`)

The mixer decoded one block ahead of the one being played and applied a block's end flag when it decoded it. For a sound that ends without a loop (end flag set, repeat flag clear) that meant: silence, and the ENDX bit, from the moment the block before the end block started to play. Such a sound lost its last two blocks, 56 samples; one of only two blocks made no sound at all (the new test showed that). On hardware a block's end flag acts when the block has been played to its last sample.

Now a block is decoded when the voice reaches it, into the second half of the voice's buffer, with the block before it kept in the first half. The interpolation takes the sample at the read position and the three before it, as the hardware does, so a voice starts from silence behind its first sample and is no longer two samples early. When the read position leaves a block, that block's end flag acts: ENDX, the jump to the loop address (read at that moment, not a block earlier), and for a sound without a loop the release and the cut to silence. The address IRQ for a block fires when the voice starts reading it, as before.

The voice record keeps its layout but its buffer and read position mean something else, so state files are version 6. Older files load: a playing voice in one of them goes on from the start of the block it had decoded ahead, a jump of at most one block once, at load.

What it changes in the game: in the 30 seconds of race from the pre-race state the level per second is the same to 0.1 dB as before the change, and a cold boot to frame 14100 has every section before the race within 0.1 dB, with the stream voices (title, menu and video sound, which loop over two buffers and depend on the block addresses and the address IRQ) playing as before and 0 IOP faults. The difference is at the ends of one-shot effects, a millisecond or two each, which these measurements do not resolve.

Tests (`tests/runtime/test_spu2_mixer.cpp`, now 18 tests and 84 checks): a voice starts from silence behind its first sample (the first four output samples); a sound of two blocks without a loop sounds for all of its samples, at full level to the last, with ENDX clear at 32 samples and set once it has ended; a two-block loop plays its blocks in turn and reports the block being read; a playing voice in a state marked as an older version keeps playing. Eight of the checks failed before the change. The function harness still runs on the version 4 snapshot corpora (1,166 and 312 cases, 0 fail), and a snapshot written by this build is version 6 and loads.

## The corrected race (after gap 012)

Everything above was measured on the race as it was before `docs/re/gap-012-vu-fmor-fsor-wrong-meaning.md`: the player's car was thrown off the map eight seconds in, so "the crash" in those recordings was that. The race from the same pre-race state with the fork at 76eb759 and all the mixer changes (`~/b3/out/audio02/race-corrected.wav`, recorded with the race02 snapshot corpus; 72.7 million primitives drawn against 54.9 million before, 0 IOP faults):

| Seconds after frame 12300 | What happens (frames kept every 150) | RMS dBFS | Samples at full scale |
|---|---|---|---|
| 1 to 5 | grid | -36 to -27 | 0 |
| 6 to 11 | driving off, 50 to 100 mph | -22 to -13.5 | 0 |
| 12 to 13 | | -9.4, -9.7 | 0.42%, 0.17% |
| 14 to 16 | a rival's crash shown in the takedown view | -6.7, -7.6, -8.6 | 5.7%, 2.1%, 1.9% |
| 17 to 25 | driving, trading paint | -12 to -15 | under 0.11% each second, 0 in most |
| 26 to 28 | an escape past a wreck | -8.1, -9.3, -11.8 | 2.2%, 0.45%, 0.21% |

Over the 24 seconds of race 0.56% of the samples are at full scale, all of it in the seconds where a crash is being shown. Plain driving does not clip and is quieter than in the broken race, where the engine was always near the limiter. So the open point of gap 011 stands on the corrected race: crash and takedown sequences drive core 1's voice sum past full scale. Whether a console does the same depends on the stream and effect volumes the game sets there, which nobody has checked against the game's own code yet.

## After the audit of 2026-10-13

Four things from the audit of that day (its findings 9, 10, 11 with 40, and 13). The code changes are in the fork's `runtime/src/iop/spu2.cpp` and a new header beside it, `spu2_work_area.hpp`, on branch `fixes01`. None changes the saved-state layout. Nobody has listened; a race has not been recorded with them yet.

### What is known about the crash clipping

Read from the traces that already existed (`~/b3/out/audio02/race-reverb.log` and `peaks.log`, both of the race before gap 012); nothing was run.

- **The song is not under the crash bed.** At the crash the game keys off core 1's voices 16 to 21 and keys on 18 to 21 (12.7 s after frame 12300). The song, voices 16 and 17, stays off for the whole bed and is keyed on again 1.6 s after the bed is keyed off. So the two stream volumes `0x3979` and `0x2E14` never sound together, and their ratio is not a mix balance; `docs/re/gap-011-spu2-mix-switches-and-input-area.md` said otherwise in two places and is corrected. The Xbox build does play the song under the bed, at 0.30 against 0.70 (reB3 `docs/RE_MUSIC.md` section 6.3). reB3's own default pauses the song there, as a user request and not as recovered behaviour (its section 6.5).
- **The bed is nearly at the limit by itself.** Voices 20 and 21 peak at 29,305 and 29,353 through a volume of 29,426 of 32,768: the source is at full scale and the bed leaves 0.96 dB.
- **24 effect voices are on top of it** in the same half second: 16 on core 1, peaking between 4,700 and 23,400 each, and core 0's dry voices 16 to 23, peaking between 4,800 and 20,400, which come in through core 0's output and core 1's external input at full volume. Both limits are reached: core 1's voice sum, and the final mix.
- **Still open: the level.** The bed is at 0.90 of full scale where the Xbox build has 0.70. That is 2.2 dB on the loudest source; at 0.70 the bed alone would peak near 22,900. Finding the PS2 twin of the function that sets the stream volumes is still the only check inside the project that `0x3979` is what the game means.
- **Still open: the corrected race.** It has a recording and no voice trace. That its takedown and near-wreck seconds (the table above) are the bed with the song off is inferred from the one crash traced. One run from the pre-race state with the voice-peak patch kept next to the recordings (`PS2_TRACE_VOICES`) would show it, and is cheaper than a console recording.

### The final mix wrapped at twice full scale

A core's output was `(voices + input + core 0 + reverb return) * master volume >> 15` in 32 bits, clamped afterwards. With the master volume the game uses (`0x3FFF`, which is 32,766 of 32,768) the product does not fit once the sum is beyond 65,540 either way, and the sample came out at the opposite limit: a click of the full range. The recordings have it: `race-fixed.wav` and `race-reverb.wav` at 24.19 s and `full-fixed.wav` at 229.15 s, one or two samples each, on the right channel inside a clipped stretch; `race-corrected.wav` has none. PCSX2 changed the same place because of this game (its pull request 10142, "SPU2: Fix applying master volume", whose stated reason is "Burnout 3 had samples overflowing 16bits").

Now the sum is clamped to 16 bits, the master volume is applied to that, and the result is clamped again. That is the order of PCSX2's SPU2 mixer since that pull request, and of the PlayStation SPU as Mednafen and DuckStation have it (each SPU2 core is built like that SPU); none of the three says it was measured on a console, and it has not been checked against one here. The voice mixes were already clamped before this, where the write-back areas take them.

What it changes: a mix beyond 16 bits now clips at full scale times the master volume. With `0x3FFF` that is +32,765 and -32,766 where it was +32,767 and -32,768, so every clipped sample moves by 1 or 2 towards zero and the few wrapped ones move to their own side; no other sample changes. **"Samples at full scale" in the tables above were counted as plus or minus 32,767 or beyond (`tools/audio-check.py`); a recording made after this change has none, and the count has to be of 32,765 and beyond to compare.** A game with its master volume turned down now clips at that lower level, not at full scale.

Tests (`tests/runtime/test_spu2_mixer.cpp`): three sources at full scale and of one sign come out on their own side, at levels like the crash's too, for both signs; and at half master volume a mix of twice full scale comes out at half scale, on core 0's written-back output as well. 12 checks, all of which failed before.

### Exponential decreases never reached zero

An envelope or volume sweep that falls exponentially takes a step of "step times level over `0x8000`". The mixer divided, which rounds towards zero, so below a level of 4,096 the step was 0: a voice released that way stayed in release for ever at up to 12.5% of full level, and a decay to the lowest sustain level never arrived. Now the step is shifted down 15 bits, which rounds down, so it is at least one unit and the level reaches 0. PSX-SPX writes the rule as a division and does not say how it rounds, but says in the same section that a release ends at 0 and a sweep decreases to 0, which only rounding down gives; PCSX2, Mednafen and DuckStation all shift.

It also makes every exponential fall a little faster all the way down, not only at the end (a release that steps once every 32 samples is at 0 after 7.4 s; before, it stopped at 4,095). Burnout 3 does not use the mode in any state sampled (16 saved states, menu and race: every release and sweep is linear), so nothing should change there. The one setting in those states that can show it is ADSR1 `0x000F`, seen only on idle voices: its single decay step is one unit larger.

Tests: an exponential release follows the rule step for step to 0 and the voice stops reading; a slow one too; a decay reaches sustain level `0x800`; an exponential sweep down reaches 0. 13 checks, 10 of which failed before (the other three check the tests' own set-up). `test_spu2_mixer.cpp` is now 25 tests and 109 checks.

### The reverb is cheaper, with the same output

The reverb worked out each of its 14 work-area addresses per sample with two 64-bit divisions, and its way out ran 20 filter taps over entries that are always 0. Now each offset is wrapped into the work area once per batch and an address is an addition and a compare (`spu2_work_area.hpp`), and the way out runs only the taps that have samples under them: the even ones on the side that just ran, the centre one on the other.

The output is bit for bit the same. `tests/runtime/test_spu2_reverb.cpp` passes as it was (6,400 samples and the work area against the transcription), and has four more tests, 15 tests and 43 checks now: a preset with offsets at and beyond the size of the area, a reflection destination at 0 and all-pass destinations below their sizes, in a work area of an odd size; a work area of two halfwords; a reverb that is not run for one and for three batches and then runs again; and the new address arithmetic against the old division formula, kept in the test, for every size of work area from 2 halfwords to the whole of sound RAM with offsets and sample clocks at the edges of the area and of 32 bits, and for small and random areas with every pair of offsets (92 million addresses in all, half a second).

Measured with the mixer alone (the audit's benchmark, 32-sample batches, nanoseconds per batch, best of 9 and the lowest of 3 runs, Ryzen 9 7950X, native; "before" is the fork at a854891, "after" has all three changes):

| Case | g++ -O2 before | after | clang++ -O2 before | after |
|---|---|---|---|---|
| silent, reverb on | 4,051 | 2,757 | 3,010 | 1,885 |
| 16 + 18 voices, reverb on | 5,442 | 4,157 | 4,456 | 3,394 |
| 8 + 4 voices, reverb on | 4,643 | 3,367 | 3,579 | 2,511 |
| 16 + 18 voices, no reverb | 2,608 | 2,642 | 2,057 | 2,113 |

About a quarter of the mixer with the reverb running, and a little under half of the reverb itself. On the page the mixer is 1.5% of the emulation thread, so this is a few tenths of a percent there (an estimate from the native ratio, not measured), and the frame rate does not change. The checksums of the output and of the work area are the same before and after in every case.
