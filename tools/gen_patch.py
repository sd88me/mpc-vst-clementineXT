#!/usr/bin/env python3
"""Emit src/patch_tab.h: the 256-entry SDATA field table (index, key, range, default) from the definitions below.
Source: Microwave 2/XT SysEx spec 3.1 (field facts only), with the errata of docs/DESIGN.md section 2 applied.
Run: python3 tools/gen_patch.py > src/patch_tab.h
     python3 tools/gen_patch.py --params > vst/params.json      (the VST parameter list, SDATA order + appended controls)"""
import json, os, sys
F = {}   # index -> (key, name, lo, hi, default)

def f(i, key, name, lo, hi, d):
    assert i not in F, i
    F[i] = (key, name, lo, hi, d)

def osc(n, base, sync):
    p = "osc%d_" % n
    f(base, p + "oct", "Osc %d Octave" % n, 16, 112, 64)
    f(base + 1, p + "semi", "Osc %d Semitone" % n, 52, 76, 64)
    f(base + 2, p + "detune", "Osc %d Detune" % n, 0, 127, 64)
osc(1, 1, 0); osc(2, 12, 1)
f(5, "osc1_bend", "Osc 1 Bend Range", 0, 122, 2)
f(6, "osc1_keytrack", "Osc 1 Keytrack", 0, 76, 51)
f(7, "osc1_fm", "Osc 1 FM Amount", 0, 127, 0)
f(16, "osc2_sync", "Osc 2 Sync", 0, 1, 0)
f(17, "osc2_bend", "Osc 2 Bend Range", 0, 122, 2)
f(18, "osc2_keytrack", "Osc 2 Keytrack", 0, 76, 51)
f(19, "osc2_link", "Osc 2 Link", 0, 1, 0)
f(25, "wavetable", "Wavetable", 0, 127, 0)
for n, b in ((1, 26), (2, 36)):
    p = "w%d_" % n
    f(b, p + "start", "Wave %d Start" % n, 0, 63, 0)
    f(b + 1, p + "phase", "Wave %d Phase" % n, 0, 127, 0)
    f(b + 2, p + "env", "Wave %d Env Amount" % n, 0, 127, 64)
    f(b + 3, p + "velo", "Wave %d Env Velo" % n, 0, 127, 64)
    f(b + 4, p + "keytrack", "Wave %d Keytrack" % n, 0, 127, 64)
    f(b + 5, p + "limit", "Wave %d Limit" % n, 0, 1, 0)
f(42, "w2_link", "Wave 2 Link", 0, 1, 0)
for i, k, nm, d in ((47, "mix_w1", "Mix Wave 1", 127), (48, "mix_w2", "Mix Wave 2", 0), (49, "mix_ring", "Mix Ringmod", 0),
                    (50, "mix_noise", "Mix Noise", 0), (51, "mix_ext", "Mix External", 0)):
    f(i, k, nm, 0, 127, d)
f(53, "aliasing", "Aliasing", 0, 5, 0)
f(54, "quantize", "Time Quantization", 0, 5, 0)
f(55, "clipping", "Clipping", 0, 1, 0)
f(57, "accuracy", "Accuracy", 0, 1, 1)
for n in range(4): f(58 + n, "play%d" % (n + 1), "Play Parameter %d" % (n + 1), 0, 82, (28, 29, 10, 77)[n])
f(62, "f1_cutoff", "Filter 1 Cutoff", 0, 127, 127)
f(63, "f1_reso", "Filter 1 Resonance", 0, 127, 0)
f(64, "f1_type", "Filter 1 Type", 0, 12, 0)   # spec says 0..9; factory sounds use up to 12 (checked against firmware output)
f(65, "f1_keytrack", "Filter 1 Keytrack", 0, 127, 64)
f(66, "f1_env", "Filter 1 Env Amount", 0, 127, 64)
f(67, "f1_velo", "Filter 1 Env Velo", 0, 127, 64)
f(70, "f1_special", "Filter 1 Special", 0, 127, 0)
f(73, "f2_cutoff", "Filter 2 Cutoff", 0, 127, 0)
f(74, "f2_type", "Filter 2 Type", 0, 1, 0)
f(75, "f2_keytrack", "Filter 2 Keytrack", 0, 127, 64)
f(76, "fx_type", "Effect Type", 0, 35, 0)
f(77, "volume", "Amplifier Volume", 0, 127, 100)
f(79, "amp_velo", "Amp Env Velo", 0, 127, 64)
f(80, "amp_keytrack", "Amp Keytrack", 0, 127, 64)
f(81, "fx_p1", "Effect Parameter 1", 0, 127, 0)
f(82, "chorus", "Chorus", 0, 2, 0)   # spec says off/on; factory sounds use 2
f(83, "fx_p2", "Effect Parameter 2", 0, 127, 0)
f(84, "pan", "Panning", 0, 127, 64)
f(85, "pan_keytrack", "Panning Keytrack", 0, 127, 64)
f(86, "fx_p3", "Effect Parameter 3", 0, 127, 0)
f(87, "glide_on", "Glide Active", 0, 1, 0)
f(88, "glide_type", "Glide Type", 0, 3, 0)
f(89, "glide_mode", "Glide Mode", 0, 1, 0)
f(90, "glide_time", "Glide Time", 0, 127, 0)
f(92, "arp_on", "Arp Active", 0, 2, 0)
f(93, "arp_tempo", "Arp Tempo", 0, 127, 70)   # spec says 1..127; firmware sounds hold 0
f(94, "arp_clock", "Arp Clock", 0, 15, 5)
f(95, "arp_range", "Arp Range", 1, 10, 1)
f(96, "arp_pattern", "Arp Pattern", 0, 16, 0)
f(97, "arp_dir", "Arp Direction", 0, 3, 0)
f(98, "arp_order", "Arp Note Order", 0, 3, 0)
f(99, "arp_velo", "Arp Velocity", 0, 1, 0)
f(100, "arp_reset", "Arp Reset", 0, 1, 0)
f(101, "arp_len", "Arp User Length", 0, 15, 15)
for n in range(4): f(102 + n, "arp_user%d" % (n + 1), "Arp User Pattern %d-%d" % (4 * n + 1, 4 * n + 4), 0, 15, 15)
f(108, "alloc", "Allocation Mode", 0, 1, 0)
f(109, "assign", "Assignment", 0, 2, 0)
f(110, "detune", "Detune", 0, 127, 0)
f(112, "depan", "De-Pan", 0, 127, 0)
def adsr(base, p, nm, trig):
    for j, s in enumerate(("attack", "decay", "sustain", "release")):
        f(base + j, "%s_%s" % (p, s[0]), "%s %s" % (nm, s.capitalize()), 0, 127, (0, 64, 127, 20)[j])
    f(trig, p + "_trig", nm + " Trigger", 0, 2, 0)
adsr(113, "fenv", "Filter Env", 117); adsr(119, "aenv", "Amp Env", 123)
for n in range(8):
    f(125 + 2 * n, "wenv_t%d" % (n + 1), "Wave Env Time %d" % (n + 1), 0, 127, 0)
    f(126 + 2 * n, "wenv_l%d" % (n + 1), "Wave Env Level %d" % (n + 1), 0, 127, 0)
f(141, "wenv_trig", "Wave Env Trigger", 0, 2, 0)
for i, k, nm in ((142, "wenv_on_loop", "Key On Loop"), (145, "wenv_off_loop", "Key Off Loop")):
    f(i, k, "Wave " + nm, 0, 1, 0); f(i + 1, k + "_start", "Wave %s Start" % nm, 0, 7, 0); f(i + 2, k + "_end", "Wave %s End" % nm, 0, 7, 0)
for n in range(3):
    f(149 + 2 * n, "fre_t%d" % (n + 1), "Free Env Time %d" % (n + 1), 0, 127, 0)
    f(150 + 2 * n, "fre_l%d" % (n + 1), "Free Env Level %d" % (n + 1), 0, 127, 64)
f(155, "fre_rt", "Free Env Release Time", 0, 127, 0)
f(156, "fre_rl", "Free Env Release Level", 0, 127, 64)
f(157, "fre_trig", "Free Env Trigger", 0, 2, 0)
for n, b in ((1, 159), (2, 166)):
    p = "lfo%d_" % n
    f(b, p + "rate", "LFO %d Rate" % n, 0, 127, 64)
    f(b + 1, p + "shape", "LFO %d Shape" % n, 0, 5, 0)
    f(b + 2, p + "delay", "LFO %d Delay" % n, 0, 127, 0)
    f(b + 3, p + "sync", "LFO %d Sync" % n, 0, 3, 0)
    f(b + 4, p + "sym", "LFO %d Symmetry" % n, 0, 127, 64)
    f(b + 5, p + "human", "LFO %d Humanize" % n, 0, 127, 0)
f(172, "lfo2_phase", "LFO 2 Phase", 0, 127, 0)
f(174, "mdelay_src", "Modifier Delay Source", 0, 31, 0)
f(175, "mdelay_time", "Modifier Delay Time", 0, 127, 0)
for n in range(4):   # errata: the spec labels 188-191 "Modifier 3" a second time; they are Modifier 4
    b = 176 + 4 * n
    f(b, "mod%d_src1" % (n + 1), "Modifier %d Source 1" % (n + 1), 0, 31, 0)
    f(b + 1, "mod%d_src2" % (n + 1), "Modifier %d Source 2" % (n + 1), 0, 31, 0)
    f(b + 2, "mod%d_op" % (n + 1), "Modifier %d Type" % (n + 1), 0, 15, 0)
    f(b + 3, "mod%d_par" % (n + 1), "Modifier %d Parameter" % (n + 1), 0, 127, 0)
for n in range(16):   # errata: destination range is 0..35 (36 entries), not 0..33
    b = 192 + 3 * n
    f(b, "m%d_src" % (n + 1), "Mod %d Source" % (n + 1), 0, 31, 0)
    f(b + 1, "m%d_amt" % (n + 1), "Mod %d Amount" % (n + 1), 0, 127, 64)
    f(b + 2, "m%d_dst" % (n + 1), "Mod %d Destination" % (n + 1), 0, 35, 0)
for n in range(16): f(240 + n, "name%d" % (n + 1), "Name %d" % (n + 1), 32, 127, 32)

# Init-sound values measured from the firmware's own init sound (oracle, Xenia); they replace the guesses above.
INIT = {6: 48, 18: 48, 47: 80, 48: 80, 57: 0, 60: 32, 61: 37, 73: 127, 90: 32, 93: 37, 94: 12, 101: 7, 102: 8, 103: 8, 104: 8, 105: 8, 110: 32, 113: 10, 114: 50, 116: 10, 119: 10, 120: 50, 122: 10, 125: 32, 126: 127, 127: 32, 129: 32, 130: 64, 131: 32, 133: 32, 134: 64, 135: 32, 137: 32, 138: 64, 139: 32, 149: 32, 150: 127, 151: 32, 152: 0, 153: 32, 154: 0, 155: 32, 156: 0, 159: 100, 166: 100, 192: 2, 195: 1, 198: 4, 201: 6, 204: 5}
INIT_RESERVED = {9: 64, 22: 64, 33: 64, 44: 64, 69: 64, 78: 64}   # reserved bytes the firmware sets in its init sound (kept so a saved init matches)
for i, v in INIT.items():
    k, nm, lo, hi, _ = F[i]
    F[i] = (k, nm, lo, hi, v)

LISTS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "sdata_lists.json")))
ENUM = {   # key or key prefix -> option names; a 0..1 field with none listed becomes off/on
    "f1_type": LISTS["f1_type"], "f2_type": ["6dB LP", "6dB HP"], "fx_type": LISTS["fx_type"],
    "clipping": ["saturate", "overflow"], "glide_type": ["porta", "gliss", "fp.", "fg."], "glide_mode": ["exp", "linear"],
    "arp_on": ["off", "on", "hold"], "arp_dir": ["up", "down", "alt", "random"], "arp_order": ["note", "n.rev", "played", "p.rev"],
    "arp_velo": ["root note", "last note"], "alloc": ["poly", "mono"], "assign": ["normal", "dual", "unison"],
    "arp_clock": ["1/1", "1/2.", "1/2T", "1/2", "1/4.", "1/4T", "1/4", "1/8.", "1/8T", "1/8", "1/16.", "1/16T", "1/16", "1/32T", "1/32", "1/64T"],   # measured (the last three are inferred)
    "chorus": ["off", "on", "on 2"],
    "lfo1_shape": ["sin", "tri", "sqr", "saw", "rnd", "S&H"], "lfo2_shape": ["sin", "tri", "sqr", "saw", "rnd", "S&H"],
    "lfo1_sync": ["off", "on", "on 2", "clock"], "lfo2_sync": ["off", "on", "on 2", "clock"],
    "aliasing": ["off", "1", "2", "3", "4", "5"], "quantize": ["off", "1", "2", "3", "4", "5"],
    "play": LISTS["play"],
}
TRIG = ["normal", "single", "retrigger"]

def options(k, lo, hi):
    if k in ENUM: return ENUM[k]
    if k.startswith("play"): return ENUM["play"]
    if k.endswith("_trig"): return TRIG
    if k.endswith("_src") or k.endswith("_src1") or k.endswith("_src2"): return LISTS["src"]
    if k.endswith("_dst"): return LISTS["dst"]
    if k.endswith("_op"): return LISTS["modifier"]
    if (lo, hi) == (0, 1): return ["off", "on"]
    return None

def _slots(prefix, n, keys):
    return [prefix % (i + 1) + k for i in range(n) for k in keys]

# Skin sections, in page order (studio.py auto packs them into pages of 2 rows x 8 slots). Labels follow the XT's own page names.
SECTIONS = [
    ("PLAY", ["bank", "bank_name", "program", "program_prev", "program_next", "patch_name", "play1", "play2", "play3", "play4", "play_v1", "play_v2", "play_v3", "play_v4"]),
    ("OSC 1", ["osc1_oct", "osc1_semi", "osc1_detune", "osc1_bend", "osc1_keytrack", "osc1_fm"]),
    ("OSC 2", ["osc2_oct", "osc2_semi", "osc2_detune", "osc2_bend", "osc2_keytrack", "osc2_sync", "osc2_link"]),
    ("WAVETABLE", ["wavetable"]),
    ("WAVE 1", ["w1_start", "w1_phase", "w1_env", "w1_velo", "w1_keytrack", "w1_limit"]),
    ("WAVE 2", ["w2_start", "w2_phase", "w2_env", "w2_velo", "w2_keytrack", "w2_limit", "w2_link"]),
    ("MIXER", ["mix_w1", "mix_w2", "mix_ring", "mix_noise", "mix_ext"]),
    ("QUALITY", ["aliasing", "quantize", "clipping", "accuracy"]),
    ("FILTER 1", ["f1_cutoff", "f1_reso", "f1_type", "f1_keytrack", "f1_env", "f1_velo", "f1_special"]),
    ("FILTER 2", ["f2_cutoff", "f2_type", "f2_keytrack"]),
    ("FILTER ENV", ["fenv_a", "fenv_d", "fenv_s", "fenv_r", "fenv_trig"]),
    ("AMPLIFIER", ["volume", "amp_velo", "amp_keytrack", "pan", "pan_keytrack", "chorus"]),
    ("AMP ENV", ["aenv_a", "aenv_d", "aenv_s", "aenv_r", "aenv_trig"]),
    ("WAVE ENV TIMES", ["wenv_t%d" % i for i in range(1, 9)]),
    ("WAVE ENV LEVELS", ["wenv_l%d" % i for i in range(1, 9)]),
    ("WAVE ENV", ["wenv_trig", "wenv_on_loop", "wenv_on_loop_start", "wenv_on_loop_end", "wenv_off_loop", "wenv_off_loop_start", "wenv_off_loop_end"]),
    ("FREE ENV", ["fre_t1", "fre_l1", "fre_t2", "fre_l2", "fre_t3", "fre_l3", "fre_rt", "fre_rl", "fre_trig"]),
    ("LFO 1", ["lfo1_rate", "lfo1_shape", "lfo1_delay", "lfo1_sync", "lfo1_sym", "lfo1_human"]),
    ("LFO 2", ["lfo2_rate", "lfo2_shape", "lfo2_delay", "lfo2_sync", "lfo2_sym", "lfo2_human", "lfo2_phase"]),
    ("GLIDE", ["glide_on", "glide_type", "glide_mode", "glide_time"]),
    ("VOICES", ["alloc", "assign", "detune", "depan"]),
    ("EFFECT", ["fx_type", "fx_p1", "fx_p2", "fx_p3"]),
    ("ARP", ["arp_on", "arp_tempo", "arp_clock", "arp_range", "arp_pattern", "arp_dir", "arp_order", "arp_velo"]),
    ("ARP PATTERN", ["arp_reset"]),   # the user pattern (arp_len, arp_user1-4) is not offered: no good way to edit it, and the firmware ignored it in the rig
    ("MOD DELAY", ["mdelay_src", "mdelay_time"]),
] + [("MODIFIER %d" % n, ["mod%d_src1" % n, "mod%d_src2" % n, "mod%d_op" % n, "mod%d_par" % n]) for n in range(1, 5)] \
  + [("MOD %d" % n, ["m%d_src" % n, "m%d_amt" % n, "m%d_dst" % n]) for n in range(1, 17)]

_STRIP = ("Osc 1 ", "Osc 2 ", "Wave 1 ", "Wave 2 ", "Filter 1 ", "Filter 2 ", "Filter Env ", "Amp Env ", "Amplifier Env ", "Amplifier ",
          "Wave Env ", "Free Env ", "LFO 1 ", "LFO 2 ", "Arp ", "Glide ", "Effect ", "Modifier ")
_ABBR = (("Velocity", "Velo"), ("Amount", "Amt"), ("Envelope", "Env"), ("Keytrack", "Keytrack"), ("Resonance", "Reso"), ("Attack", "Attack"),
         ("Parameter", "Param"), ("Symmetry", "Symm"), ("Humanize", "Human"), ("Semitone", "Semi"), ("Pitchbend", "Bend"), ("Bend Range", "Bend"),
         ("Startwave", "Start"), ("Start Phase", "Phase"), ("Allocation Mode", "Mode"), ("Assignment", "Assign"), ("Destination", "Dest"),
         ("Source", "Src"), ("Trigger", "Trigger"), ("Effect Parameter", "Param"), ("Time Quantization", "Quantize"), ("Amplifier Volume", "Volume"),
         ("Panning Keytrack", "Pan Keyt"), ("Modifier Delay ", "Mdly "))

_OVERRIDE = {"mix_w1": "Wave 1", "mix_w2": "Wave 2", "mix_ring": "Ringmod", "mix_noise": "Noise", "mix_ext": "External",
             "play1": "Param 1", "play2": "Param 2", "play3": "Param 3", "play4": "Param 4", "amp_keytrack": "Keytrack",
             "arp_len": "Length", "arp_user1": "Steps 1-4", "arp_user2": "Steps 5-8", "arp_user3": "Steps 9-12", "arp_user4": "Steps 13-16",
             "wenv_on_loop": "On Loop", "wenv_on_loop_start": "On Start", "wenv_on_loop_end": "On End", "wenv_off_loop": "Off Loop",
             "wenv_off_loop_start": "Off Start", "wenv_off_loop_end": "Off End", "fre_rt": "Rel Time", "fre_rl": "Rel Level",
             "pan_keytrack": "Pan Keyt", "aliasing": "Aliasing", "mdelay_src": "Source", "mdelay_time": "Time", "depan": "De-Pan"}

def short_label(key, name):
    """A short control label (about 10 characters): the frame title already says which module it belongs to."""
    import re
    if key in _OVERRIDE: return _OVERRIDE[key]
    out = re.sub(r"^(Mod|Modifier) \d+ ", "", name)
    for pre in _STRIP:
        if out.startswith(pre): out = out[len(pre):]; break
    for a, b in _ABBR: out = out.replace(a, b)
    return out.strip()

def params_json():
    ps = []
    for i in range(1, 240):
        if i not in F: continue
        k, nm, lo, hi, d = F[i]
        if k == "arp_pattern": hi = 15   # 16 (user pattern) is not offered; the sound format keeps the field
        p = {"key": k, "name": short_label(k, nm)}
        o = options(k, lo, hi)
        if o:
            o = o[lo:hi + 1] if len(o) > hi - lo + 1 and not k.startswith("play") and k not in ("fx_type", "f1_type") else o
            p["options"] = o; p["default"] = d - lo
        else:
            p.update({"min": lo, "max": hi, "default": d, "display": "int"})
        ps.append(p)
    # appended controls (never reorder or insert above this line once a release ships)
    ps.append({"key": "program", "name": "Program", "min": 0, "max": 255, "default": 0, "display": "int", "type": "stepper"})
    ps.append({"key": "patch_name", "name": "Sound", "min": 0, "max": 0, "default": 0, "display": "string", "type": "readout"})
    ps.append({"key": "program_prev", "name": "Program <", "min": 0, "max": 1, "default": 0, "momentary": True, "type": "trigger", "step_of": "program", "step_delta": -1})
    ps.append({"key": "program_next", "name": "Program >", "min": 0, "max": 1, "default": 0, "momentary": True, "type": "trigger", "step_of": "program", "step_delta": 1})
    # the Banks page (tiles are momentary string params: get = the tile text, set = a tap; see the jv880 port)
    ps.append({"key": "bank_name", "name": "Bank", "min": 0, "max": 0, "default": 0, "display": "string", "type": "readout"})
    ps.append({"key": "browse_bank_name", "name": "Browsing", "min": 0, "max": 0, "default": 0, "display": "string", "type": "readout"})
    ps.append({"key": "patch_page_text", "name": "Page", "min": 0, "max": 0, "default": 0, "display": "string", "type": "readout"})
    ps.append({"key": "patch_page_prev", "name": "Page <", "min": 0, "max": 1, "default": 0, "momentary": True, "type": "trigger"})
    ps.append({"key": "patch_page_next", "name": "Page >", "min": 0, "max": 1, "default": 0, "momentary": True, "type": "trigger"})
    for i in range(1, 13):
        ps.append({"key": "bank_slot_%d" % i, "name": "BANK %d" % i, "min": 0, "max": 1, "default": 0, "momentary": True, "display": "string", "type": "slot"})
    for i in range(1, 29):
        ps.append({"key": "patch_slot_%d" % i, "name": "SOUND %d" % i, "min": 0, "max": 1, "default": 0, "momentary": True, "display": "string", "type": "slot"})
    # the global page: a bank stepper and the four Play knobs (each one is the parameter its Play Parameter selector names, spanning that parameter's range)
    ps.append({"key": "bank", "name": "Bank", "min": 0, "max": 23, "default": 0, "display": "int", "type": "stepper"})
    ps.append({"key": "bank_prev", "name": "Bank <", "min": 0, "max": 1, "default": 0, "momentary": True, "type": "trigger", "step_of": "bank", "step_delta": -1})
    ps.append({"key": "bank_next", "name": "Bank >", "min": 0, "max": 1, "default": 0, "momentary": True, "type": "trigger", "step_of": "bank", "step_delta": 1})
    for i in range(1, 5):
        ps.append({"key": "play_v%d" % i, "name": "Play %d" % i, "min": 0, "max": 127, "default": 0, "display": "int"})
    # the Play knobs' assignment as a stepper with a name readout (the 83-entry popup list runs off the screen)
    for i in range(1, 5):
        ps.append({"key": "play_sel%d" % i, "name": "Assign %d" % i, "min": 0, "max": 82, "default": 0, "display": "int", "type": "stepper"})
        ps.append({"key": "play_sel%d_prev" % i, "name": "Assign %d <" % i, "min": 0, "max": 1, "default": 0, "momentary": True, "type": "trigger", "step_of": "play_sel%d" % i, "step_delta": -1})
        ps.append({"key": "play_sel%d_next" % i, "name": "Assign %d >" % i, "min": 0, "max": 1, "default": 0, "momentary": True, "type": "trigger", "step_of": "play_sel%d" % i, "step_delta": 1})
        ps.append({"key": "play_name%d" % i, "name": "Assigned %d" % i, "min": 0, "max": 0, "default": 0, "display": "string", "type": "readout"})
    have = {p["key"] for p in ps}
    secs = []
    for label, keys in SECTIONS:
        ks = [k for k in keys if k in have]
        assert len(ks) == len(keys), (label, [k for k in keys if k not in have])
        secs.append({"label": label, "keys": ks})
    print(json.dumps({"name": "Clementine-XT", "params": ps, "sections": secs}, indent=1))

# MIDI controller table (Controller Number Assignment, release 2.28): cc -> (SDATA index, conversion).
# kinds: 0 direct, 1 octave (cc 0..8 -> 16 + 12*cc), 2 semitone (cc 0..24 -> 52 + cc), 3 arp range (cc + 1),
# 4 oscillator keytrack (cc 0..127 -> 0..72). Bank select, sustain, mod wheel etc. are handled by the engine, not here.
CC = [(5, 90, 0), (10, 84, 0), (12, 82, 0), (13, 7, 0), (14, 113, 0), (15, 114, 0), (16, 115, 0), (17, 116, 0),
      (18, 119, 0), (19, 120, 0), (20, 121, 0), (21, 122, 0), (22, 88, 0), (23, 89, 0), (24, 159, 0), (25, 160, 0),
      (26, 166, 0), (27, 168, 0), (28, 167, 0), (29, 117, 0), (30, 161, 0), (31, 123, 0), (33, 1, 1), (34, 2, 2),
      (35, 3, 0), (36, 5, 0), (37, 6, 4), (38, 12, 1), (39, 13, 2), (40, 14, 0), (41, 16, 0), (42, 17, 0), (43, 18, 4),
      (44, 19, 0), (45, 47, 0), (46, 48, 0), (47, 49, 0), (48, 50, 0), (50, 62, 0), (51, 65, 0), (52, 66, 0),
      (53, 67, 0), (54, 64, 0), (55, 80, 0), (56, 63, 0), (57, 77, 0), (58, 79, 0), (60, 73, 0), (61, 74, 0),
      (62, 75, 0), (65, 87, 0), (70, 25, 0), (71, 26, 0), (72, 27, 0), (73, 28, 0), (74, 29, 0), (75, 30, 0),
      (76, 31, 0), (77, 36, 0), (78, 37, 0), (79, 38, 0), (80, 39, 0), (81, 40, 0), (82, 41, 0), (83, 42, 0),
      (85, 149, 0), (86, 150, 0), (87, 151, 0), (88, 152, 0), (89, 153, 0), (90, 154, 0), (91, 155, 0), (92, 156, 0),
      (93, 157, 0), (94, 193, 0), (95, 196, 0), (102, 92, 0), (103, 95, 3), (104, 94, 0), (105, 93, 0), (106, 97, 0),
      (107, 96, 0), (108, 98, 0), (109, 99, 0), (110, 100, 0), (111, 101, 0), (112, 162, 0), (113, 163, 0),
      (114, 164, 0), (115, 169, 0), (116, 170, 0), (117, 171, 0), (118, 172, 0)]

def main():
    if "--params" in sys.argv:
        return params_json()
    print("/* generated by tools/gen_patch.py from the SDATA spec (3.1): do not edit */")
    print("#pragma once")
    print("#include \"patch.h\"")
    print("const patch_field_t patch_fields[PATCH_SIZE] = {")
    for i in range(256):
        if i in F:
            k, nm, lo, hi, d = F[i]
            print('  /*%3d*/ { "%s", "%s", %d, %d, %d },' % (i, k, nm, lo, hi, d))
        else:
            print('  /*%3d*/ { 0, 0, 0, 0, 0 },' % i)   # reserved (and index 0, the format version, handled in patch.c)
    print("};")
    print("const patch_cc_t patch_cc_map[] = {")
    for cc, idx, kind in CC:
        assert idx in F, (cc, idx)
        print("  { %d, %d, %d }," % (cc, idx, kind))
    print("  { 0, 0, 0 } };")
    print("const uint8_t patch_init_reserved[][2] = { %s { 0, 0 } };" % " ".join("{ %d, %d }," % kv for kv in sorted(INIT_RESERVED.items())))
main()
