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
