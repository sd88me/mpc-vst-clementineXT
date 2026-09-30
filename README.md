# Clementine

A wavetable synth for Akai MPC OS standalone devices (MPC Live/One/X/Key, Force), built as a native VST2
instrument with its own screen skin and Q-Links. It is modelled on the Waldorf Microwave II/XT: same voice
architecture, same sound-dump format, and it loads Microwave II/XT `.syx` sound banks as they are.

**Status: design stage, nothing to install yet.** See [docs/DESIGN.md](docs/DESIGN.md).

## Plan in one paragraph
A new C engine (not a ROM emulator; those need a 64-bit CPU) whose patch format is the XT's 256-byte sound dump,
running at the XT's 40 kHz internal rate with its stepped 8-bit wavetables, calibrated against an offline
reference that runs the original firmware on a desktop machine. The plugin ships with an original open wave set
and presets; the original waves and factory sounds are imported on the device from files the user supplies.

## What this repo does not contain
No Waldorf ROM or OS files, no waves or wavetables extracted from them, no factory sound banks, no Waldorf logos or
panel artwork. The plugin reads those from the user's own files at runtime (docs/DESIGN.md section 10).

## Building
Needs a checkout of [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) next to this repo (`MPC_VST`);
build steps will follow the port layout there (`vst/vst.json`, `tools/build_port.sh`).

## Licence
GPL-3.0-only (see `LICENSE`). Vendored third-party code is listed in `src/VENDORED.md`.

Waldorf and Microwave are trademarks of their owners. This project is not affiliated with or endorsed by Waldorf.
