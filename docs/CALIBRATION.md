# Calibration log

Measured differences between our engine and the firmware (oracle). Renders are 40 kHz float, compared with
`tools/compare_render.py`; our side is `test/render_cmp.c`. Sound files stay local (`~/oracle/out`, gitignored by location).

## 2026-09-30: oscillator only (init sound and a saw variant, notes 36-84)
- **Pitch:** exact (0.0 cents, notes 48-84). Harmonic series of the saw slot follows 1/k at every note, so wave data, table lookup,
  rotation and mip levels are consistent with the firmware.
- **Level:** our placeholder gain (0.1125) matches at about note 60. Firmware level falls about 0.85 dB per octave of pitch
  (saw fundamental: -1.4 dB at note 60, -3.4 dB at note 84, relative to note 36); ours is flat. Cause unknown; the filter/output
  stage is not built yet, so check again after filter 1 and 2.
- **Spectral tilt:** firmware harmonics fall away faster than the wave's own by a factor that flattens with frequency
  (about R(523 Hz)/R(262 Hz) = 0.87, R(1 kHz)/R(523 Hz) = 0.92, R(2 kHz)/R(1 kHz) = 0.97), roughly a low-shelf of 2.5 dB.
  Not explained by mip level choice (every level was tried). Suspect filter 1 at cutoff 127 or the output stage; measure with
  the external input (oracle item 3) once filters exist.
- **Not compared yet:** envelope times (our curve is a placeholder), velocity, sync, FM, ring mod, noise, pitch bend, anything
  modulated. Mip level choice at high pitch (`osc_read`) is our own rule.

## 2026-09-30 (later): output stage, amp envelope, gain
Measured with noise through the external input (`tools/oracle ext`, `tools/filter_response.py`) and renders of a one-oscillator sound
(`cal.bin` = init sound with wave 2 muted; the init sound's free oscillator phase makes two-wave levels random per note).
- **Output shelf (all paths):** one pole at 280 Hz, one zero at 437 Hz, -3.86 dB at high frequencies, 0.04 dB rms fit to 13.7 kHz.
  This was the "level vs pitch" and "spectral tilt" of the first calibration. Steep roll-off above ~19 kHz is the firmware's own
  40 kHz output filter (-8.5 dB at 19.5 kHz, -30 dB at 19.9 kHz); we do not model it (our resampler cuts at 20 kHz).
- **Volume** is linear in v/127. **Pan** is piecewise linear in amplitude: 1.0 at hard left to 0.75 at centre, then linearly to 0
  at hard right. **Sustain** is linear in S/127.
- **Envelope:** attack is a linear ramp; decay and release are exponential (toward sustain / toward 0) with the same time
  constant for a given value: tau = 3.29 s * 2^((v-64)/8) up to v=72, then 7.9 s (80), 12 s (96), 24 s (112), effectively a hold at 127.
  Attack ramp times 0.03 s (16), 0.16 s (32), 0.49 s (48), 1.04 s (64), 1.9 s (80), 3.2 s (96), 6.5 s (112), 10.5 s (120).
  Decay toward a non-zero sustain matches an exponential within about 8%.
- **Output gain:** firmware/ours = -14.45 dB with our raw scale, constant within 0.15 dB from note 36 to 84 (`OUT_GAIN`).
- **Oscillator vs firmware after these:** one sine within 0.1 dB at every pitch; saw harmonics within 0.2 dB up to note 60 and within
  about 0.5 dB at note 72-84 once the mip level crossfades with limit 30 kHz (`MIP_LIMIT_HZ`).
- **Still open:** velocity to gain, pan keytrack, LFO/mod-matrix, effects, and free-phase behaviour of two waves.

## 2026-09-30 (filters)
Measured with `tools/oracle ext` (white noise through the external input, one filter at a time) and analysed with
`tools/filter_response.py` (Welch cross-spectrum); our side is `test/filter_run.c` compared by `tools/filter_compare.py`.
- **Structure:** Filter 1 low-pass types are second-order sections. 12 dB LP = one section, Q = 0.5 at resonance 0. 24 dB LP =
  a critically damped section (Q 0.5) followed by a resonant one. Fits are 0.15-0.2 dB rms with the TPT/prewarped-bilinear form.
- **Resonance:** damping 1/Q falls roughly linearly with the resonance value: Q = 0.51 (0), 0.68 (32), 1.10 (64), 1.59 (80), 2.99 (96),
  5.5 (104), 11.7 (108), then self-oscillation from about 110-113 (manual: above 113). `filt_damping` is a table of these.
- **Cutoff law:** for the critically damped case the effective pole frequency is 82 Hz at cutoff 32, 456 Hz at 64, 3.4 kHz at 96 and
  about 10.6 kHz at 112 (`POLE` in `src/filter.c`); the nominal semitone law (440 Hz * 2^((c-64)/12)) is 4% below it at 48-72 and
  grows to 1.5x at 112. At high resonance (110) the pole sits exactly on the nominal law, so the pole frequency depends on both
  cutoff and resonance in the firmware. We use the Q = 0.5 table for all resonances, which leaves 1-2 dB error at cutoff 72 and
  2-6 dB at cutoff 96+ with resonance. A (cutoff, resonance) table is the fix (data in `filt5.tsv`).
- **Types 2-12** (band-pass, high-pass, waveshaper, dual, FM, S&H, notches, band stop) are plausible structures with passband
  levels matched (BP -2.6 dB, 24BP -5.5 dB, HP -3.5 dB, sine shaper +9.5 dB, waveshaper about +16 dB, dual -5.6 dB relative to the
  LP passband); their shapes are several dB off (a plain SVF band-pass/high-pass does not match: the firmware's HP falls at only
  ~6-7 dB/octave below cutoff). Type 8 (FM) is noisy and unmodelled.
- **Filter keytrack:** exactly 1 cutoff unit per semitone at +100% (SDATA 96), pivoting on note 64; +197% (127) gives 1.97 units.
- **Filter envelope amount:** 2 cutoff units per amount step at full envelope (amount +16 gave +31.7 units); bipolar.
- **Filter 2:** one-pole 6 dB LP/HP with its own cutoff law (about 510 Hz at cutoff 32, 1.45 kHz at 56, 3.2 kHz at 80, 6.3 kHz at
  104, open at 127); HP gain sits about 4.5 dB (cutoff 32) to 24 dB (cutoff 127) below unity in the measured band. Not implemented
  to that law yet (`filter2_run` reuses Filter 1's table).
- **Mod matrix, first look:** the mod wheel (CC 1) had no effect through the oracle's MIDI input as tried; velocity as a source
  acted like a constant; volume modulation by a constant source is strongly nonlinear in the amount (nothing below about +32, then
  x1.97 at +63 from a base volume of 64). LFO 1 rate 64 runs at 1.05 Hz. These need a proper sweep before the matrix is built.

## 2026-10-01: filter tables
- **Resonant section (12 dB LP, and the resonant half of the 24 dB LP):** free (pole frequency, Q) fits of the firmware at 8 cutoffs x 5
  resonances are 0.15-0.3 dB rms, and both depend on both controls: Q at a given resonance is higher at high cutoffs (resonance 104:
  Q 3.0 at cutoff 40, 6.0 at 72, 11.3 at 112) and the pole is on the nominal law at high resonance but up to 1.5x above it at resonance 0
  and cutoff 112. `src/filter.c` interpolates those grids (`FPR`, `QTAB`). Result against the sweep: 12 dB LP 0.15-0.7 dB rms at every
  cutoff/resonance tested (mean 0.37 dB), 24 dB LP mean 1.5 dB (worst: cutoff 96+ with resonance, 2-4 dB).
- **24 dB LP** at resonance 0 is two critically damped sections; the second sits up to 1.9x above the first at cutoff 88-112 (`R24_V`).
  Below -70 dB the firmware is 2-4 dB less steep than two identical sections, which we ignore.
- **Filter 2** (one pole): pole frequency 108 Hz at cutoff 0, 510 at 32, 2 kHz at 64, 6.9 kHz at 96, open at 127 (`POLE2`).
- Deep stopband values from the measurement rig floor out near -110 dB; fits are restricted to points above -70 dB.

## 2026-10-01: modulation matrix and LFOs
Sent MIDI (bend, controllers) must follow the note: the firmware resets them at note-on (oracle `--acc`, `--abend`).
- **Amount law (all destinations tried):** the stored amount a (64 = 0) scales a full-scale source by 2 * sign * 2^((|a-64|-32)/4)
  native units: doubling every 4 steps, so amounts below about 16 do almost nothing. Verified for pitch (200 cents at +32, 800 at +40,
  3200 at +48 = 2 semitones x m), volume (+31.5 units at +48, saturating near 126), Filter 1 and Filter 2 cutoff (+1.9, +3.9, +7.9,
  +15.9, +31.9 units at +32..+48) and mixer level (+4.1, +8.1, +16.1, +32.6 units at +36..+48). Pan modulation had no effect in Sound mode.
  `mod_amount_gain` implements it; our pitch matches the firmware to within 1 cent at every amount tried.
- **Sources:** mod wheel (CC 1) = 127 -> 1.0; keytrack and keyfollow = (note - 64)/128 (amount +56 gives exactly (note-64) semitones on pitch).
  Pitch bend gives +-200 cents at a bend range of 2.
- **LFO:** rate in Hz = 0.02608 * 2^(rate/12) (0.416 Hz at 48, 1.051 at 64, 2.65 at 80, 6.67 at 96, 16.8 at 112, 40.0 at 127). Delay 0 free-runs;
  1..127 restarts the LFO at the note after 0.1 s per step (0.096 s for retrigger, 0.196 s at 2, 6.4 s at 64). Sine, triangle and
  square start at zero going up (square high first); the saw is a rising ramp that also passes through zero at the start. Rendered
  through volume the waveforms match the firmware to about 0.5%.
- **Not measured yet:** symmetry, humanize, sync (shared LFO), LFO 2 phase lock, random and S&H shapes, LFO level destinations, Modifiers,
  wave envelope, free envelope, the remaining destinations (reso, wave position, FM, envelope times).

## 2026-10-01: mixer sources
- **Noise:** linear in the mixer level; white noise shaped by a pole (2.5 kHz) and zero (12 kHz) after the output shelf is removed (within 1.5 dB
  of the firmware at 60 Hz-16 kHz); rms 0.0254 at level 127 through our chain (`NOISE_LEVEL` 0.756).
- **Ring modulator and two-oscillator mix** match the firmware within 0.5% (levels and the sum/difference tones).
- **Oscillator FM** is frequency modulation (sideband amplitude falls as 1/(modulator:carrier ratio), measured at 2:1, 3:1, 4:1):
  carrier frequency x (1 + k*w2) with k = 0.085*(amount/16)^2.8; first sidebands within 3% of the firmware for amounts 16-44. The carrier
  level differs (start-phase dependent) and amounts above about 48 are not compared.
- **Velocity to volume:** positive amounts match 1 - a*(1 - vel/127) with a = amount/64 (2.3% at vel 1 with +63). Negative amounts (which make
  loud notes quiet) follow a steeper law that is not modelled: at -64 the level reaches zero near velocity 100, not 127.

## 2026-10-01: wave position, wave and free envelopes
Slot identification (`/home/sam/oracle/fit/slotfind.py`, spectrum match against the table's slots) reads the firmware's played slot to +-1.
- **Wave keytrack** is exactly (percent) * (note - 64) slots: start 30 at +100% plays slots 2, 14, 26 at notes 36, 48, 60 (ours identical).
- **Matrix -> wave position** is 1 slot per m (not the 2 units of the other destinations): +1, +2, +4, +9, +17 slots at amounts +32..+48.
- **Wave envelope amount** moves the slot about 1.1 per step at full envelope (start 16: +17 at +16, +35 at +32, clamped at 63); velocity amount assumed alike.
- **Wave envelope segments** approach their level exponentially: 90% of a step takes 0.084 s at time 24, 0.35 s at 40, 1.34 s at 56 (doubling every
  8 steps): tau = 0.0365 s * 2^((t-24)/8). The rules for handing over to the next segment, for loops and for the free envelope are
  **approximations** (three time constants per segment); the firmware's sustain-end and loop behaviour was not resolved.
- **Resonance destination** = 2 units per m, like cutoff (checked at +36..+48).

## 2026-10-01: voices and glide
- **Dual (assign 1):** two voices, total detune spread 0.755 cents per detune step (+-48 cents at 127; measured peaks -45.7/+50.1 at 127).
- **Unison (assign 2):** ten voices about 33 cents apart at detune 127 (total spread 2.36 cents per step, +-150 cents); higher voices are quieter in the
  left channel because of the pan spread. De-Pan positions are a rough fit (unison leans left of centre); dual spreads symmetrically.
- **Voice level with several voices per note:** each voice about 0.73/sqrt(n): a coherent dual pair sums to 1.03x one voice, ten unison voices to 2.43x.
- **Glide:** exponential in pitch with a time constant of 2x the envelope decay constant for the same value (0.13 s at 20, 0.8 s at 40); linear
  glide covers an octave in roughly 1.6x that constant (3.2 semitones/s at value 60). The first note after start glides from an unknown
  default pitch; ours starts on pitch.
- **Oracle:** `--stereo` (interleaved L,R) and `--n2 NOTE BLOCKS` (a second key while the first is held) added to `render`.

## Modifiers (partial)
Rig: modifier 1 on modwheel (A) and breath CC2 (B) or velocity, routed to volume through a matrix slot; r is the level change
in units of the full-scale volume effect. Findings (A, B, parameter P all 0..1):
- `*` = A*B. `XOR`/`OR`/`AND` act bitwise on the 7-bit values (/128). `abs` = |A|. Type 12 (listed "max") returned A whatever B was.
- `+` wraps like a signed byte: A+B >= 1 gives A+B-2; below 1 it reads full scale (+1). `-` gives A-B when negative, +1 otherwise.
- `Switch` gives 1 when A >= P (equality at .787 gave 1), else 0. Not yet separated from a fixed threshold of 0.5.
- Type 11 ("min") depends on P only: 2P wrapped into -1..+1. `/` stayed at 0.01-0.03.
- Not measured, implemented from the manual as guesses: S&H, ramp, lag, filter, differentiator, modifier delay (source 24).
The type numbering or the manual's names may be off for 11 and 12; the behaviour is reproduced as measured.

## Arpeggiator (measured) and trigger modes (not measured)
Arp step length = the Clock value in beats at the Arp Tempo: clock 0-12 = 1/1, 1/2., 1/2T, 1/2, 1/4., 1/4T, 1/4, 1/8., 1/8T, 1/8, 1/16.,
1/16T, 1/16 (13-15 are faster than the rig resolves and are taken as 1/32T, 1/32, 1/64T). Tempo 1..127 = 50..300 BPM (127 gave exactly
300: a quarter-note clock step 0.200 s), within about 1 % over the range; 0 is "extern" (host tempo). The gate closes 7.6 ms before
the next step (constant over tempo and clock). Preset rhythms 1-15 are the 16-step masks in `ARP_PRESET` (pattern 0 plays every step);
a rest does not consume a note of the sequence. Order: the notes ascending (note) or as played; "n.rev"/"p.rev" reverse the whole
expanded octave sequence (60 79 72 67 for the notes 60/67 over two octaves). Directions up, down, alternate (ends not repeated) and
random behaved as implemented. Not matched: the first step with a reversed order (the firmware starts on the lowest held note),
hold mode, the user pattern (pattern 16 ignored the four user bytes in the rig and played a fixed 12-step pattern),
and the arp velocity source. Envelope triggers apply on legato mono notes (unmeasured).

## Effects (partial)
Index numbering (oracle, all 36 indices, steady note): 0 off, 1 Chorus, 2 Flanger 1, 3 Flanger 2, 4 AutoWahLP, 5 AutoWahBP,
6 Overdrive, 7 Amp Mod, 32 Delay, 33 Pan Delay, 34 Mod Delay; every other index leaves the sound untouched (factory sounds use
8 and 9 too, so those are probably older II types that the XT ignores). The first six assignments follow the manual's order;
4 and 5 are inferred (a steady 261 Hz tone shows little change for 4).
Delay (32): echo time 0.12 s * 2^((p1-64)/36) (35 ms to 0.40 s, independent of the tempo setting); repeat ratio 0.744*p2/127;
mix p3 linear dry/wet (0 = dry). Pan Delay (33): same time and mix; the first repeat is on the right, the second left, and
the feedback closes after two hops. Mod Delay (34): one repeat at about the same time law, dry and wet both ~0.5; speed follows the common effect LFO law (0.0167*2^(p/12) Hz) and the depth is about +-4 ms (from the pitch shift of a tone); speed and
depth modulate it (not calibrated; our sine LFO ranges are guesses). Chorus, flangers, wahs, overdrive and amp mod remain
uncalibrated apart from knowing that mix 0 is dry and that overdrive gain 0 is silent.

Overdrive (6): output = 6.5*g/(50+1.57*p1) * clip((1+p1)*x, +-0.19), x in firmware output units, g = p2/64 up to 64 and
1+0.874*(p2-64)/63 above it. The third parameter ("amp type") changed neither level nor harmonic content in any test, so it is
ignored. Checked against the firmware for four drive/volume settings: rms within 3% (hard knee; the real one is a little softer).

Amp Mod (7): LFO sine at 0.0167*2^(p1/12) Hz (measured 0.68 Hz at 64, 4.25 Hz at 96); the right side runs p2/127 of half a cycle
later; out = dry*x + wet*x*sin with linear mix (mix 127 is pure ring modulation, mix 64 a tremolo to zero).
Chorus (1), Flanger 1 (2), Flanger 2 (3), measured by following the echo lag of white noise (oracle): same sine LFO law as Amp Mod;
delay 128 samples*(1+depth*sin) (chorus), depth*128*(1+sin) (flanger 1), 128*(1+sin) (flanger 2, where p2 is feedback); the right
side is half a cycle away; chorus/flanger 1 wet = (x + delayed)/2 (rms ratio 0.70 for uncorrelated noise), flanger 2 wet = delayed/2.
Flanger 2 feedback gain is a fit (0.83*p2/127) to the rms growth. Still open: the LFO phase at note start, the wahs, and the
always-available chorus (amp page), which is untouched.
Amp-page chorus (1 and 2 are identical): one tap, delay 128*(1+sin) samples at 0.5 Hz, sides half a cycle apart, added at full level to the dry signal.

AutoWah (4 = LP, 5 = BP), steady noise: cutoff = 62.5 Hz * p2 (p2 0 is silent), 12 dB/oct slopes. The sense term and resonance law are not calibrated.

## Filter 1 types 2-12 (2026-10-01)
Fitted against `filt.tsv` (white noise through the external input, 4 cutoffs x 3 resonances per type; `tools/filter_compare.py`,
`ABS=1` compares levels against the 12 dB LP's -19.1 dB passband). Mean rms error, old -> new: 24 dB BP 26.7 -> 3.9 dB, 12 dB BP 4.3 -> 4.5*,
12 dB HP 12.6 -> 5.4 (2 dB up to cutoff 96), 24 dB notch 5.6 -> 4.3, 12 dB notch 3.5 -> 2.1, dual 6.3 -> 4.0.
(*the BP mean is dominated by the firmware's steep output filter above 9 kHz, which we do not model; below that it is within 1 dB.)
- Type 3 (12 dB BP) is twice the raw band-pass output of the 12 dB LP's (pole, Q) section (peak gain 2Q).
- Type 4 (12 dB HP) is that section as a high-pass followed by a fixed critically damped 2-pole LP near 12.5 kHz.
- Type 2 (24 dB BP): one-pole HP and the resonant 2-pole LP at 0.745x the LP's pole, then a critically damped LP at 4.7x the pole;
  level +4.5 dB at cutoff <= 72 rising to +12 dB at 120 (vs the LP).
- Type 11 (12 dB notch) is the section's own notch at half level (-6 dB passband); type 10 multiplies a wide critically damped notch at
  0.95x pole by the section's notch, unity passband.
- Type 7: half the LP plus the raw BP of a section moved (special - 64) steps. Still too high above the BP peak (firmware falls faster).
- Not fitted: type 12 (band stop; only special 64 was measured, the notch centre sits about 3x the pole at cutoff 72 and 5x at 48, with
  -6 dB below and +5 dB above), type 9 (S&H), and the non-linear types 5, 6, 8, whose levels depend on the input.

## Modifiers, timed types (oracle `--acc0`: a controller step with the capture starting at the step)
- Lag (13): linear ramp to source 1 at 2.09 units/s for parameter 64, doubling every 11 steps (parameter 20: 0.13/s, 100: > 10/s).
- Filter (14): one-pole low-pass, time constant about 40 ms at parameter 100, 15 ms at 64, a few ms at 20 (0.040*2^((P-100)/25) s).
- Differentiator (15): a short pulse at the step, then 0; the scale (0.0125/dt) is a guess.
- S&H (7): the source is sampled every 1.2 s * 2^((60 - P)/12) (2.1 s at 50, 0.12 s at 100; a sample is taken at note start).
- Ramp (8): rises linearly while the source is above half, full scale in 0.34 s * 2^((70 - P)/12.2), and drops to 0 when the source falls.
- Types 11 and 12 (the manual's "min" and "max") behave as: 11 = a constant from the parameter (P/64, wrapping at 1 to -1), 12 = source 1.
  (Earlier notes had the two swapped.)
- Control Delay (source 24, SDATA 174/175): the chosen source delayed by 12.6 ms per Time step (1.6 s at 127), measured with a
  controller step.

Arp tempo: 0 ("extern") follows the host tempo, which the wrapper passes in as `lfo_bpm` (built with -DHAS_LFO_BPM=1); 1..127 map to 50-300 BPM. The note values for the Arp Clock are guesses (see above); the engine does not yet follow the host transport (start/stop/position).

## Algorithmic tables 28-51 (rebuilt from observed output)
The firmware computes these with code; none of their 61 waves matches any ROM wave. They were studied through the oracle's DSP-memory
capture (never shipped) and rebuilt in `src/waves.c` `algo_table()`. Every wave is stored as 64 samples and mirrored (second half = the
negated reverse, -128 saturating to +127); tables 32-40 also taper samples 48..63 by (63 - i)/16 (rounded).
- **Exact, sample for sample (all 61 slots, including the mips): 28, 29, 32, 33, 34, 35, 36, 37, 41, 42.**
  28: floor((70 + 8s) i / 64) mod 64. 29: 32 for 64-s samples then s zeros. 41: 127 for 60-s samples then -128. 42: a ramp 2i that
  resets after 60-s samples. 32-34: saw sweeps, ((2 m i + max(2, m - 1/64)) mod 256) - 128 with m = 2 + s/30, 2 + s/10, 2 + 7s/30.
  35-37: square waves of m = 1 + 3s/60, 1 + 7s/60, 1 + 15s/60 cycles.
- **Close (rms error about 1 of 128): 38-40** (128 sin(2 pi (m + 0.04)(i + 0.5)/128), m = 1 + s/8, 2 + s/4, 4 + s/2) and **31** (keyframes at
  slots 0, 30, 60 blended; the firmware's blend in between is slightly different).
- **Not rebuilt (open-table stand-ins): 30 and 43-51**, noise-like and plucked-string families (43/44 are a sliding window over a fixed
  noise sequence).
76 of the 256 factory sounds use tables 28-51; 66 of them use rebuilt ones.

Self-oscillation (filter resonance above about 111): the firmware's filter rings on its own at note start (the kick sounds 235-238 produce all their output this way, at a constant or slowly decaying level). We strike the first section with a state of 2.8 and add a little noise; 236 now matches in level, 235 (a ring at constant amplitude) does not.
