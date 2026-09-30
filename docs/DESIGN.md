# Clementine: design

A wavetable instrument for MPC OS devices modelled on the Waldorf Microwave II/XT. Started 2026-09-30 in
`mpc-vst-plugins` (`docs/proposals/`), moved here. Paths such as `wrapper/`, `tools/`, `docs/NOTES.md`,
`docs/PORTING.md` and `docs/BENCH.md` refer to a checkout of [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins)
(`MPC_VST`).

Status: design, nothing built yet. Author's inputs: the Microwave 2/XT *Controller Number Assignment* (OS 2.09)
and *System Exclusive Specification* (OS 2.16) documents, plus the projects listed under Sources.

## 1. Why the ROM emulators don't run, and what that changes

Xenia (Microwave II/XT) and Vavra (microQ) come from gearmulator. They run the original firmware on emulated chips:
a 68k-family microcontroller plus one DSP56300-family DSP for the XT (three with the voice expansion). gearmulator's
README says it supports **64-bit x86 and aarch64 only; 32-bit architectures are not supported**. The dsp56300 core has
JIT back ends for x64 and aarch64 only (`jitops_*_x64.cpp`, `jitops_*_aarch64.cpp`), so on a Gen1 MPC (RK3288,
32-bit armhf `MPC` binary, `docs/NOTES.md` "CPU layout") only the interpreter could run, and an interpreted
56300 at full speed is out of reach on a 1.8 GHz Cortex-A17. Surge XT has the same limit ("will not build on 32-bit
Raspberry Pi systems").

So there are two routes. They don't exclude each other:

| | Track A: native "XT-alike" engine (recommended) | Track B: run the real XT DSP program |
|---|---|---|
| What | New C engine that implements the XT's voice architecture and reads XT data (waves, tables, sound dumps) | Static recompile of the XT's DSP firmware (as Monomodule/Machinedrum do for their DSPs) plus a 68k host emulator |
| Accuracy | As close as calibration gets it (section 4) | Bit-exact if it runs |
| CPU | Fits, by design (section 5) | Unknown. Needs a spike: 56300 static recompile + 68k interpreter on armv7 |
| Distribution | Normal release, user supplies the data files (JV-880 precedent) | `build-yourself` (firmware-derived), like Monomodule |
| Gen2 note | Same | If a Gen2 MPC runs a 64-bit `MPC` (unverified: `tools/probe_device.sh`), gearmulator's aarch64 JIT becomes an option there |

The rest of this document is Track A. Track B stays a time-boxed spike (section 11), because if the existing
recompiler handles a 56300 program, the result would be exact.

## 2. The core idea: adopt the XT's own data model, don't design a "similar" synth

Everything that makes presets portable is already specified in the attached SysEx document. The engine uses it as
its native format:

- **The patch is the 256-byte SDATA block** (SysEx spec 3.1). The engine's state *is* that block. A `.syx` single
  dump (`F0 3E 0E dev 10 BB NN <256 bytes> xsum F7`) or an all-sounds dump (256 x 256 bytes, A001..B128) loads with
  no conversion. That is how "port the presets" gets to 100% at the data level: the only remaining error is in the DSP.
- **VST parameters = the non-reserved SDATA fields, in SDATA index order**, with the XT's raw ranges (0..127,
  enums, the -64..+63 offsets). The order is stable by construction (PORTING.md section 2: append only). Display strings
  follow the XT's own value texts (gearmulator's `parameterDescriptions_xt.json`, GPL-3.0, lists them all).
- **MIDI CCs follow the controller table** (CC 5 glide time, 14-21 envelopes, 24-31 LFOs, 33-48 osc/mix, 50-62
  filters/amp, 70-83 wave, 85-93 free env, 102-111 arp, 112-118 LFO extras). External controllers and MPC
  automation then behave like on the hardware.
- **SNDP parameter-change SysEx** (`F0 3E 0E dev 20 LL HH PP XX F7`, index = HH*128+PP) is accepted as well, so the
  plugin can be driven by existing XT editors if MPC ever forwards SysEx (untested).
- **Plugin state (chunk) = SDATA + a small header** (bank/program name, effect state). A "save .syx" action writes
  the current sound as a real single dump, so sounds move between the plugin, Xenia and real hardware.

Wave data follows the XT layout too: a wave is 128 signed 8-bit samples, stored as 64 (the second half is
`w[64+n] = -w[63-n]`); a wavetable is 64 slots referring to wave numbers; slots 61-63 are always triangle, square and
saw; empty slots are filled by the firmware with a "spectral interpolation" of their neighbours. gearmulator's
constants: 506 ROM waves, 250 RAM waves (1000-1249), 128 tables (96-127 user), tables 28-51 and 64-95 (0-based) are
algorithmic (computed, no control table). The manual documents waves 000-299 and 65 ROM tables; the oracle dump shows what the
firmware actually holds (see the extraction results).

### Errata to handle in the parser (the SysEx document contradicts itself)
- Osc semitone: SDATA says 52..76, the CC table says 56..76. Accept the full range, clamp to +-12.
- Wave start phase: "3-257 degree" in SDATA is a typo for 3..357.
- SDATA 188-191 are labelled "Modifier 3" again; they are Modifier 4.
- Mod destinations: range says 0..33, but the list has 36 entries (34 FM Amount, 35 F1 Extra). Use 0..35.
- Arp range: SDATA 1..10, CC 103 0..9 (CC value + 1).
- MULP: the ID table says 20h, the format says 21h. GLBR: 04h in the format, 14h in the ID list.
- Found by comparing the spec with the firmware's own 256 sounds (oracle `dumpall`, 2026-09-30; the bank stays local):
  the dump checksum is the sum of the SDATA bytes only, not BB+NN+SDATA as written (`syx.c` writes that form and accepts
  either); Filter 1 type goes up to 12, not 9 (10-12 are the two notches and band stop, per the manual; see docs/MANUAL_NOTES.md); Chorus (82) takes 0..2; Arp Tempo (93)
  holds 0; and the "reserved" bytes 9, 22, 33, 44, 69, 78 are non-zero in factory sounds (64 in the init sound), so
  they are kept as loaded. Effect types seen in factory sounds: 0, 1, 2, 3, 6, 8, 9, 32, 33, 34, so the XT's list goes
  well past the manual's 10 and the last three are still unnamed. Init defaults in `tools/gen_patch.py` come from the
  firmware's init sound.
- Effect type: 0..35 on the XT, "subject to change", no list in the SysEx document. The user's manual (covers II, XT
  and XTk) names 7 types on the II (Chorus, Flanger 1, Flanger 2, AutoWahLP, AutoWahBP, Overdrive, Amp. Mod) and 3 more on
  the XT (Delay, Pan Delay, Mod Delay), so 10 in total; the range 0..35 leaves room the manual doesn't document. The
  numeric order, and whether the II's `0-7` means "off" plus these 7, is not stated: confirm with the oracle or a dump.
  Parameters per type (the three effect parameters SDATA 81/83/86, in the order the manual shows them):
  Chorus / Flanger 1: speed, depth, mix. Flanger 2: speed, feedback, mix. AutoWahLP / AutoWahBP: sense, cutoff, resonance.
  Overdrive: drive, gain, amp type (Direct, Combo, Medium, Stack). Amp. Mod: speed, spread, mix (tremolo when dry > 63,
  low-frequency ring mod when dry < 64). Delay / Pan Delay: time (note value + BPM), feedback, mix. Mod Delay: time,
  speed, depth. Mix displays as dry:wet (127:0 .. 0:127). Only Instruments 1-3 of a Multi can use effects (irrelevant
  here: one plugin instance is one part).

## 3. Engine architecture (C, `wrapper/engine.h`, no JUCE)

```
 MIDI/CC/SysEx -> voice alloc (poly/mono, normal/dual/unison, glide, arp)
                     |
 control tick ------>+  (control rate to be measured; SDATA 54 "Time Quantization" hints at it)
   Filter env ADSR, Amp env ADSR, Wave env (8 stages, key-on/key-off loops),
   Free env (3 stages + release, bipolar), LFO1/2 (6 shapes, delay, sync, symmetry,
   humanize, phase), 4 modifiers (+ - * / xor or and S&H ramp switch abs min max lag
   ctlfilter diff) + modifier delay, 16-slot matrix (32 sources x 36 destinations)
                     |
 per voice, per sample at 40 kHz:
   Osc1/Osc2: wave position -> table slot -> mip level -> 8-bit wave read
              (sync, link, Osc1 FM, keytrack, pitchbend scale, 3 fixed tri/sqr/saw)
   Mixer: wave1, wave2, ring mod, noise, (external) -> "Clipping: saturate/overflow"
   Filter 1: 24LP 12LP 24BP 12BP 12HP sin-shaper>12LP 12LP>shaper dual-LP/BP FM-LP S&H-LP
   Filter 2: 6 dB LP/HP
   Amp: volume, velocity, keytrack, pan, pan keytrack
                     |
 mix -> effects (chorus + XT effect types) -> 40 kHz -> 44.1 kHz resampler -> int16 out
```

Design decisions:

- **Run the core at 40 kHz internally.** gearmulator's XT hardware class runs at 40 kHz (`wLib::Hardware(40000)`). Where
  the hardware aliases, it aliases at 40 kHz; running at the native rate keeps that character and lets the calibration
  compare against the oracle sample for sample. One polyphase 40 to 44.1 kHz resampler (400:441, so 441 phases) on the summed stereo
  output costs almost nothing next to the voices.
- **No band-limited oscillator.** The DSP keeps each of a part's 64 waves as a mip pyramid in Y memory
  (128+64+32+...+1 = 256 words per wave, `xtWavePreview.cpp`) and reads from it. The XT's sound *is* stepped 8-bit
  waves with its own aliasing. Surge's windowed-sinc wavetable oscillator and FigBug's Wavetable would sound too clean
  and cost more. The "Aliasing" (off, 1-5), "Accuracy" and "Clipping" parameters are the knobs that choose how dirty.
- **Discrete wave stepping, no crossfade.** Movement comes from the 64 precomputed slots, not from morphing between
  them. That makes the oscillator cheap: a table lookup per sample.
- **Float for control, fixed-point where the hardware is.** The DSP56300 has 24-bit data and 56-bit accumulators. Where
  overflow/wrap is audible (mixer "overflow" mode, resonant filters), model it with integer or explicit wrap, not float
  saturation.
- **NEON across voices.** Process 4 voices per NEON lane group for the filters and mixer (Cortex-A17 has NEON/VFPv4).
- **One plugin instance = one XT part.** MPC tracks already provide multitimbrality, so Multi mode (MDATA/IDATA) is
  only parsed for importing sounds, not emulated.
- **Arp runs inside the instrument.** MPC ignores plugin MIDI out (NOTES.md), but the XT's arp only drives its own voices.

## 4. Getting it to sound as close as possible: the oracle rig

The accuracy work is measurement, not guessing. gearmulator's `xtLib` (GPL-3.0) runs fine on an x64 dev machine, so it
serves as a **firmware oracle**: an offline test tool that loads the user's own XT ROM or OS update, is driven by
SysEx and MIDI, and exposes the emulated DSP's memory and audio. It is a dev tool only. It never ships and never goes
to the device. (Someone else's Faust project proposes the same "firmware oracle" approach for the XT; see Sources.)

What the oracle gives us, in order of value:

1. **Exact wave data, including interpolated and algorithmic tables.** Select table N on part 0 (SNDP param 25), let
   the firmware build it, then read DSP Y memory at the wave area (`0x20000 + part*64*256`, 256 words per wave, per
   `xtWavePreview.cpp`). That gives all 64 slots *after* the firmware's spectral interpolation and for the
   algorithmic tables, plus the mip levels the DSP actually uses. We never have to reverse-engineer the interpolation.
   Confirm first that the firmware writes the table there on a table change. Output: one file per table, 64 x 256
   int8. Keep the WAVR/WCTR (sysex spec 2.31/2.41) dump of raw waves and control tables too, for user wavetables.
2. **The host-to-DSP control stream.** Log the HDI08 words the 68k sends the DSP while playing test patches. If per-voice
   pitch, wave position, cutoff coefficients and envelope levels travel that way (to be confirmed), the synth splits in
   two halves that can be matched separately: the control math (envelope curves, LFO shapes and rates, keytrack,
   cutoff to coefficient) against the logged stream, and the audio DSP against audio given the same stream.
3. **Filter responses through the external input.** The XT mixes an external input (SDATA 51, XT only). Feed sweeps,
   impulses and noise through each Filter 1 type at a grid of cutoff/resonance/keytrack values; fit our filters to the
   measured magnitude and phase responses and to the level-dependent (nonlinear) behaviour.
4. **Oscillator and mixer tests** with the filter open: every table at several pitches, sync, ring mod, FM, the
   aliasing/quantize/clipping settings.
5. **Preset-level regression.** Render every factory sound at several notes and velocities through both engines at
   40 kHz, and score each pair: multi-resolution STFT distance, RMS envelope contour error, pitch error. The scores
   run as a CI job and gate changes, so accuracy only goes up. Blind A/B listening for the sounds with the worst scores.

Order of work (bottom-up, so each layer is verified on a known input): waves, then oscillator pitch/phase/sync/FM,
then mixer and clipping, then filters, then envelopes and LFOs, then the matrix and modifiers, then effects.

## 5. CPU budget (to be confirmed with `tools/bench.sh` on a device)

A 128-frame block at 44.1 kHz is 2902 µs; PASS in BENCH.md is p99 <= 15%, about 435 µs. At 40 kHz a block is about
116 internal samples, so 10 voices (the XT's polyphony) is about 1160 voice-samples per block, about 375 ns or about 675
cycles each at 1.8 GHz. A table read, a mixer, a 2- or 4-pole filter and a 1-pole filter take on the order of 100 cycles,
and the modulation runs at control rate. 10 voices should PASS with room to spare. 30 voices (expanded XT) is a
likely WARN, so make polyphony a parameter. The first device bench happens as soon as one voice plays (Phase 1),
not at the end.

## 6. How the listed projects fit

| Project | Licence | Use |
|---|---|---|
| gearmulator (Xenia) | GPL-3.0 | The oracle (section 4). Reference for data layouts (`xtRomWaves`, `xtWavePreview`, `xtMidiTypes`, `xtState`) and value texts (`parameterDescriptions_xt.json`). Not runnable on 32-bit armhf. |
| Surge XT | GPL-3.0 | Not as a whole: no 32-bit ARM build. `sst-filters` is header-only GPL-3.0 (SSE intrinsics; on ARM via simde, which maps to NEON). Its LP/BP/HP and S&H filters are starting points before calibration. `sst-effects` for chorus/delay until the XT effects are modelled. |
| schwung-tablor | BSD-3-Clause | A Schwung module, so it builds for MPC today through `adapters/schwung` with no code change. Use it on day one to prove the build, skin, bench and install path, and borrow voice handling and file loading. Its DSP is FigBug-Wavetable-style, not XT, so it gets replaced. |
| FigBug Wavetable | BSD-3-Clause, needs JUCE | Not usable in the C wrapper. Useful for UI and modulation ideas only. |
| Waldorf legacy page | Waldorf's terms | Source of the manuals, OS updates and sound banks the user downloads. Could not be fetched from this environment, so its exact terms still need reading. |

## 7. Code layout and runtime model

One C engine behind `wrapper/engine.h`, split so each file maps to one calibration target:

| File | Job |
|---|---|
| `patch.c` | The 256-byte SDATA struct, range clamping, the errata from section 2, defaults (an init sound) |
| `syx.c` | Import: single, all-sounds, SNDP, WAVD/WCTD (spec 3.4/3.5), MW1 format later. Export: single dump |
| `waves.c` | Wave store (ROM slots, RAM 1000-1249, the open set), the 64-slot table builder (slot interpolation), mip pyramids, algorithmic tables |
| `import.c` | Worker-thread importer for user files (section 10), writes a cache next to the `.so` |
| `osc.c`, `mixer.c` | Wave read, sync, FM, ring mod, noise, clipping modes |
| `filter.c` | Filter 1's 10 types, Filter 2 |
| `env.c`, `lfo.c`, `mod.c` | ADSRs, wave env with loops, free env, LFOs, matrix, modifiers, control-rate tick |
| `voice.c`, `alloc.c`, `arp.c` | Voice state, poly/mono, normal/dual/unison, glide types, arpeggiator |
| `fx.c`, `out.c` | Effects, 40 to 44.1 kHz resampler, gain staging, int16 conversion |
| `engine.c` | `mpc_engine()`: parameter keys to SDATA fields, CC map, programs, chunk |

Runtime rules:
- **Every table is built before it is needed.** The XT's host rebuilds a table when the sound changes it. Here the
  loader builds all 128 tables (128 x 64 slots x 256 bytes = 2 MB of int8) on a worker thread when the wave data
  loads, and a table change on the audio thread is a pointer swap. No glitch when a Q-Link sweeps the table
  parameter, which on the hardware stalls briefly.
- **Program changes** swap a whole SDATA block between blocks; voices keep playing and pick up the new values at the next
  control tick, as on the hardware.
- **Denormals:** set FPSCR flush-to-zero on the audio thread (NEON flushes already, VFP does not). The bench's
  release-tail stage checks it.
- **No allocation, locks or file I/O on the audio thread.** Loads and imports report status through a display-string
  parameter ("ORIGINAL WAVES: installed / not found / importing 40%").

## 8. Output stage

The signal path after the voices, in XT order:

```
voice: amp env x volume x velocity x keytrack -> pan (+ pan keytrack, + matrix) --+
                                                                                  |
all voices -> stereo sum (24-bit-style fixed point, XT clipping) -> effect -> chorus
           -> 40 kHz to 44.1 kHz resampler -> output trim -> soft limit -> int16 -> wrapper
```

- **Headroom.** The wrapper takes int16 from `render()` and hard-clips (`f2s` in `wrapper/vst2_wrap.c`). A 10-voice
  unison patch will clip if the voice sum is scaled like one voice. Sum at the XT's internal scale, then apply one
  fixed make-up gain calibrated so an init sound at full velocity peaks where Xenia's does. After that comes an
  **Output trim** parameter (appended after the SDATA fields) and a gentle limiter that is off by default, so
  "authentic" and "safe for live use" are both a setting away.
- **16-bit output.** Enough at the level MPC mixes, but the resampler and trim run in float and only the last step
  goes to int16, with TPDF dither on quiet signals. If that ever shows up as a limit, a float `render_f32()` field can
  be appended to `mpc_engine_t` (append-only, as the header already says) with the wrapper preferring it.
- **Resampler.** Polyphase FIR, 441 phases (441 output samples per 400 input samples), 96 taps (32 taps left a 19 kHz tone
  imaged only 13 dB down; 96 gives 59 dB), run once on the stereo sum. Two settings: *Clean* (steep, no images above 20 kHz) and *Vintage* (a gentler filter that keeps some of
  the XT's 40 kHz grit). Tune *Vintage* against recordings of real hardware, not Xenia, which has its own resampler.
- **Main/Sub outs.** The XT's second output doesn't map to a stereo VST2 instrument. The instrument sums both; the
  Multi `Output` field is ignored on import.
- **Effects.** The XT has one effect per sound (types 0..35, 3 parameters, SDATA 76/81/83/86) plus chorus (82).
  Implement chorus, flanger, delays and the other types one by one against the oracle; until a type is modelled it
  falls back to the nearest implemented one and the readout says so.

## 9. Controls and skin

### Parameters
- 203 non-reserved SDATA parameters (index 1..239; the name bytes 240-255 are a display string), in SDATA order, keyed by readable names (`osc1_oct`, `w1_start`,
  `f1_type`, `mod3_src`, ...). Reserved bytes and the name aren't parameters; the name is a display string.
- Appended after them: `bank` (file popup), `program` (stepper with name readout), `polyphony`, `output_trim`,
  `resampler`, `limiter`, `mod_view`, the wave-data status readout, and the skin's `__open` popups.
- Enums with 7 or more options (filter types, the 32 mod sources, 36 destinations, 36 effect types, 16 modifier
  ops, arp patterns) are `popup`s; short ones are `enum_h`.

### The XT's own performance controls map straight onto MPC
- **Play Parameters #1-4** (SDATA 58-61) are the XT's per-sound "four knobs you can reach from the play screen". They
  become the Play page's first Q-Link row, and each knob shows whatever parameter the sound assigned to it. The
  factory sounds were voiced around those four, so a loaded preset arrives already "performance mapped".
- **Controls W, X, Y, Z** are mod sources (list 3.12, 20-23). Expose them as four Q-Links on every page's second
  bank, so matrix routings that sound designers put on W-Z work on MPC as they did on the XT.
- **Mod wheel, aftertouch, poly pressure, breath, foot** come from MPC's MIDI as usual (check that MPC passes poly
  pressure and CC 2/4 to a VST2; record in NOTES).

### Pages (one per MPC tab, Q-Links follow the page)
| Page | Contents |
|---|---|
| PLAY | Preset browser (bank popup, program stepper, 16-character name), Play Params 1-4, W-Z, volume, glide, allocation/assignment/detune, wave-data status |
| OSC | Osc 1/2 octave, semitone, detune, keytrack, bend range, sync, link, Osc 1 FM amount |
| WAVE | Wavetable (popup with names), Wave 1/2 start wave, phase, env amount, velocity, keytrack, limit, link; a picture of the current slot |
| WAVE ENV | 8 time and 8 level sliders side by side, so the row of sliders reads as the envelope; trigger; key-on and key-off loop start/end |
| MIX | Wave 1, Wave 2, ring mod, noise, external levels; aliasing, time quantisation, clipping, accuracy |
| FILTER | Filter 1 cutoff, resonance, type, keytrack, env amount, velocity, special (its label follows the type: `when=f1_type:...`); Filter 2 cutoff, type, keytrack; filter ADSR + trigger |
| AMP | Amp ADSR + trigger, volume, velocity, keytrack, pan, pan keytrack, free envelope |
| LFO | LFO 1/2 rate (or sync division, `when=`), shape, delay, sync, symmetry, humanize, LFO 2 phase |
| MOD | 16 slots shown 4 at a time: `mod_view` 1-4 picks which (`when=mod_view:N`), each slot source popup, amount knob, destination popup |
| MODIFIERS | 4 modifiers (source 1, source 2, op, parameter) + modifier delay |
| ARP / FX | Arp settings and user pattern (16 steps as toggles, packed into SDATA 102-105); effect type, 3 parameters, chorus |

Limits that shape this (NOTES/ROADMAP): no native envelope or XY component for a VST2, so envelopes are slider rows;
no live meters (the wrapper has no engine-driven update path yet), so the wave picture and readouts refresh when a
control is touched, not while a note plays. A live oscilloscope would need that ROADMAP item first.

### Look: the orange XT
Palette sampled from a photo of the orange rack XT (approximate; adjust by eye against the hardware):

| Role | Colour | On the XT |
|---|---|---|
| Plate | `#F38302` | The orange front panel |
| Plate lines | `#D86000` | The darker orange arcs and section rules |
| Display surround | `#3290B1` | The teal band around the LCD |
| LCD glass | `#9FAE62` lit, `#797F57` unlit, text `#1F2414` | The yellow-green backlit 2x40 LCD |
| Section titles | `#4F6C9E` (slate blue, italic) | "Oscillator 1", "Filter", "Envelopes" |
| Control labels | `#5A5A6A` (small, condensed) | Names under each knob |
| Knob caps | `#6C6470` top, `#4A444E` skirt | The grey-violet rubber caps |
| Accent | `#F84D4C`, darker `#C8424A` | The big red encoder, red Cutoff and Wavetable knobs, red Shift button |
| Light knob | `#9C98A3` | Power / Sync caps |

- **Layout idea from the panel:** section titles sit on a thin rule with the title breaking it
  (`— Filter —`), knobs in two rows per section, and the most-used control of a section gets the red cap (Cutoff
  on FILTER, Wavetable on WAVE, the value/data knob on PLAY). Theme keys in `layout.conf`, knobs as `look=cap`
  recoloured, frames styled through `art_css=`.
- **LCD strip** at the top of every page (sound name + last touched parameter, "FILTER 1 CUTOFF   64"), yellow-green
  on a teal surround. The XT's UI is a 2x40 LCD (DISD in the SysEx spec is 80 characters).
- **Own motif, not a copy:** colours and general layout are the influence. The panel's own graphics are not copied:
  no Waldorf logo, no "microWAVE XT" script, no traced arc/swoosh artwork, no panel photos. Our plate art is drawn from
  scratch (a different line motif, e.g. a stepped wave outline). The product name doesn't use Waldorf or Microwave.
- Built with the browser renderer (`"art": "html"`, `art_css=`) and SVG plate art.
- Wave picture: a filmstrip rendered at build time from the **open** wave set only (section 10). Imported Waldorf waves
  don't appear in shipped images; the picture shows slot position (0-63) and the fixed tri/square/saw marks.
- Preview every page offline (`tools/studio.py preview`) before anything goes to a device.

## 10. Vendoring and the Waldorf material: how far each can go in a published zip

The catalog rule (PORTING.md section 5): public repo, SPDX licence, no closed binaries or copyrighted ROMs in the repo
or zip. Sorted by what that allows:

### Ships in the zip
| Item | Licence | Note |
|---|---|---|
| Our engine, skin, tools | GPL-3.0-only | Required as soon as any GPL code is vendored; matches Dexed/Acid |
| sst-filters, sst-basic-blocks (if used) | GPL-3.0 | Vendored under `src/vendor/` with `VENDORED.md` (commit, local changes) |
| simde | MIT | Only if sst code needs SSE on ARM |
| Tablor pieces (voice handling, file scan) | BSD-3-Clause | Keep its licence file |
| gearmulator snippets (ROM/OS parsing, value texts from `parameterDescriptions_xt.json`) | GPL-3.0 | Fine inside a GPL-3.0 port; credit in `VENDORED.md` |
| **An open wave set** | CC0 or GPL-3.0, ours | Original waves and 64-slot tables we make (additive, formant, PWM, sync sweeps, vocal-ish) filling the same table numbers with similar *character*, so any sound plays and the plugin is usable out of the box |
| **Original presets** | ours | A bank written for the open wave set. Factory XT sounds are not shipped |
| Behaviour constants from calibration | ours | Fitted filter curves, envelope and LFO rate tables, the table interpolation method: measurements of behaviour written as our own code, not copies of firmware data |

### Loaded at runtime from the user's own files (never in the zip)
| User file | What it unlocks |
|---|---|
| The Microwave II/XT **OS update** (`.mid`, from Waldorf's legacy download page), or a 256 KB ROM dump | The original waves and control tables, imported on the device |
| Factory and third-party **`.syx` sound banks** | The original presets, loaded as they are (section 2) |
| **Wave and table `.syx` dumps** (WAVD/WCTD) | User wavetables, including third-party MW wave banks |
| A dump from **the user's own hardware** (WAVR/WCTR requests, done by the desktop extractor) | Everything, for owners of an XT |

The importer runs inside the plugin (`import.c`, worker thread). The user drops the file into the plugin's `import/`
folder. The plugin parses it, extracts waves and control tables, builds the 128 tables with **our** interpolation and
algorithmic-table code, and writes a cache in `cache/` (user data: `release.py --user-data`, so upgrades keep it).
Then the wavetable popup shows the original tables and the status readout says "ORIGINAL WAVES: installed". This keeps
the zip free of Waldorf data and still gives a one-file, on-device setup.

What must be verified first, because the design depends on it:
1. **Does the OS update contain all waves and control tables?** gearmulator merges a `.mid` update into the upper
   128 KB of a full ROM, and the user-writable ROM waves start at `0x26501`, inside that half. Whether ROM waves 0-451
   and the control tables are there too is not known yet. If they aren't, the importer also needs a full ROM dump or a
   hardware dump, and the docs say so.
2. **Factory sounds inside the OS image.** gearmulator reads ROM singles for banks A and B. If they sit in the OS
   update, the importer can offer the factory banks from the same file.
3. **Our interpolation vs the firmware's.** Tested with the oracle: our table builder's output against the DSP
   wave memory dump, slot by slot. The dump itself is a test fixture on the developer's machine and is never committed.

### Never in the repo or zip
ROM or OS files; extracted or oracle-dumped waves and tables; factory `.syx` banks; recordings of factory presets
(including golden files for tests: CI regression runs only on the open set, the full factory comparison runs locally);
Waldorf logos, panel photos or trade dress; the words Waldorf or Microwave in the product name.

### Worth asking
Waldorf publishes the legacy OS files, manuals and sound banks for free download, but free to download isn't
permission to redistribute. One email asking Waldorf whether an open-source project may bundle the factory sound banks,
and possibly the ROM waves, costs nothing. A yes moves those rows into "Ships in the zip" with their notice; a no or
no answer leaves this design as it is. (None of this is legal advice; read the legacy page's own terms, which this
research couldn't reach.)

### Catalog entry
A normal published release (not `build-yourself`): `"license": "GPL-3.0-only"`, `release.py --repo --license`,
`catalog_check.py --catalog` OK, and a `requires` note such as "Optional: your Microwave II/XT OS update file for the
original wavetables; .syx banks for the original sounds".

## 11. Plan

| Phase | Deliverable | Gate |
|---|---|---|
| 0 | Oracle tool (x64, gearmulator `xtLib`), wave/table extractor, test-patch set; Tablor built through `adapters/schwung` and benched on a device | Extracted tables match Xenia's wave editor; pipeline proven on device |
| 1 | One voice: SDATA state, `.syx` loader, oscillators from extracted tables, mixer, amp env, 40 to 44.1 kHz resampler | Offline test (`tools/test_port.sh`), first device bench |
| 2 | Filter 1 (10 types) + Filter 2, filter env; external-input calibration | Filter response error within agreed tolerance |
| 3 | Wave env with loops, free env, LFOs, modifiers, 16-slot matrix, glide, allocation/unison, 10 voices | Preset regression scores on the factory banks |
| 4 | Parameters (SDATA order), CC map, skin pages (Osc, Wave, Mix, Filter, Env, LFO, Mod, FX, Arp) and Q-Link banks, preset browser, "save .syx" | Device test per PORTING.md section 4 |
| 5 | Effects (chorus + XT types), arp | Scores; bench PASS |
| 6 | Release: `release.py --repo --license`, `catalog_check.py --catalog`, `tested.json`, catalog entry | CLAUDE.md ground rules |

**Track B spike** (time-boxed, can run alongside Phase 0): (1) check whether the recompiler used for Monomodule and
Machinedrum targets the 56300 instruction set; (2) in the oracle, measure how much of the XT DSP's cycle budget
the firmware uses (the share of time in its idle loop); (3) estimate the 68k host cost under an interpreter on armv7.
Continue only if the numbers point at under 35% of one core (BENCH.md WARN). Otherwise Track A alone.


## Open questions
- Control rate and envelope curve shapes (answered by oracle item 2).
- Whether every algorithmic table is static, or some depend on per-voice state (oracle item 1 shows it).
- The XT effect list and its parameters (manual + oracle).
- Whether MPC delivers SysEx and CCs to a VST2 instrument (device test, record in NOTES.md).
- Gen2 `MPC` binary word size (`tools/probe_device.sh`), which decides whether gearmulator itself is an option there.

## Sources
- gearmulator: <https://github.com/dsp56300/gearmulator> (README platform list; `source/waldi/xt/xtLib/xtRomWaves.cpp`,
  `xtWavePreview.cpp`, `xtMidiTypes.h`, `xtRomLoader.cpp`, `xtHardware.cpp`; `xtJucePlugin/weData.cpp`,
  `parameterDescriptions_xt.json`)
- dsp56300 emulator: <https://github.com/dsp56300/dsp56300> (JIT back ends in `source/dsp56kEmu/`)
- Surge XT: <https://github.com/surge-synthesizer/surge>; sst-filters: <https://github.com/surge-synthesizer/sst-filters>
- schwung-tablor: <https://github.com/athousanddetails/schwung-tablor>
- FigBug Wavetable: <https://github.com/FigBug/Wavetable>
- Faust "firmware oracle" plan for the MW II/XT: <https://github.com/curlcomplex/Faust-expr/issues/101>
- Not reachable from the research environment (to read by hand): <https://theusualsuspects.io/downloads/xenia>,
  <https://waldorfmusic.com/legacy-microwave-ii-xt-xtk-series/>
