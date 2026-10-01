#!/usr/bin/env bash
# Regenerate vst/params.json and vst/layout.conf (theme header + the pages of tools/gen_layout.py).
# Needs a checkout of mpc-vst-plugins next to this repo (MPC_VST, default ../mpc-vst-plugins).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST:-$HERE/../mpc-vst-plugins}"
python3 "$HERE/tools/gen_patch.py" --params > "$HERE/vst/params.json"
# the pages are written by tools/gen_layout.py (and the SOUNDS tab by hand in layout.banks.conf)
{ cat "$HERE/vst/layout.header.conf"; python3 "$HERE/tools/gen_layout.py"; } > "$HERE/vst/layout.conf"
echo "wrote vst/layout.conf ($(grep -c '^\[tab' "$HERE/vst/layout.conf") tabs)"
