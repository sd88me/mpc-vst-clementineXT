#!/usr/bin/env python3
"""Generate the Schwung (Ableton Move) module description from vst/params.json.

    gen_schwung.py            writes schwung/module.json and schwung/src/schwung_meta.h

module.json carries the static `chain_params` (type, range, options of every control) and the `ui_hierarchy` (the pages the Move's
knob grid walks, eight controls per page); schwung_meta.h embeds the same two JSON strings and the enum option tables so the plugin
can answer `get_param("chain_params")`, `get_param("ui_hierarchy")` and turn an enum label into the index the engine takes.
Limits (Schwung's chain host): 256 params, 128 options of 31 characters per enum, 64 KB of JSON each.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
VERSION = "0.1.0"


def load():
    d = json.load(open(os.path.join(ROOT, "vst", "params.json")))
    return {p["key"]: p for p in d["params"]}


# (level key, page name, label prefix, [param keys]); eight controls per page at most, in knob order
def pages():
    P = []
    def add(key, name, pre, keys): P.append((key, name, pre, keys))
    add("osc1", "Oscillator 1", "O1", ["osc1_oct", "osc1_semi", "osc1_detune", "osc1_bend", "osc1_keytrack", "osc1_fm", "wavetable", "osc2_link"])
    add("osc2", "Oscillator 2", "O2", ["osc2_oct", "osc2_semi", "osc2_detune", "osc2_bend", "osc2_keytrack", "osc2_sync"])
    add("wave1", "Wave 1", "W1", ["w1_start", "w1_phase", "w1_env", "w1_velo", "w1_keytrack", "w1_limit"])
    add("wave2", "Wave 2", "W2", ["w2_start", "w2_phase", "w2_env", "w2_velo", "w2_keytrack", "w2_limit", "w2_link"])
    add("mixer", "Mixer", "Mix", ["mix_w1", "mix_w2", "mix_ring", "mix_noise", "mix_ext"])
    add("quality", "Quality", "Q", ["aliasing", "quantize", "clipping", "accuracy"])
    add("filter1", "Filter 1", "F1", ["f1_cutoff", "f1_reso", "f1_type", "f1_keytrack", "f1_env", "f1_velo", "f1_special"])
    add("filter2", "Filter 2", "F2", ["f2_cutoff", "f2_type", "f2_keytrack"])
    add("fenv", "Filter Envelope", "FE", ["fenv_a", "fenv_d", "fenv_s", "fenv_r", "fenv_trig"])
    add("amp", "Amplifier", "Amp", ["volume", "amp_velo", "amp_keytrack", "pan", "pan_keytrack", "chorus"])
    add("aenv", "Amp Envelope", "AE", ["aenv_a", "aenv_d", "aenv_s", "aenv_r", "aenv_trig"])
    add("wenv_t", "Wave Env Times", "WT", ["wenv_t%d" % i for i in range(1, 9)])
    add("wenv_l", "Wave Env Levels", "WL", ["wenv_l%d" % i for i in range(1, 9)])
    add("wenv_x", "Wave Env Loops", "WE", ["wenv_trig", "wenv_on_loop", "wenv_on_loop_start", "wenv_on_loop_end", "wenv_off_loop", "wenv_off_loop_start", "wenv_off_loop_end"])
    add("free", "Free Envelope", "FR", ["fre_t1", "fre_l1", "fre_t2", "fre_l2", "fre_t3", "fre_l3", "fre_rt", "fre_rl"])
    add("free_t", "Free Env Trigger", "FR", ["fre_trig"])
    add("lfo1", "LFO 1", "L1", ["lfo1_rate", "lfo1_shape", "lfo1_delay", "lfo1_sync", "lfo1_sym", "lfo1_human"])
    add("lfo2", "LFO 2", "L2", ["lfo2_rate", "lfo2_shape", "lfo2_delay", "lfo2_sync", "lfo2_sym", "lfo2_human", "lfo2_phase"])
    add("glide", "Glide", "Gl", ["glide_on", "glide_type", "glide_mode", "glide_time"])
    add("voices", "Voices", "Vc", ["alloc", "assign", "detune", "depan"])
    add("fx", "Effect", "FX", ["fx_type", "fx_p1", "fx_p2", "fx_p3"])
    add("arp", "Arpeggiator", "Arp", ["arp_on", "arp_tempo", "arp_clock", "arp_range", "arp_pattern", "arp_dir", "arp_order", "arp_velo"])
    add("arp_r", "Arp Reset", "Arp", ["arp_reset"])
    for half in range(2):
        lo = 8 * half + 1
        ids = list(range(lo, lo + 8))
        add("mod_amt%d" % (half + 1), "Mod Amount %d-%d" % (lo, lo + 7), "Amt", ["m%d_amt" % n for n in ids])
        add("mod_src%d" % (half + 1), "Mod Source %d-%d" % (lo, lo + 7), "Src", ["m%d_src" % n for n in ids])
        add("mod_dst%d" % (half + 1), "Mod Dest %d-%d" % (lo, lo + 7), "Dst", ["m%d_dst" % n for n in ids])
    for n in range(1, 5):
        add("modifier%d" % n, "Modifier %d" % n, "Mf%d" % n, ["mod%d_src1" % n, "mod%d_src2" % n, "mod%d_op" % n, "mod%d_par" % n])
    add("mdelay", "Control Delay", "CD", ["mdelay_src", "mdelay_time"])
    return P


PLAY_KNOBS = ["play_v1", "play_v2", "play_v3", "play_v4", "f1_cutoff", "f1_reso", "volume", "fx_p1"]
LABELS = {"play_v1": "Play 1", "play_v2": "Play 2", "play_v3": "Play 3", "play_v4": "Play 4", "play1": "Play 1 Param", "play2": "Play 2 Param",
          "play3": "Play 3 Param", "play4": "Play 4 Param"}


def label(key, p, pre):
    if key in LABELS: return LABELS[key]
    name = p["name"]
    s = ("%s %s" % (pre, name)).strip()
    if key.startswith("m") and key[1:2].isdigit() and "_" in key:   # matrix slot: "Amt 3"
        n = key[1:key.index("_")]
        s = "%s %s" % (pre, n)
    if key.startswith("mod") and key[3:4].isdigit(): s = "%s %s" % (pre, name)
    return s[:15]


def param_meta(key, p, pre):
    m = {"key": key, "name": label(key, p, pre)}
    if "options" in p:
        m["type"] = "enum"; m["options"] = [o[:31] for o in p["options"]]; m["default"] = p.get("default", 0)
    else:
        m["type"] = "int"; m["min"] = p["min"]; m["max"] = p["max"]; m["default"] = p.get("default", 0)
    return m


def build():
    params = load()
    metas = []; seen = set(); levels = {}
    root_items = []
    for key, name, pre, keys in pages():
        items = []
        for k in keys:
            if k in seen: continue
            seen.add(k); metas.append(param_meta(k, params[k], pre)); items.append(k)
        if not items: continue
        levels[key] = {"name": name, "params": [{"key": k, "name": next(m["name"] for m in metas if m["key"] == k)} for k in items], "knobs": items[:8]}
        root_items.append({"level": key, "name": name})
    # the four Play knobs and the parameter each controls (the sound's own choice of four), on the first page
    play = []
    for i in range(1, 5):
        k = "play_v%d" % i; seen.add(k)
        metas.append({"key": k, "name": LABELS[k], "type": "int", "min": 0, "max": 127, "default": 0}); play.append(k)
        k2 = "play%d" % i
        metas.append(param_meta(k2, params[k2], "Play")); metas[-1]["name"] = LABELS[k2]
    levels["play_assign"] = {"name": "Play Assign", "params": [{"key": "play%d" % i, "name": LABELS["play%d" % i]} for i in range(1, 5)],
                             "knobs": ["play%d" % i for i in range(1, 5)]}
    root_items.insert(0, {"level": "play_assign", "name": "Play Assign"})
    root_items.insert(0, {"level": "banks", "name": "Bank"})
    root_knobs = PLAY_KNOBS
    for k in root_knobs:
        if k not in {m["key"] for m in metas}:
            metas.append(param_meta(k, params[k], "Root"))
    levels["banks"] = {"name": "Bank", "items_param": "bank_list", "select_param": "bank"}
    root = {"name": "Clementine-XT", "list_param": "preset", "count_param": "preset_count", "name_param": "preset_name",
            "params": root_items, "knobs": root_knobs}
    levels = dict(root=root, **levels)
    hier = {"modes": None, "levels": levels}
    return metas, hier


def main():
    metas, hier = build()
    assert len(metas) <= 256, len(metas)
    for m in metas:
        if m["type"] == "enum": assert len(m["options"]) <= 128, m["key"]
    cp = json.dumps(metas, separators=(",", ":"))
    uh = json.dumps(hier, separators=(",", ":"))
    print("params %d, chain_params %d bytes, ui_hierarchy %d bytes" % (len(metas), len(cp), len(uh)), file=sys.stderr)
    assert len(cp) < 60000 and len(uh) < 60000, "JSON too large for Schwung's 64 KB buffers"
    module = {"id": "clementine-xt", "name": "Clementine-XT", "abbrev": "CLXT", "version": VERSION,
              "description": "Wavetable synth modelled on the Microwave II/XT", "dsp": "dsp.so", "api_version": 2,
              "component_type": "sound_generator", "author": "sd88me",
              "capabilities": {"audio_out": True, "midi_in": True, "midi_out": False, "chainable": True, "component_type": "sound_generator",
                               "ui_hierarchy": hier, "chain_params": metas}}
    os.makedirs(os.path.join(ROOT, "schwung", "src"), exist_ok=True)
    json.dump(module, open(os.path.join(ROOT, "schwung", "module.json"), "w"), indent=1)
    # C header: the two JSON strings (split into chunks), and the enum tables for label -> index
    def cstr(s):
        out = []
        for i in range(0, len(s), 120):
            out.append('"%s"' % s[i:i + 120].replace("\\", "\\\\").replace('"', '\\"'))
        return "\n    ".join(out)
    h = ["/* Generated by tools/gen_schwung.py; do not edit. */", "#pragma once",
         "static const char SCHWUNG_CHAIN_PARAMS[] =\n    %s;" % cstr(cp),
         "static const char SCHWUNG_UI_HIERARCHY[] =\n    %s;" % cstr(uh),
         "typedef struct { const char *key; int nopt; const char *const *opt; } schwung_enum_t;"]
    enums = [m for m in metas if m["type"] == "enum"]
    for i, m in enumerate(enums):
        h.append("static const char *const SCHWUNG_OPT_%d[] = { %s };" % (i, ", ".join('"%s"' % o.replace('"', "'") for o in m["options"])))
    h.append("static const schwung_enum_t SCHWUNG_ENUMS[] = {")
    for i, m in enumerate(enums):
        h.append('    { "%s", %d, SCHWUNG_OPT_%d },' % (m["key"], len(m["options"]), i))
    h.append("};")
    h.append("#define SCHWUNG_NENUMS %d" % len(enums))
    open(os.path.join(ROOT, "schwung", "src", "schwung_meta.h"), "w").write("\n".join(h) + "\n")


if __name__ == "__main__":
    main()
