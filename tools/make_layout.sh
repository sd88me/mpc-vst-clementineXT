#!/usr/bin/env bash
# Regenerate vst/params.json and vst/layout.conf (theme header + studio auto-layout of the section groups).
# Needs a checkout of mpc-vst-plugins next to this repo (MPC_VST, default ../mpc-vst-plugins).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST:-$HERE/../mpc-vst-plugins}"
python3 "$HERE/tools/gen_patch.py" --params > "$HERE/vst/params.json"
python3 "$MV/tools/studio.py" auto "$HERE/vst/params.json" -o "$HERE/vst/layout.body.conf" >/dev/null
# the Banks page is written by hand (layout.banks.conf); the studio puts the bank/sound tiles it knows nothing about on "MORE" tabs: drop those
python3 - "$HERE/vst/layout.body.conf" > "$HERE/vst/layout.body.nomore.conf" <<'PY'
import sys
out, skip = [], False
for line in open(sys.argv[1]):
    if line.startswith("[tab"): skip = line.startswith("[tab MORE")
    if not skip: out.append(line)
sys.stdout.write("".join(out))
PY
cat "$HERE/vst/layout.header.conf" "$HERE/vst/layout.banks.conf" "$HERE/vst/layout.body.nomore.conf" > "$HERE/vst/layout.conf"
rm "$HERE/vst/layout.body.nomore.conf"
rm "$HERE/vst/layout.body.conf"
echo "wrote vst/layout.conf ($(grep -c '^\[tab' "$HERE/vst/layout.conf") tabs)"
