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
