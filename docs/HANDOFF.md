# Handoff: where Clementine-XT stands and what is left (2026-10-02)

Read `CLAUDE.md` (ground rules), then `README.md` (what it is), `docs/DESIGN.md` (architecture), `docs/CALIBRATION.md` (every measurement, newest
at the bottom) and `docs/STATUS.md` (build commands). This file is the to-do list and the things that cost time to learn.

## 1. Where things are
| What | Where |
|---|---|
| MPC VST2 plugin (this repo) | https://github.com/sd88me/mpc-vst-clementineXT, checkout `~/mpc-vst-clementine`, branch `main` (pushed) |
| Schwung (Ableton Move) module | https://github.com/sd88me/schwung-clementineXT, checkout `~/schwung-clementineXT`. Its `engine/` is a copy of this repo's `src/`; fix engine bugs here, then run `scripts/sync_engine.sh` there |
| Framework | `~/mpc-vst-plugins` (build, skin, release, bench, catalog tools). Pinned ideas in its `docs/PORTING.md`, `docs/RELEASING.md`, `docs/NOTES.md` |
| Reference rig (local only, never committed) | `~/oracle`: the original firmware run offline through gearmulator against the user's own ROM; scripts and captures. Also `~/roms` (the ROM dump), `~/oracle/out/fs/` (256 factory sounds and the firmware's renders of them) |
| Test device | Akai Force at 192.168.1.44 (root ssh; `.claude/settings.local.json` has the allow rules). Plugin folder `/sdcard/Synths/sd88me - VST - Clementine-XT/`, user files in its `ROMS/` (`lower_/upper_Am29F010.bin`, `Factory.syx`) |

## 2. State today
- Version installed on the Force: **0.3.3-dev**. 183 of 248 comparable factory sounds are within 3 dB of the firmware in level, 221 within 6 dB; mean band error 13 dB
  (`~/oracle/fs_cmp.py`, reports in `~/oracle/out/fs_report*.txt`).
- Offline tests pass (`../mpc-vst-plugins/tools/test_port.sh vst/vst.json` and the `test/test_*.c` programs, see `docs/STATUS.md`). A release zip passes
  `catalog_check.py --catalog` (needs a plain `X.Y.Z` version).
- CPU on the Force (0.3.3-dev): p99 14.7 % at 8 voices, 14.5 % at 16, 25.1 % in the Q-Link sweep (verdict WARN). The sweep was 22 % before the new skin; cause not isolated.
- Skin: ten tabs (GLOBAL, SOUNDS, OSC, WAVE, FILTER, ENV, LFO ARP, MOD 1-8, MOD 9-16, MODIFIERS), written by `tools/gen_layout.py` (never edit `vst/layout.conf`;
  run `tools/make_layout.sh`). The user checked it on the device and said it looks better; knob value text and popup lists were adjusted twice already.
- Schwung module: builds for aarch64 and passes an ASan host simulation; **never run on a Move**.

## 3. Everyday commands
```
tools/make_layout.sh                                              # params.json + layout.conf (after editing gen_patch.py / gen_layout.py)
../mpc-vst-plugins/tools/test_port.sh vst/vst.json                # offline host test, must print PASSED
../mpc-vst-plugins/tools/build_port.sh vst/vst.json               # armhf .so + skin in vst/build/ (about 4 minutes, Docker)
cd vst && python3 ../../mpc-vst-plugins/tools/release.py --so build/clementine_xt.so --skin "build/skin/sd88me - VST - Clementine-XT" \
   --entry build/pluginlist-entry.xml --version X.Y.Z --user-data ROMS --about "..." --id clementine-xt \
   --repo sd88me/mpc-vst-clementineXT --license GPL-3.0-only [--bench build/bench.txt] -o /tmp/rel     # then catalog_check.py <zip> --catalog
scp the zip to the device, unzip, `sh install.sh -y` (stops and restarts MPC; ask the user first unless they just said to deploy)
../mpc-vst-plugins/tools/bench.sh vst/build/clementine_xt.so 192.168.1.44 -j
```
Quick engine iteration: `gcc -O2 -w -Isrc -I../mpc-vst-plugins/wrapper -o /tmp/rc test/render_cmp.c src/engine.c src/out.c src/patch.c src/syx.c src/waves.c src/wavedata.c src/filter.c src/fx.c src/mod.c src/presets.c -lm`, then `python3 ~/oracle/fs_cmp.py <sound ids>` (no ids: all 256, a minute or two).
After an installer run for a *different* plugin version the old zip's uninstaller removes the plugin-list entry too, so reinstall afterwards.

## 4. Remaining work, in priority order
### A. Release (the main goal)
1. Device smoke test of a build with a real version number (steps in `../mpc-vst-plugins/docs/RELEASING.md` step 5): add the plugin, play, turn every page and Q-Link, save and
   reload a project, run `uninstall.sh`. Check on the device: knob value text inside every panel, popup lists (play ASSIGN, matrix source/destination), the ten tab names in the MPC strip,
   the Play knobs and bank stepper, tile selection on SOUNDS.
2. Decide the version (0.4.0 or 1.0.0), re-run the bench, `release.py` with `--bench --repo --license --id`, `catalog_check.py --catalog`, tag `clementine-xt-vX.Y.Z`, `gh release create`.
3. README screenshots (the skin preview PNGs: `studio.py preview` needs Pillow, which is only in the `mpc-vst-html-art` Docker image; the page backgrounds are in `vst/build/skin/.../Plugin Skins/sh_bg_N.png`).
4. Investigate the Q-Link sweep CPU rise (22 % to 25 %); likely `refresh()` or `play_*` on every parameter write. Profile with the bench's sweep case.

### B. Engine accuracy (all optional; measure first, see section 6)
| Item | Notes | Worth |
|---|---|---|
| Filter 1 type 6 (waveshaper) | ours is 11-24 dB too loud for an oscillator input; the firmware's curve is not one static function of input. 14 factory sounds, about 3.5 dB off | low |
| Filter 1 type 12 (band stop) | up to 5 dB off; an LP + HP sum fits badly (3.5-5.5 dB rms), structure unknown | low |
| Wave tables 43, 46, 50, 51 | open stand-ins. 43 (sounds 12, 148, 218): not ROM, not an LFSR bit-plane. 46: ROM window (comb image 0xEB8A) plus an unexplained looping tail. 50/51: short waves plus zero slots. 30 is approximate | 43 medium |
| Tables 64-127 | uninitialised memory in the reference; 23 factory sounds cannot match | none |
| Individual sounds | 7 and 250 (arp patterns, about 10 dB), 227 (type 4 + table 51), 12, 235 (self-oscillation level), 152 (BP, 5 dB loud), 209 (sync + ring) | medium |
| Effects | wah sense/resonance guessed, Mod Delay depth, effect LFO start phase (found: the reference's LFOs free-run about 9.9 s before a capture) | low |
| Arpeggiator | hold mode, user pattern (the firmware ignored the user bytes in the rig), first step with reversed order, arp velocity source | low |
| Output stage | the firmware's steep roll-off above 19 kHz is not modelled; the Clipping parameter was never measured | low |
A systematic check across all sounds and parameters found more than any single sound did (amp chorus phase, panning matrix no-op, cutoff compression, arp ties, table generators).
Repeat that style: sweep one parameter on a plain sound with `~/oracle/tool-build/oracle render|ext` and compare `rc` output (scripts used so far are listed in section 5).

### C. Skin and usability
- Knob look is the default; the user liked the plate and panels. Possible polish: knob caps, LED toggles (`look=` options in `tools/skin_assets.py`), a coloured accent per tab.
- The Play ASSIGN popups are 83-entry lists; they fit only on the top row (the list opens below the field, in as many columns as it takes). Keep them there.
- Sound names for the 28 tiles come from the engine on each repaint; fine.

### D. Schwung module (its own repo)
See `~/schwung-clementineXT/README.md` "To do": run on a Move, validate the pages (Schwung's `tools/param-pages/validate.mjs` needs Node), catalog files and a first release.
The module rule that matters: nothing slow on the audio thread (`create_instance`, bank loads and `destroy_instance` all go through the worker).

### E. Housekeeping
- `docs/DESIGN.md` still has the original plan text in places (phases, candidate libraries); trim when releasing.
- The two repos share no code except the `engine/` copy: after every engine change here, sync there and run its `scripts/test.sh`.
- Keep `docs/CALIBRATION.md` as the log: add a paragraph per finding, with the numbers.

## 5. The reference rig (local only)
- `~/oracle/tool-build/oracle`: `render <sound.bin> <out.f32> <note> <vel> <blocks> <hold> [--set IDX VAL]...`, `ext <sound|-> <in.f32> <out.f32> <note> <blocks> noise|sine:<Hz>|impulse [--set ...]`
  (external input through the filters), `dump`, `tabledump`. Run from the ROM folder (`~/roms/Waldorf MicroWave II EPROMs`). Output is the left channel at 40 kHz. `--set IDX VAL` uses SDATA indices (`src/patch_tab.h`).
- `~/oracle/fs_cmp.py [ids]`: ours vs firmware for the 256 factory sounds (needs `/tmp/rc`, built as above). `tvs.py N` (level over time), `showsound.py N` (parameters and matrix), `pk.py` (spectral peaks).
- `~/oracle/out/tabdump.bin` (all 128 tables as the firmware built them) with `tabs.py`, `segs.py` (ROM window search), `bmtab.py` (Berlekamp-Massey), `emu.py`, `fir.py`, `morph.py` (how tables 45 and 47-49 were found).
- Filters: `tools/filter_compare.py` against `~/oracle/out/filt.tsv`; `/tmp`-style helpers written this session were not kept (fsweep, harm, sh5, dly, chor, fxfit): rewrite from `docs/CALIBRATION.md` if needed.
- Nothing from `~/oracle`, `~/roms` or the factory banks may be committed. `.gitignore` blocks `*.syx`, `*.bin`, `import/`, `ROMS/`.

## 6. Things that cost time
- The offline renders are deterministic only with a fixed oscillator start phase; with free phase two oscillators at the same pitch add randomly. Mute one oscillator or set the phase parameter for level tests.
- Output of the reference has about 78 samples of latency and a shelf (pole 280 Hz, zero 437 Hz); compare relative spectra or levels, not waveforms sample by sample.
- A level metric that looks at 8 bands is hypersensitive to anything that moves the fundamental (it found the chorus LFO phase). Look at band medians by feature (`chorus`, filter type, effect) to find systematic errors.
- The wrapper turns a list tile's text into a number: tiles answer `<key>_on` for their state. Popups add hidden `<key>__open` params after the port's own; params are append-only once released.
- `MODULE_SUBDIR "."` is the plugin folder itself. The skin renderer clips text to its widget; popups open under the field, else above, columns until the list fits, shifted left to stay on screen.
- Python in this environment has no numpy, Pillow or Node; the scripts use plain Python.
- Installing: the installer stops and restarts MPC and backs up `MPC.settings`; the previous version's uninstaller removes the shared plugin-list entry, so reinstall after uninstalling another version.

## 7. Ground rules (repeated because they are easy to break)
Never commit Waldorf ROM/OS files, extracted waves/tables, factory banks, recordings of factory sounds, logos or panel art. No "Waldorf" or "Microwave" in the product name, plugin id or skin art
(the README may say it is modelled on the Microwave II/XT and carries the disclaimer). Vendored code goes in `src/vendor/` with an entry in `src/VENDORED.md`. Every release must pass
`catalog_check.py --catalog`. Do not push, tag or release, and do not restart the user's MPC, unless they ask.
