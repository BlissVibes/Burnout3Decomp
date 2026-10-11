# Provenance and Credits

Every external project we learn from, borrow from, or build on is listed here,
with its license and how we use it. Update this file whenever we start using
something new. "Use" levels: **reference** (read to learn, nothing copied),
**tooling** (run as a tool, not shipped), **derived** (code or data adapted or
included in this repo; must be license-compatible and credited in-file).

Our repo license: MIT (Copyright 2026 BlissVibes).

| Project | Author(s) | License | Where | Use (current) | Notes |
|---|---|---|---|---|---|
| ps2-web-recomp | snuri00 | MIT | https://github.com/snuri00/ps2-web-recomp | reference (candidate: derived) | PS2 EE static recompiler to C++ to WebAssembly, HLE EE kernel, IOP R3000 interpreter, WebGPU GS, SPU2, IPU, VU1 AOT/JIT. Runs GT4 and GoW II in-browser. Sibling: psp-web-recomp. |
| Burnout3 (matching decomp) | siddharthakumar-98 | GPL-3.0 | https://github.com/siddharthakumar-98/Burnout3 | reference only | Byte-matching decomp of SLUS-21050 with CodeWarrior PS2 3.0.1b119. D0-D5 done, 129/5654 game functions matched (2.3%). GPL-3 is NOT compatible with our MIT repo: do not copy code. Binary map, symbol names, and toolchain findings may inform our own work only via independent re-derivation. |
| GameMerge | siddharthakumar-98 | (check) | https://github.com/siddharthakumar-98/GameMerge | reference | Confirms CodeWarrior 2.4.1.01 / RenderWare 3.6 identification. |
| reB3 | halcaponey (fork of lolz0r/reb3) | MIT | https://github.com/halcaponey/reb3 | reference (candidate: derived) | C reimplementation of Burnout 3 (Xbox default.xbe). Has Emscripten WebGL build, SDL2, gamepad, BGV reader/writer, Blender addon, docs/BGV_EXTRACTION.md, differential validators against the retail binary under Unicorn. |
| Burnout-3-Recompiled | matheuz232 | none stated | https://github.com/matheuz232/Burnout-3-Recompiled | reference only | Native x64 recompile attempt; does not boot. Useful fact: ELF entry 0x00100008. No license, so nothing may be copied. |
| PS2Recomp | ran-j | (check) | https://github.com/ran-j/PS2Recomp | reference | Experimental PS2 to C++ static recompiler + runtime; ps2xAnalyzer/ps2xRecomp/ps2xRuntime/ps2xIOP module split; Ghidra ExportPS2Functions.java script. |
| N64Recomp / N64ModernRuntime | N64Recomp contributors | MIT | https://github.com/N64Recomp/N64Recomp | reference | Design pattern: literal translation, runtime-owned indirect calls, platform I/O via callbacks. |
| XenonRecomp | hedge-dev | MIT | https://github.com/hedge-dev/XenonRecomp | reference | Design pattern: weak-linked function hooks, mid-asm hooks, jump-table detection. |
| Play! | jpd002 | BSD-2-Clause | https://github.com/jpd002/Play- | reference | HLE-BIOS PS2 emulator with browser build (Play!.js). ps2-web-recomp borrows its IPU tables. |
| librw | aap | (check, permissive) | https://github.com/aap/librw | reference | Open re-implementation of RenderWare Graphics; reference for RW 3.x structures, PS2 native data. |
| Burnout Modding Information and Format Documentation | AcuteSyntax | gist, no license | https://gist.github.com/AcuteSyntax/536a2d62ab1b3fde5c14f70d268b14c0 | reference | BGV/BTV header layout (rev 0x17), DAT containers, TXD notes, BGD track config, PS-ADPCM audio. |
| Burnout Wiki | burnout.wiki community | CC (check page) | https://burnout.wiki/wiki/Modding , https://burnout.wiki/wiki/Vehicle_Data | reference | Format docs and tool list. |
| Edness tools (Noesis fmt_Burnout3LRD.py, BGV Palette Splitter, TXD scanner) | Edness | (check per tool) | https://ednessp.github.io/tools | tooling (candidate) | Texture/archive/model extraction for PS2 Burnout 3. |
| AWDio | escape209 | (check) | via burnout.wiki | tooling (candidate) | Wave Dictionary (AWD) audio extraction. |
| splat / spimdisasm / m2c / objdiff / asm-differ / decomp-permuter / decomp.me | decomp community | MIT / various | github.com/ethteck/splat, github.com/decompals/m2c, github.com/encounter/objdiff, decomp.me | tooling (candidate) | Standard matching-decomp toolchain. |
| ghidra-emotionengine-reloaded | chaoticgd / astrelsky | Apache-2.0 | https://github.com/chaoticgd/ghidra-emotionengine-reloaded | tooling (candidate) | R5900 language module for Ghidra. |
| Reburn 3 | MattKC | n/a | https://forum.mattkc.com/viewtopic.php?p=871 | reference | Earlier PC-port effort via Cxbx injection; historical context. |
| AI Signal: "How to Decompile Games With AI" | Macabeus | article | https://aisignal.day/en/ai-game-decompile-howto-2026/ | reference | Agent-loop workflow: per-function folders, numbered attempts, score, 3-strikes stop rule, notes.md. |

Source material we do NOT redistribute: the Burnout 3 ISO, SLUS_210.50 ELF, VU
microcode, PS2 BIOS, any SDK or CodeWarrior binaries. The user supplies their
own disc at runtime.
