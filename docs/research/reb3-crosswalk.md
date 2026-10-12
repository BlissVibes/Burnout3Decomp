# reB3 crosswalk: what the Xbox reimplementation already solved for us

Date: 2026-10-12. Source: halcaponey/reb3 (fork of lolz0r/reb3), MIT, head c49c398, cloned outside the repo. Six parallel readers (overview and web shell, front end and inputs, data formats against our disc, physics and AI, audio and effects, source and tooling) read all 28 docs, the source and the tools; their findings are condensed here. Facts measured on our PS2 disc are marked "measured"; reB3's own evidence tags are kept ([C] confirmed under emulation, [S] inference, GLUE harness invention, INSPIRED not retail); our inferences are marked "guess".

## What reB3 is

A C harness that plays Burnout 3 off the user's own Xbox NTSC-U disc. Recovered retail logic sits in separate modules, each citing the XBE address it came from; the 25,000-line harness around it is original code, not decompilation. It plays 36 events and 107 vehicles with AI, traffic, takedowns, crash cinematics, the retail HUD and front end, and Crash FM. Physics, collision, triggers and control flow are held 1:1 to retail; the look is relaxed, and a separate "photorealism" layer is marked INSPIRED and must never be cited as game behaviour. The method: the XBE is rebuilt as a correctly mapped ELF, read in Ghidra, and individual retail functions are run under Unicorn with seeded state and diffed field by field against the C port (about 4,000 checks across 46 Python suites). No full-game traces exist; xemu was used only for screenshots. Web and Android builds exist but are suspended; the active target is native Windows.

Licence: MIT, so knowledge and code are both usable with attribution. Caveats: the Rockbox WMA decoder it fetches is LGPL and not needed by us (PS2 has no WMA); `src/burnout3_physics_params.h` and `src/burnout3_sfx_emitters.h` carry retail parameter names, defaults and wave names copied from the Xbox executable, which our no-game-data rule forbids in this repo.

## The two builds share the game code and most data

The PS2 disc (measured: 775 files, 2.87 GB) has the same directory layout, file names and container formats as the Xbox disc:

| Data | PS2 state (measured) | reB3 parser | Verdict |
|---|---|---|---|
| `GAMEDATA.BGD` (events, start grid, nav graph, traffic descriptors, AI pace) | version 9, same record layout, same TDESC example values | `decode_bgd.py`, `cx_paths.c`, `cx_traffic.c` | reusable as is |
| `TLIST.BIN`, `VLIST.BIN` (36 tracks, 107 cars, base-40 ids) | identical | `extract_tlist.py`, `read_vlist` | reusable as is |
| `GLOBALUS.BIN`, `HEADUS.BIN` (UTF-16 string tables) | same format, 4006 strings; indices shifted by about 21 against Xbox above index 209 and different below | `extract_*` string walk | format reusable, index tables not |
| `VDB.XML` (per-car physics tuning, hashed keys) | header and record counts parse; a community PS2 dump matched 14 of 16 Xbox defaults | `extract_car_vdb.py` | likely reusable |
| `.AWD` wave dictionaries (174 files, 1575 waves, same names) | same container, payload is 4-bit PS-ADPCM, channels field 0 | `extract_awd.py` | walk reusable, needs a VAG decoder |
| `.RWS` audio streams (crash beds, menu loop, DJ, EA Trax, movie audio) | same chunk layout; multi-segment files replace the Xbox XWB banks; 0x2000-byte interleave | `extract_rws.py` | header reusable, needs multi-segment support and VAG |
| `STREAMED.DAT` collision kd-trees and light probes | identical layout, two header offsets shift by 0x10; all 36 units of one track parse | `extract_collision.py` | reusable with a two-offset change |
| `STATIC.DAT` track geometry, `.BGV` car meshes, `.TXD` textures | headers and tables match; payloads are VIF/DMA packets and GS PSMT4/PSMT8 textures | mesh and texture decoders | not applicable; new decoders needed |
| Fonts | compiled into the ELF on both platforms | `extract_font.py` method | method applies, addresses do not |

Not on the Xbox side at all: the IOP modules, the NETGUI online front end, the bundled Need for Speed Underground demo, the MPEG-2 videos, and a handful of DATA files (PRGDATA.BIN, LOADSCRN.BIN, STAGEHED.BIN, the KFS tables, the icons). `Data\Global.txt`, which our run logs show the game asking for, is not on the disc.

Both platforms are little-endian, so every in-file structure keeps its byte order. The source is the same Criterion C/C++ for the gameplay layers, so the logic transfers; the struct offsets reB3 asserts at retail positions (105 fields on the vehicle object alone) are a hypothesis for the PS2 build until the first PS2 twin is found and checked.

## What transfers to the port now

1. **The front-end flow is not in reB3.** Its menu is the harness's own, and it never found the retail controller mapping. For menus we are on our own, and our own probe runs already reach the event screen. Useful anyway: the unlock model (Single Event tracks are unlocked by World Tour progress, so a fresh profile may list few or none; event rows with unlock hint 0xFFFFFFFF are always open), the PS2 string table facts (R1 is boost and aftertouch; the game demands a DualShock 2 in analog mode; there is a "CONTINUE WITHOUT SAVING" path when no memory card is present; 50/60 Hz and 480p dialogs exist in the table), and the fixed random seeds, which make scripted runs reproducible.
2. **Audio verification.** Every wave and stream name is shared, so once we decode VAG we can compare durations and envelopes against the Xbox PCM. Expected audible sequence at a race start: ident jingle, one DJ line, then the song, never overlapping; DJ gap every 60 to 70 s; crash beds in counter order; effects pitch to 0.75 under aftertouch while music keeps tempo.
3. **Runtime health checks for a race.** Collision exists only once the streamer marks a unit block resident, so CDVD timing faults show up as falling through the world. Silent failures of the VDB or BGD reads leave the game on compiled defaults ("the whole fleet drives the same"). The game clock is a frame period with an integer divisor (slow motion is divisor 5 or 6), so wrong vertical-blank cadence mis-scales physics, AI clocks and crash cinematics.
4. **Race renderer expectations** (Xbox order, to be confirmed on PS2): scene to an off-screen target, sky dome, streamed opaque then alpha geometry, cars, blob shadows, sprites and particles, then a downsample-and-blur chain composited at double gain, HUD, gamma. Planar water reflection samples the scene. No post-processing changes for crashes or takedowns [C].
5. **Web shell patterns** (MIT, attribute lolz0r): a worker owning the disc File with atomics-blocked synchronous reads into the shared heap; a shared-memory audio ring with an AudioWorklet consumer; OffscreenCanvas present through an ImageBitmap; a headless Chrome smoke harness over raw CDP that hands the disc in through a file input and gates on log lines and pixels; and the recorded lessons (large IndexedDB values fail, a main-thread OPFS write causes audio underruns, pre-size the pthread pool, never make synchronous proxied calls per frame).

## What transfers to the decompilation

### A function map with anchors

reB3 cites about 745 distinct Xbox functions; the physics, collision, crash, props, AI, nav, traffic and gameplay-rule core is roughly 300 of them with a fidelity grade each. The addresses are useless on PS2, but the anchors that locate each function are not:

- **Strings.** All tunables go through one ValueDB registrar: `"Physics/Drift"`, `"Slide Max"`, `"../Export/ValueDB/VehiclePhysics/%s.cfg"`, `"../Export/ValueDB/AI/defaults.cfg"`, `"Score/Burnout Points"`, `"Boost Bar"`, `"Camera/Follow"`, `"Sound/Surface"`, `"Sound/Skids"`; also `"Gamedata.bgd"`, `"tracks\\crash%d.rws"`, `".btv"`, `"Ghost car"`, `"Aftertouch"`, `"VehicleUnderside"`, the DJ bank format strings, `"sound\\crashmod.awd"`, `"bstpsl0%i"`, `"cartar0%i"`, and the 10-character base-40 wave stems.
- **Tables and immediates.** The 256-word CRC table of the ValueDB hash; the traffic RNG seeds 0xFD462907 and 0x02B9D6F8; the 7-by-7 crashability byte table starting 1,1,1,1,1,1,0; the authority band table 0, 0.1, 0.4, 0.5, 1; divisions by 40 in the base-40 codec.
- **Float constants.** 2.2374146 and 2.23693633 (m/s to mph), 9.549296, 0.44704, 17.8816, 4.4704, 13.4112, 22.352, 1/18000, 150.0, 27.5, 0.707, 0.99, 5000, 2500, 45.0, 0.455, 0.60, 0.85, 0.75.
- **Call-graph shape.** The per-car frame calls the same five callees twice with half the time step; the collision manager calls qsort and walks six class arrays; the aggression machine is an eight-state jump table.
- **Struct-offset fingerprints**, valid only once the layout is confirmed: 0x1534 (authority), 0x1353, 0x13E4, 0x1598, 0x16C0, 0x1920, 0x27D8, 0x23C0, 0x23C4 co-occurring in one function.

The physics reader's suggested first twenty matches, in order: the ValueDB hash and registrar; the vehicle physics registrar, defaults and config-to-live copy (which yields the PS2 vehicle layout); the traffic, AI, score and boost registrars; the per-car frame; the force pass; the integrator; engine and gearbox; chassis-versus-world crash decision; racer-versus-racer; wreck and traffic collisions; the collision manager; the AI driver; the authority ladder; rubber band and pace; the PRNG and traffic seed. After those: the ray cast, kd-tree walk, hull copy, slam handler, aggression table, avoidance.

### Recovered semantics as specifications

For each mapped function reB3's docs give the equations, thresholds and quirks (gravity 10.0 in the force term, the signed contact impulse, the off-by-one in traffic model picks, the dead AI parameters, the rsqrt approximation in the heading difference). These are reusable as the comments and the test oracles of decompiled functions. The retail struct tables (vehicle 0x1A00 window with wheels at +0x820 stride 0xC0, racecar stride 0x27E0 with the AI object at +0x1A00, physics config 0x1D0 with 64 floats at +0xB8, hull 0x600, collision records 0x30, the nav section and traffic descriptor layouts) are the first thing to confirm or correct on PS2.

### Method and tooling to adopt

- **Per-function backend switch.** reB3 runs each of ten features either as its C port or as the retail x86 in a Unicorn sidecar, selected per feature. Our recompiled EE code is that oracle, in process and at native speed: a decompiled C function replaces a recompiled one by address and can be A/B'd live.
- **Differential validation from real state.** reB3 seeds synthetic state by hand because it has no traces. We can do better: dump EE RAM at chosen frames from our runtime, call a recompiled function on a snapshot, and diff against the decompiled C on the same snapshot. Unicorn has no R5900, so the recompiler replaces it. Decompiled C must be compiled against the same EE FPU model (round toward zero, no denormals) or rounding will masquerade as logic errors.
- **Struct parity tooling.** Offsets live in source comments; a generator emits packed structs with a static assertion per field; a relayout tool moves harness-only fields past the retail window. `tools/relayout_struct.py` and `gen_retail_struct.py` are pure text tools and can be copied with attribution.
- **Ledgers.** A master index of addresses, methods and verdicts; a ledger of every invention; a to-do board with the blocker and starting function per item; evidence tags at every site.
- **No-game-data gate.** reB3's validator reads its needle strings from the user's own files at test time so the test itself carries no game data. Ours should scan every tracked file, not just the source.

## What does not transfer

Everything on the D3D, NV2A shader, XInput, DirectSound, XACT, WMA and Xbox Live side; the present composite and gamma ramp; every absolute address; the MSVC calling conventions and vtable byte offsets (match PS2 vtables by slot index); SSE evaluation-order effects; reB3's float traces as a bit-level oracle (the EE FPU rounds differently). The track, car and texture decoders need PS2-native replacements: a VAG decoder, a GS PSMT4/PSMT8 plus CLUT decoder with swizzle, and a VIF/DMA mesh decoder.

## Open questions to settle first

1. Does the PS2 vehicle object keep the Xbox layout? Find the PS2 twin of the config-to-live copy (64 float stores from a 0x1D0 record into the +0x1360 window) and compare.
2. Does the PS2 ValueDB use the same CRC table and key composition? Search the ELF for the 256-word table.
3. Which compiler built SLUS-21050 (ee-gcc 2.9x, 3.x, or SN ProDG)? It decides vtable layout, inlining of the vector helpers (likely VU0 macro code, guess) and switch-table shape, and it is the first question of any matching decompilation.
4. What are PRGDATA.BIN, LOADSCRN.BIN, STAGEHED.BIN and the KFS files, and where does `Global.txt` come from?
5. The RWS segment index to Xbox bank index mapping for the DJ and EA Trax banks (guess: one to one).

## Provenance

reB3 is credited in `PROVENANCE.md`; this document uses its knowledge only. If any of its tools are copied into this repo (the struct tools, the smoke harness), the copy must carry the MIT notice naming lolz0r.
