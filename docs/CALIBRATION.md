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
- **Still open:** velocity to gain, pan keytrack, filter 1/2 (next), LFO/mod-matrix, effects, and free-phase behaviour of two waves.
