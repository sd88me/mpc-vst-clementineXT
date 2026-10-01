#!/usr/bin/env bash
# Host simulation of the Schwung module on x86 (ASan + UBSan). No Move needed.
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tools/gen_schwung.py
python3 - <<'PY'
import json
d = json.load(open("schwung/module.json"))
cp = d["capabilities"]["chain_params"]; keys = [m["key"] for m in cp]
assert len(keys) == len(set(keys)) <= 256, "duplicate or too many params"
levels = d["capabilities"]["ui_hierarchy"]["levels"]
for name, lv in levels.items():
    for p in lv.get("params", []):
        if "key" in p: assert p["key"] in keys, (name, p["key"])
    assert len(lv.get("knobs", [])) <= 8, name
txt = json.dumps(d)
assert all(ord(c) < 128 for c in txt), "non-ASCII text (the device's 5x7 font cannot draw it)"
for m in cp:
    if m["type"] == "enum": assert m["options"] and all(len(o) <= 31 for o in m["options"]), m["key"]
    assert len(m["name"]) <= 15, m["key"]
assert levels["root"]["list_param"] == "preset" and levels["banks"]["select_param"] == "bank"
print("module.json: %d params, %d levels" % (len(keys), len(levels)))
PY
mkdir -p build/schwung /tmp/clxt_sim_data
SRC="src/engine.c src/out.c src/patch.c src/syx.c src/waves.c src/wavedata.c src/filter.c src/fx.c src/mod.c src/presets.c"
gcc -O1 -g -Wall -Wno-format-truncation -fsanitize=address,undefined -Isrc -Isrc/vendor/schwung -Isrc/vendor/mpc-vst-plugins -Ischwung/src \
    $SRC schwung/src/schwung_plugin.c schwung/test/host_sim.c -o build/schwung/host_sim -lm -lpthread
ASAN_OPTIONS=detect_leaks=0 ./build/schwung/host_sim
