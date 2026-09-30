# Behaviour notes from the user's manual

Facts about how the II/XT/XTk behaves, taken from the owner's manual (which covers all three) and written in our own
words, so the engine and skin can be checked against them. The manual itself is Waldorf's and is not in this repo.
Where the SysEx spec, the manual and the firmware disagree, the firmware wins (see DESIGN.md errata).

## Waves and tables
- 65 ROM wavetables exist (1-65), 66-96 are reserved, 97-128 are user tables (SDATA index 25 is 0-based).
- Waves: ROM 000-299, user 1000-1249. A table is 64 references; empty slots are a weighted crossfade of the nearest
  filled neighbours (50/50 in the middle of a one-slot gap, 2/3-1/3 across a two-slot gap): a plain linear blend by
  position. At least 5 references exist per table: the first, plus the last four (slot 60 and the fixed 61-63).
- Slots 61-63 are triangle, 50% square and saw in every table. Wave `Limit` stops modulation reaching them.
- Most waves store only half a cycle (mirrored and negated); full cycles are possible (`w[64+n] = -w[63-n]` is the
  stored form for half-cycle waves; whole-cycle waves are why pulse-width tables work).
- "Time Quantization" overrides the wave interpolation at low pitches (five steps); Aliasing off/1-5 lets a wave's
  harmonics fold back; a wave normally represents 64 harmonics. Accuracy off detunes voices very slightly.
- Clipping applies when the mixer inputs (wave 1, wave 2, noise, ringmod) sum above 128: saturate limits, overflow
  negates the excess.

## Pitch
- Reference: MIDI note 69 (A3) gives the Tune frequency (440 Hz) with octave, semitone, detune 0 and keytrack 100%.
- Detune is in 1/128 semitone. Keytrack pivots on note 64 (E3); +100% is 1:1. Octave -4..+4 in steps of 12.
- Osc 2 Sync: osc 1 is master, restarts osc 2 each period. Link: osc 2 uses osc 1's modulation settings. FM Amount is
  osc 2 modulating osc 1's frequency. Pitch bend range 0-120, "harmonic" (harmonic/subharmonic scale) or "global".
- Wave keytrack: +100% means one table slot per semitone away from E3 (start wave 29 plays wave 30 on F3).
- Phase: "free" randomises the start phase per note, else 3-357 degrees.

## Filters
- Filter 1 types 0-9 as in the SysEx list; **10 = 24 dB notch, 11 = 12 dB notch, 12 = band stop** (extra parameter
  "Bandwidth"; 0 makes it equal to the 12 dB notch). Extra parameter (SDATA 70) by type: WaveShapr = shaping wave,
  Dual L/BP = band-pass offset in semitones, FM-Filter = osc 2 FM amount, S&H = rate (127 passes the signal untouched).
- Cutoff 64 with resonance 114 self-oscillates at 440 Hz; cutoff is scaled in semitones. Resonance above about 113
  self-oscillates. Notch types show no resonance until self-oscillation.
- Filter 1 -> Filter 2 in series. Filter 2 is 6 dB LP/HP, no resonance. Keytrack pivots on note 64.
- Envelope amount and velocity amount **add** to form the cutoff modulation.

## Amplifier, pan, glide, trigger
- Amp envelope always modulates volume; velocity amount scales the envelope with velocity (negative inverts).
- Chorus: two short delays modulated by a ~0.5 Hz sine. Pan: -64..+63, keytrack pivots on note 64.
- Glide types porta / gliss / fingered / fingered gliss (the last two only on legato notes); exp or linear; time 0-127.
- Envelope trigger per envelope: normal (per voice), single (all voices act as one) or retrigger (single, restarting from
  the current value). For the amp envelope single/retrigger only apply in mono.
- Poly 10 voices. Dual = two voices per note detuned +-Detune/2. Unisono = all 10 voices split over the held notes.
  De-Pan spreads the stacked voices across the stereo field (0 none, 127 full).

## Envelopes and LFOs
- ADSR times are rates 0-127. Wave envelope: 8 time/level pairs; loop points 1-8 name segment ends, so segment 1 cannot
  loop. Key-on loop end is also the end of the sustain phase (even with the loop off); key-off loop end is the last
  segment used. Free envelope: 3 segments + release, levels bipolar (-1..+1), no loops.
- LFO: sine, triangle, square, saw, random, S&H; rate 0 with S&H draws a new value per note. Delay: off, retrigger, or
  1-126 (retrigger, delayed). Sync off / on (all voices share one LFO) / clock (LFO 1 only). Symmetry -64..+63
  (pulse width for square; slope for triangle). Humanize off/1-127 varies the speed. LFO 2 Phase off/3-357 locks it to
  LFO 1.

## Modifiers and matrix
- Four modifiers, each on two sources and a parameter; results lie in -1..+1. Types: + - * / XOR OR AND, S&H (sample
  source 1 at intervals set by the parameter), ramp (triggered on a rising edge, rise time = parameter), switch (max
  when source 1 exceeds the parameter), abs, min, max, lag (linear ramp to source 1 over the parameter time), filter
  (low-pass, cutoff = parameter), differentiator. A separate delay line delays one source.
- 16 matrix slots: source x amount (-64..+63) to a destination. Unipolar sources: the three envelopes, all MIDI
  controllers, velocity, release velocity, aftertouch, poly pressure, MIDI clock. Bipolar: free envelope, both LFOs,
  keytrack, keyfollow, pitch bend. For keytrack/keyfollow an amount of +56 is 100% of the scale. Keyfollow is keytrack
  including pitch bend and glide. `F1 Extra` is the destination for filter 1's extra parameter; `FM Amount` is oscillator FM.

## Arpeggiator
- Holds up to 20 notes; range 1-10 octaves; 15 preset rhythm patterns plus a user pattern of 1-16 steps; directions up,
  down, alternate, random; note order by note, reversed, as played, played reversed; velocity from root note or last note.
  Clock values include triplets and dotted values (the exact SDATA value order is not yet confirmed).
- Tempo is "extern" or 50-300 BPM; hold mode keeps the arpeggio after release. Runs per sound (the multi's own arp can
  override it in Multi mode, which we do not emulate).

## Play access and effects
- Four play parameters per sound pick any of the 83 listed parameters (SysEx list 3.11); Controls W-Z appear as the
  last four entries. Effects: see DESIGN.md section 2 (10 documented types; factory sounds also use indices 32-34).

## Measured on the firmware (oracle `render`)
- Init sound, notes 48/60/72/84: fundamental within 0.1 cent of standard MIDI pitch (note 69 = 440 Hz), so oscillator keytrack
  SDATA 48 is exactly +100%. Oscillator keytrack is therefore `-100% + 300% * v / 72` (v 0..72; the spec's range 0..76 runs a
  little past +200%). Filter, wave and amp keytrack (0..127) are `(v - 64) * 3.125%` (96 = +100%).
