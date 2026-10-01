# CPU on the device

Measured on an Akai Force (Cortex-A12, 32-bit, `tools/bench.sh`, 2902 us per block at 44.1 kHz / 128 frames):

| Build | 1 / 4 / 8 / 16 voices p99 | Q-Link sweep p99 | Verdict |
|---|---|---|---|
| First build (per-sample modulation, libm in the voice loop) | 70 / 74 / 77 / 77 % | 90 % (spikes to 425 %) | FAIL |
| Control-rate voices, cached coefficients, prebuilt tables | 17 / 19 / 19 / 19 % | 27 % | WARN |
| + masked mip reads, fast log2, control-rate wave/free envelopes | 13.6 / 14.5 / 14.7 / 14.7 % | 22 % | WARN |

What mattered on the ARM devices, where `lroundf`, `floorf`, `powf`, `tanf`, `tanhf` and `expf` each cost 20-110 ns:
- Everything slow-moving (matrix, LFO rates, envelope times, pitch, wave slots, filter controls, pan, gain) is computed every 8
  samples per voice (`voice_control` in `src/engine.c`); the per-sample path reads the results. Envelopes, glide, oscillators,
  mix, filters and amp stay per sample.
- Filter coefficients, LFO frequencies, envelope and glide coefficients come from caches or 128-entry tables.
- Every table (wave tables, filter grids, envelope tables) is built when the plugin is created, never on the audio thread
  (a wavetable change in the first build took 11 ms).
- Denormals are flushed to zero while rendering.

Per voice-sample the device spends about 300 ns (10 voices of a typical sound = 11-13 % of a block plus about 2.5 % for the
resampler). The heaviest single features are oscillator FM (+27 %), the 24 dB band-pass (+17 %), noise (+9 %) and the wah,
chorus and flanger effects (+5-9 %). `-mcpu=cortex-a12 -mfpu=neon-vfpv4 -ffast-math` would save another 10 % but ties the
build to that CPU; it is not used.

Bench of 0.3.3-dev (2026-10-02, Force, `tools/bench.sh`): 8 voices p99 14.7 % (max 16.8), 16 voices p99 14.5 % (max 19.3), Q-Link sweep p99 25.1 % (max 28.0), release tail 6.3 %;
verdict WARN (worst p99 25.1 %, no background threads). The sweep rose from 22 % with the larger parameter list of the new skin and the delay table / chorus changes cost nothing
measurable in the sustained cases.
