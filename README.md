# Clementine

A wavetable synth for Akai MPC OS standalone devices (MPC Live/One/X/Key, Force), built as a native VST2
instrument with its own screen skin and Q-Links. It is modelled on the Waldorf Microwave II/XT: same voice
architecture, same sound-dump format, and it loads Microwave II/XT `.syx` sound banks as they are.

**Status: development build, plays on a Force.** It has run on an Akai Force (CPU bench WARN: 14.5 % p99 at 16 voices, 22 % in the
Q-Link sweep, docs/PERFORMANCE.md) and most of the sound engine is calibrated against the original firmware. See
[docs/DESIGN.md](docs/DESIGN.md) for the design and [docs/CALIBRATION.md](docs/CALIBRATION.md) for every measurement.

Working: the XT's 256-byte sound format (all fields, `.syx` single/bank import, save), the MIDI controller map, the wavetable oscillators
(real table data from the user's own ROM dump, measured mip levels and pitch, FM, ring mod, noise), amp, Filter 1 (all 13 types, 0-4, 7,
10, 11 fitted, the rest rough) and Filter 2, the wave/free/amp/filter envelopes, the modulation matrix with the modifiers, both LFOs, pan,
poly/mono/dual/unison voices, glide, all ten effects (calibrated), the arpeggiator (measured, host-tempo sync), 17 of the 24 algorithmic
wave tables 28-51, a Banks page (bank list and paged sound list; banks are the built-in sounds plus every `.syx` in `ROMS`), 12 built-in
presets on twelve open wave tables for use without a ROM, and an orange XT-styled skin. The original waves and tables are read from the
user's own ROM dump (two 128 KB halves or one 256 KB image in the plugin's `ROMS` folder, created on first load).

Not done: the noise-like algorithmic tables 43-51 (open stand-ins), the S&H and ramp modifiers and the modifier delay, Filter 1 types
5, 6, 8, 9, 12 (rough), a few effect details (Mod Delay speed/depth, wah sense), the arp user pattern and hold mode, and the catalog
release checks.

## Plan in one paragraph
A new C engine (not a ROM emulator; those need a 64-bit CPU) whose patch format is the XT's 256-byte sound dump,
running at the XT's 40 kHz internal rate with its stepped 8-bit wavetables, calibrated against an offline
reference that runs the original firmware on a desktop machine. The plugin ships with an original open wave set
and presets; the original waves and factory sounds are imported on the device from files the user supplies.

## What this repo does not contain
No Waldorf ROM or OS files, no waves or wavetables extracted from them, no factory sound banks, no Waldorf logos or
panel artwork. The plugin reads those from the user's own files at runtime (docs/DESIGN.md section 10).

## Building
Needs a checkout of [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) next to this repo (default `../mpc-vst-plugins`,
override with `MPC_VST`), Docker with QEMU for the armhf build, and Python 3.

```
tools/make_layout.sh                                   # params.json + skin layout (after editing tools/gen_patch.py)
../mpc-vst-plugins/tools/test_port.sh vst/vst.json     # offline host test under ASan
../mpc-vst-plugins/tools/build_port.sh vst/vst.json    # armhf .so + skin in vst/build/
gcc -O1 -Wall -fsanitize=address,undefined -o test/test_patch test/test_patch.c src/patch.c src/syx.c && test/test_patch
gcc -O1 -Wall -fsanitize=address,undefined -o test/test_waves test/test_waves.c src/waves.c -lm && test/test_waves
```

`tools/oracle/` is a dev-only harness that runs the original firmware (through a local gearmulator checkout and your own ROM) to
measure it; it is never shipped and its output stays off the repo (see `docs/DESIGN.md` section 4 and `tools/oracle/oracle.cpp`).

## Licence
GPL-3.0-only (see `LICENSE`). Vendored third-party code is listed in `src/VENDORED.md`.

Waldorf and Microwave are trademarks of their owners. This project is not affiliated with or endorsed by Waldorf.
