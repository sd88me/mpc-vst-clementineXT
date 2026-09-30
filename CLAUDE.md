# Clementine: agent guide

MPC OS VST2 wavetable instrument modelled on the Waldorf Microwave II/XT. Start with `docs/DESIGN.md`, then
mpc-vst-plugins' `CLAUDE.md`, `docs/NOTES.md` and `docs/PORTING.md` (checked out next to this repo as `MPC_VST`).

Ground rules:
- Never commit Waldorf ROM/OS files, extracted or dumped waves/tables, factory `.syx` banks, recordings of factory
  sounds, or Waldorf logos/panel artwork. Test fixtures derived from them stay on the developer's machine.
- Don't use "Waldorf" or "Microwave" in the product name, skin art or plugin id.
- Vendored code goes in `src/vendor/` with an entry in `src/VENDORED.md`.
- Every release must be catalog-conformant (`release.py --repo sd88me/mpc-vst-clementine --license GPL-3.0-only`,
  `catalog_check.py --catalog` OK).
