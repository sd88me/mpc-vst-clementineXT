# Clementine-XT

A wavetable synthesizer for Akai MPC OS standalone devices (Force, MPC Live / Live II, One, X, Key 61), built as a native VST2
instrument with its own screen skin and Q-Link pages. It plays the way the Waldorf Microwave II and Microwave XT did: two wavetable
oscillators with FM, sync and ring modulation, thirteen filter types, a 16-slot modulation matrix with modifiers, an arpeggiator and a
chain of effects, in ten voices at the original's 40 kHz internal rate. It loads the instrument's own `.syx` sound banks.

*Clementine-XT is an independent project. The XT in the name is a nod to the Microwave XT (and, like Surge XT, reads as "extended").
It is not affiliated with or endorsed by Waldorf. See [Acknowledgements](#acknowledgements-and-legal).*

## What you need

- A first-generation MPC OS standalone device (32-bit ARM): Force, MPC Live / Live II, MPC One, MPC X or MPC Key 61. Tested on a Force
  running MPC OS 5.0.17; the others are untested.
- Root (SSH) access to the device. Installing a plugin this way is unofficial, so back up first and use it at your own risk.
- **Optional but recommended: your own copy of the Microwave II ROM.** The plugin does not include one (see below). Without it you get
  12 built-in sounds on an original set of wave tables; with it you get the real waves and can load the instrument's factory banks.

## Install

1. Download `Clementine-XT-<version>-mpc-armv7.zip`, unzip it and copy the folder to the device:
   `scp -r Clementine-XT-<version> root@<device-ip>:/tmp/`
2. Run the installer: `ssh root@<device-ip> sh /tmp/Clementine-XT-<version>/install.sh`
   It stops MPC (save your project first), copies the plugin to `/sdcard/Synths/sd88me - VST - Clementine-XT/`, backs up `MPC.settings`,
   adds the plugin to MPC's list and starts MPC again. Running it again upgrades in place and keeps your files.
3. On a track, add **Clementine-XT** from the plugin browser (Instruments). The screen appears in the plugin view and the Q-Links
   follow the page.

`INSTALL.md` in the zip has the manual route and how to uninstall.

## Your ROM and sound banks

The plugin makes a folder called `ROMS` inside its own folder on first load
(`/sdcard/Synths/sd88me - VST - Clementine-XT/ROMS/`). Everything of your own goes there; upgrades and uninstalls leave it alone.

- **Waves and wave tables:** copy your Microwave II ROM dump into `ROMS`, either the two 128 KB chip images (for example `lower_Am29F010.bin`
  and `upper_Am29F010.bin`; any `.bin` names work) or one 256 KB image. Restart the plugin (add a fresh
  instance) and the 506 original waves and the factory wave tables load. Tables 28–51 are computed by the plugin itself, so they
  work whatever the chips hold.
- **Sound banks:** copy any Microwave II/XT `.syx` file (a single sound or a whole bank dump) into `ROMS`. Each file appears as a bank on the
  SOUNDS tab, named after the file (a file called `Factory.syx` is the bank "Factory").
- **No ROM:** the built-in bank (12 sounds) plays on an original open set of wave tables. Sounds that use the original tables will sound
  different from the instrument.

You must own the instrument or have the right to use its ROM. The project does not provide it and will not help find it.

## Using it

The skin has ten tabs. Each tab has Q-Link sub-pages (the name of the first one shows in the tab strip); the Q-Link knobs always control
the page you are looking at.

| Tab | What is on it |
|---|---|
| **GLOBAL** | The four **Play knobs** and what each one controls, the bank and sound steppers with names, effect type and its three parameters, voice mode / assign / detune / de-pan, glide, volume, pan and chorus |
| **SOUNDS** | A list of banks and a paged list of 28 sounds (sounds run down the columns); tap a sound to load it, **Prev / Next** change the page |
| **OSC** | Oscillators 1 and 2 (octave, semitone, detune, bend, keytrack, FM, sync, link), the wave table, aliasing, time quantisation, clipping, accuracy |
| **WAVE** | Wave 1 and Wave 2 (start wave, phase, envelope amount, velocity, keytrack, limit) and the mixer (oscillators, ring mod, noise, external input) |
| **FILTER** | Filter 1 (13 types: 24 dB LP, 12 dB LP, 24 dB BP, 12 dB BP, 12 dB HP, sin(x)→LP, waveshaper, dual, FM filter, S&H, notches, band stop), Filter 2, filter envelope, amp envelope, amp velocity and keytrack |
| **ENV** | Wave envelope times, levels and loops, and the free envelope |
| **LFO ARP** | Two LFOs and the arpeggiator |
| **MOD 1-8, MOD 9-16** | The modulation matrix: each slot has a source, a destination and an amount |
| **MODIFIERS** | The four modifiers (maths and lag/filter/ramp/S&H functions on two sources) and the control delay |

**Play knobs.** On the Microwave, every sound chooses four parameters to put under four knobs for performance. The four PARAMETER
popups on the GLOBAL tab choose them, and the four knobs next to them turn whichever parameter you picked, over its whole range
(also from the Q-Links of the GLOBAL page). Controls W–Z can be assigned too; they are live controller values for the matrix.

**MIDI.** Notes, pitch bend, mod wheel, aftertouch, sustain, breath and foot follow the XT's own controller map; most sound
parameters answer to their Microwave controller numbers. With the arpeggiator tempo set to extern it follows the MPC's tempo.

**Saving.** The sound you are playing is stored with the MPC program and the project, so a project reloads as you left it. There is
no separate "save preset" button in the plugin; MPC offers none for this kind of plugin.

## Performance

On a Force, a dense-chord benchmark used about 15 % of the CPU at its busiest moments (about 22 % while sweeping the Q-Links), which
the benchmark script rates as a warning, not a problem. See [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Known limitations

- Four of the 24 computed wave tables (43, 46, 50 and 51) and the user tables are stand-ins, and some of the filter types
  (waveshaper, FM, S&H, band stop) are approximations. About three quarters of the factory sounds are within 3 dB of the original in level.
  The measurements are in [docs/CALIBRATION.md](docs/CALIBRATION.md).
- Several effects are approximate (wah sensitivity, mod delay depth), and the arpeggiator's hold mode, user pattern and a few edge cases
  differ from the original.
- Free-running oscillator phase makes two oscillators at the same pitch add up differently from note to note, as on the original, but not
  the same way in every sound.
- MPC OS 5 only on a Force so far, and VST2 only.

## Troubleshooting

- **The plugin doesn't appear after install:** restart MPC and check that `MPC.settings` lists the plugin; the installer's backup is
  next to it. Run `install.sh` again.
- **The waves sound plain / the factory banks are missing:** the ROM or `.syx` files are not in `ROMS`, or the ROM files are not the
  expected size (128 KB each, or 256 KB combined). Add a fresh instance of the plugin after copying them.
- **Old projects lose the plugin after an upgrade between very different versions:** the parameter list grew during development; add the
  plugin again on the track.
- **An option list runs off the screen:** report which one; the popup lists open under the field, so controls near the bottom of a tab
  open upward.

## Acknowledgements and legal

Clementine-XT exists because of the Waldorf Microwave II and Microwave XT. Its sound engine is original C code, measured against the
instrument's behaviour, and it reads the instrument's waves from your own ROM at runtime. **No ROM, wave, sound bank, logo or panel art
of the original is included in this repository or its releases.** Waldorf and Microwave are trademarks of their owners. This project
is independent and not affiliated with or endorsed by Waldorf.

Built on the [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) framework for native MPC OS plugins.

## Other platforms

A Schwung (Ableton Move) version is started in [schwung/](schwung/README.md): it builds and passes a host simulation, but has not run on a Move yet.

## Building from source

See [docs/STATUS.md](docs/STATUS.md) (development notes and build commands), [docs/DESIGN.md](docs/DESIGN.md) and
[docs/CALIBRATION.md](docs/CALIBRATION.md). You need a checkout of mpc-vst-plugins next to this repo, Docker with QEMU for the armhf build
and Python 3.

## Licence

GPL-3.0-only (see `LICENSE`). Vendored third-party code, if any, is listed in `src/VENDORED.md`.
