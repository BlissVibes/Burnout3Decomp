# Audio check 001 — 2026-10-12

Fork: branch `burnout3` at 547315b. Two halves: what the SPU2 mixer produces in the native build (Opus 1) and what the browser page plays (Opus 2). Recordings, captures and analysis output are under `~/b3/out/audio01/`; nothing from the game is in this repository. Nobody has listened to any of it yet: everything below is measurement, and the last section says what a listener should check.

## Native

Run by Opus 1. `PS2_DUMP_AUDIO=<file.wav>` records everything the SPU2 mixer produces (48 kHz stereo). Two recordings with the press schedule of `docs/re/menu-route.md`: `menu.wav` (cold boot, 3,700 frames) and `full.wav` (cold boot to frame 14,100, accelerate held from 12600; 481 s of wall time). Measured with `tools/audio-check.py` (new): level, silence, clipping, left/right correlation, energy by band, a spectrogram. Per-voice figures come from a temporary trace in the mixer, kept as a patch under `~/b3/out/audio01/` and not in the fork.

**Up to the race the mixer's output shows nothing wrong that numbers can show. In the race the engine sound is wrong: the game sends its eight engine voices to the effects bus for its own distortion module, and the runtime plays them straight to the output instead, at a level that clips. This is an emulation fault in the SPU2 mixer, the same in the browser.**

### What the recording contains

`full.wav` is 11,276,704 sample frames, 234.93 s: 799.77 per video frame, so audio and video advance together at 60 frames of video per second of audio.

| Frames | What is on screen | RMS dBFS L / R | Peak dBFS | Full-scale samples | Silent |
|---|---|---|---|---|---|
| 0-450 | boot, LOADING | none | none | 0 | all |
| 450-620 | EA logo | -29.2 / -28.2 | -11 | 0 | 0% |
| 620-1220 | Criterion logo, black | none | none | 0 | all |
| 1220-1900 | intro FMV | -22.1 / -22.6 | -8.9 | 0 | 0% |
| 1900-3300 | title | -18.7 / -19.5 | -6.8 | 0 | 0% |
| 3300-4400 | main menu to car select | -28.8 / -30.7 | -8.5 | 0 | 1% |
| 4425-11900 | Race Training video | -21.6 / -21.5 | -9.4 | 0 | 1% |
| 11925-12350 | loading | none | none | 0 | all |
| 12375-12550 | race intro | -17.0 / -22.3 | -4.6 | 0 | 0% |
| 12575-14100 | race | -9.2 / -9.4 | 0.0 | 0.92% | 0% |

Things in this table that only ears or a console can judge: the Criterion logo is silent; the title has 69% of its energy below 100 Hz; the race intro is 5 dB louder on the left.

### The race: engine voices bypass the game's distortion path

In the race 22,381 samples are at full scale (20,520 sample frames with at least one channel clipped, 0.92% of the samples) and the level is 10 dB above everything before it. The per-voice trace and the SPU2 register writes show why.

- When the race starts the IOP prints `IOP:B3Route:Finished Initialisation and setup of cores with channel mask of [ff]..` and `IOP:B3Route:started distortion..`. `B3ROUTE.IRX` is the game's own module ("B3 distortion router" in its start-up line).
- It then writes core 0's voice mix switches: the dry switches (registers `0x188`/`0x18A` and `0x190`/`0x192`) to `0xFF00`/`0x00FF`, voices 8 to 23 only, and the effect-send switches (`0x18C`/`0x18E`, `0x194`/`0x196`) with voices 0 to 7 set. Voices 0 to 7 of core 0 are the engine: eight looping voices in two groups of four, hard left and hard right, pitches moving together with the revs, each with a source level of -7 to -9 dBFS and volumes up to `0x3F49` (of `0x3FFF`).
- It points core 1's IRQ address at `0x1C00`, `0x1C80`, `0x1D00`, `0x1D80` in turn (core 1's effect-send write-back area), which gives it an interrupt every 128 samples. On those interrupts it sets core 0's transfer address to `0x1400` or `0x1500` and reads by DMA (core 0's effect-send write-back area, left), then sets the transfer address to `0x2400` and writes by DMA: `0x2400` is core 1's sound-data input area. Counts in 400 frames: 528 and 529 reads at the two halves, 1,057 writes.

So on a console the engine voices are not heard directly. Their sum appears in the effect-send write-back area, the module reads it, distorts it on the IOP and writes the result into core 1's input area, and core 1 plays its input area into the output.

The runtime's mixer (`runtime/src/iop/spu2.cpp`) departs from that in four places:
1. It ignores the voice mix switches: every voice goes to the dry mix. The engine voices are heard raw, on top of everything else.
2. It writes silence to the effect-send write-back areas ("reverb is not modelled"), so the module reads silence.
3. It plays a core's input only from a queue filled by auto-DMA transfers; what is written into the input area of SPU2 RAM is never played, so the module's output goes nowhere.
4. It ignores the master mix switches (`0x198`) and core 1's external-input volume.

A fifth departure showed in the same trace. Volume registers in sweep mode (bit 15 set, which RWA uses on most voices: values such as `0xA044` and `0x803A`) are only stepped while the voice is playing. For one second at the start of the race RWA writes `0xA020` (sweep down) to a stopped voice every 512 samples and reads the current volume 78 times in between, and the level stays at 32766 until it gives up and writes 0. On hardware the sweep runs whatever the voice is doing.

PCSX2's mixer (`pcsx2/SPU2/Mixer.cpp` and `ReadInput.cpp`, read today, knowledge only) does all five the hardware way: per-voice dry and wet gates, the dry and wet voice mixes written at `0x1000`-`0x1E00`, the input always read from `0x2000 + core * 0x400` at the output position, the gates of register `0x198`, core 0's output into core 1 through the external-input volume, and the volume slide updated before the test of the envelope phase.

Not established: how much quieter the race would be with the routing right (the distorted signal's level is the module's), and whether the remaining mix would still clip.

Fixed later the same day as gap 011: `docs/re/gap-011-spu2-mix-switches-and-input-area.md`. With the routing right, plain driving is 5 dB quieter and does not clip; a crash still does, in core 1's own voice sum.

### Before the race

- The intro, title and menu music and the training video's sound are two stream voices (core 1, voices 18/19 or 16/17, hard left and right, pitch `0x0AAA`, 32 kHz) looping over two 16 KB buffers that RWA refills. All blocks carry the loop flag; no end-of-data or stale-buffer symptom was seen.
- A first pass of the analyser reported about 200 "clicks" and many short runs of identical samples in the title music. They are not defects. 7% of the stream's ADPCM blocks use no prediction with a shift of 0 to 3, which gives steps of 1,024 to 4,096 before volume; at 32 kHz played at 48 kHz, equal neighbouring samples become flat runs of three to five output samples. The data in those blocks is varied and each has its own header. The detector was tightened to isolated steps and still cannot tell coarse ADPCM from a glitch, so its click count means little for this game.
- No clipping, no dropout inside a piece of sound, no jump at the 32-sample batch seams (ratio 1.00).

### What this cannot tell

That the sounds are the right sounds. The stream decoder was not checked against a second decoder, reverb is not modelled, and nothing here compares with a console.

## Browser

Run by Opus 2. Page: `~/b3/out/web` built from 547315b, served with `scripts/serve.py` and `boot.txt`, Chrome with a fresh `--user-data-dir` profile and `--mute-audio`, driven over the debugging port with the press schedule of `docs/re/menu-route.md` (cross held from frame 12600), one run from Start to frame 14101, 235.6 seconds.

**The page plays exactly what the mixer produces. In the title, menus and training video the captured output is the native recording bit for bit, with no gap; audio and picture stay in step. The only losses are in the first three seconds of the race, where the emulation thread cannot keep the queue filled: 113 ms of audio in 72 short dropouts. Audio reaches the listener about 130 ms after the emulation produces it.**

### How it was captured

The page file was not changed. Before Start was clicked, `startAudio` was replaced over the debugging port by a copy whose AudioWorklet processor is the page's own `Output` class with a tap added: it records every sample it writes to its output, counts the samples it had to make up because the ring was empty (the page's worklet then decays its last value by 0.995 per sample), and notes the ring depth at each render quantum. The tap posts half-second blocks to the page, and the driver reads the frame counter, `performance.now()`, `AudioContext.currentTime` and the ring's write and read positions every 50 ms. Files, under `~/b3/out/audio01/web/`: `web-menu-f2000-3700.wav`, `web-video-start-f4500-5700.wav`, `web-video-end-f10700-11900.wav`, `web-race-f12575-14100.wav` (48 kHz, 16-bit stereo), `timeline.csv`, `chunks.csv`, `context.json`, and the three scripts (`audio.mjs`, `audio_stats.py`, `audio_compare.py`).

### 1. Does the page play what the mixer produces, without gaps?

- AudioContext: `sampleRate` 48000 (what the page asks for; no resampling in the page), `baseLatency` 0.010 s, `outputLatency` 0.040 s, stereo, render quantum 128, state `running` throughout. The audio clock ran at 0.999972 of the wall clock over the run.
- Underruns. 36,992 made-up samples in all, in 8 of the 472 half-second blocks:
  - 31,584 in the first 0.66 s after Start, before the emulation has produced anything. Expected.
  - 5,408 (113 ms) in 72 render quanta between frames 12774 and about 12930, the first three seconds of the race, in six consecutive blocks (1472, 960, 640, 704, 1312 and 320 samples). The ring was empty at some point in each of those blocks.
  - None anywhere else: boot, intro video, title, menus, the two-minute training video, loading, race intro, and the race from frame 12930 to 14101.
- Queue depth (the emulation thread sleeps above 80 ms):

  | Window (frames) | fps | written per s | read per s | written per frame | queue min / mean / max, ms | made-up samples |
  |---|---|---|---|---|---|---|
  | boot to title (300-2000) | 59.98 | 48,008 | 48,006 | 800.4 | 52 / 79 / 97 | 0 |
  | title and menu (2000-3700) | 60.01 | 47,985 | 47,999 | 799.7 | 55 / 78 / 97 | 0 |
  | menus to video (3700-4425) | 60.02 | 48,010 | 47,989 | 800.0 | 34 / 79 / 97 | 0 |
  | training video (4425-11900) | 59.99 | 47,991 | 47,998 | 800.0 | 54 / 79 / 97 | 0 |
  | loading (11925-12350) | 59.95 | 47,960 | 47,928 | 800.0 | 63 / 82 / 96 | 0 |
  | race (12575-14100) | 59.70 | 47,774 | 47,770 | 800.2 | 0 / 61 / 96 | 5,408 |

  The rates are measured against the wall clock between 50 ms polls, so the last digits are noise; the worklet consumes 48,000 per second by construction.
- The emulation produces 800 samples per frame, so the game runs at 60.00 frames per second of audio time, not the NTSC 59.94 (the native recording has the same ratio: 14,095.9 frames of audio for 14,100 frames). That is the runtime's timing, not the page's.
- The ring holds 16,384 frames (341 ms) and `push_audio` drops samples when it is full. It never got above 97 ms, so nothing was dropped.

### 2. Is audio in step with the picture?

The emulation thread is paced by the audio queue, so picture and sound cannot drift apart: both come out of the same emulated time, and the only slack is the queue. Over the training video (7,470 frames, 124.5 s) the queue's median depth was 77.7 ms at the start and 77.3 ms at the end, there was no underrun and no resynchronisation, and the two captured pieces of the video, 104 seconds apart, line up with the native recording at offsets that differ by 128 samples (2.7 ms, one render quantum, the resolution of the frame-to-sample mapping used).

There is a constant offset. A sample waits about 78 ms in the ring and then `baseLatency + outputLatency` = 50 ms in the browser and the device, so sound is heard roughly 130 ms after the emulation makes it, while a frame is shown one or two display refreshes after it is made. Sound therefore trails the picture by something like 100 ms. That is at the edge of what people notice on speech; whether it is noticeable in the training video is a listening question.

### 3. Does anything in the browser path distort it?

- Resampling: none in the page; the context runs at the mixer's 48,000.
- Channel order and scale: the captured video pieces are bit-identical to the native recording over their whole length (952,608 and 951,072 stereo samples, largest difference 0), and identical with the channels swapped for under 1 percent of samples. So left is left, right is right, and the `/ 32768` conversion loses nothing at 16 bits.
- The skip logic (`web.cpp`: a frame is not presented when under 40 ms is queued) changes the picture, not the sound. It can only have acted in the first seconds of the race, where the queue ran low.
- The underrun handling is the one thing that alters the signal: when the ring is empty the worklet fades its last value out (to a third in about 4 ms) and jumps back when samples return, which will be heard as a short crackle rather than a clean gap.
- `--mute-audio` was on so that the test made no noise; it mutes after the context and should not change these numbers, but the run that matters is Mark's with sound on.

### 4. Browser against native, same frame windows

Statistics from `audio_compare.py` (RMS level per channel, peak, samples at full scale, digital silence, jumps of more than 12,000 between consecutive samples, left/right correlation, share of energy in five bands). Opus 1's analyser figures for the native files are in the Native section; these are mine for both sides so that the two columns are computed the same way.

| Window | | RMS L / R, dBFS | Peak | At full scale | Jumps | L/R r | Bands 0-200 / -1k / -4k / -10k / -24k Hz, dB |
|---|---|---|---|---|---|---|---|
| menu 2000-3700 | native | -19.8 / -20.7 | 14,984 | 0 | 0 | 0.79 | -1.2 / -7.6 / -13.5 / -17.9 / -22.8 |
| | browser | -19.1 / -20.1 | 16,206 | 0 | 0 | 0.80 | -1.3 / -7.4 / -13.2 / -18.0 / -22.9 |
| video 4500-5700 | native | -21.4 / -21.5 | 10,590 | 0 | 0 | 0.82 | -6.2 / -2.0 / -10.3 / -14.5 / -28.5 |
| | browser | -21.5 / -21.5 | 10,590 | 0 | 0 | 0.82 | -6.2 / -2.0 / -10.3 / -14.5 / -28.5 |
| video 10700-11900 | native | -21.6 / -21.4 | 11,204 | 0 | 0 | 0.84 | -7.1 / -2.2 / -7.6 / -15.6 / -31.9 |
| | browser | -21.5 / -21.4 | 11,204 | 0 | 0 | 0.84 | -7.1 / -2.2 / -7.6 / -15.6 / -31.9 |
| race 12575-14100 | native | -9.2 / -9.4 | 32,768 | 22,381 | 1,400 | 0.15 | -3.2 / -5.0 / -8.1 / -13.5 / -21.1 |
| | browser | -8.9 / -9.5 | 32,768 | 31,281 | 3,181 | 0.25 | -3.1 / -5.0 / -8.6 / -13.1 / -19.6 |

- Video: identical, sample for sample.
- Menu: with one fixed shift 67 percent of the window is bit-identical; the rest differs because menu sounds and the music start fall at slightly different moments in the two runs (the browser presses are timed by a 50 ms poll).
- Race: the two runs are different races (the car crashes at different moments), so only the statistics can be compared, and they agree in level and spectrum.
- **A finding for the native side, not the browser: the race mix is clipped.** In the browser capture 31,281 samples of 1,224,960 are at full scale, in 4,799 runs of which 964 are 8 samples or longer and the longest 81; a quarter of all samples are above half scale; the level is -9 dBFS RMS against -20 in the menus. The native recording has the same (22,381 at full scale). So the mixer itself saturates in the race. Whether a real console does too (it also clamps) or the mix is too hot because of something the runtime does (voice or master volume, the missing reverb path) is for the native half to settle.

### What these measurements cannot tell us

They show that the browser delivers the mixer's samples unaltered and on time. They do not show that the samples are the right sounds: a wrong instrument, a wrong pitch, a missing voice or a wrong pan would pass every check above. They also say nothing about the output device and nothing about a real controller's timing.

### What Mark should listen for (page at `http://localhost:8620/`, sound on)

1. The first three seconds of the race: a crackle or stutter as the race starts (the 72 dropouts).
2. The race in general: harsh, distorted or "crunchy" engine and crash sound (the clipping), compared with clean music in the menus.
3. The training video: do the voice and effects match the picture, or does sound feel late (the 100 ms offset)?
4. Left and right: a car or siren passing on one side should be on that side.
5. Music in the menus: correct songs, steady tempo and pitch, no clicks at loop points; the EA Trax pop-up should name the song that is playing.
6. Whether anything is simply missing: engine note, tyre noise, crash sounds, the DJ, menu clicks. Reverb is not modelled, so a dry sound is expected.
