#!/usr/bin/env python3
"""Writes the hand-designed pages (everything after the Banks tab) of vst/layout.conf to stdout.

The skin has 9 tabs instead of the XT's 17 menu pages. A tab is four rows of eight 158 px cells; a frame spans whole cells of one row
and each control sits in one cell (knobs and toggles at the frame's knob line, popups, steppers and readouts a little lower).
Several `qlinks` lines in a tab become sub-pages that share the design (docs/PORTING.md); their names stay short because MPC's tab strip is narrow.
"""
import sys

OUT = []
Y0, ROW_H, GAP, CELL, X0 = 92, 148, 6, 158, 10

def emit(s): OUT.append(s)

def ry(row): return Y0 + (ROW_H + GAP) * row
def cx(cell): return X0 + 77 + CELL * cell

def tab(name): emit("\n[tab %s]" % name)
def frame(row, cell, n, title): emit('frame x=%d y=%d w=%d h=%d title="%s"' % (X0 + CELL * cell, ry(row), CELL * n - GAP, ROW_H, title))
def knob(row, cell, label, key): emit('knob cx=%d cy=%d r=26 label="%s" key=%s' % (cx(cell), ry(row) + 74, label, key))
def toggle(row, cell, label, key): emit('toggle cx=%d cy=%d label="%s" key=%s' % (cx(cell), ry(row) + 78, label, key))
def popup(row, cell, label, key, w=134): emit('popup cx=%d cy=%d w=%d h=48 label="%s" key=%s' % (cx(cell), ry(row) + 104, w, label, key))
def stepper(row, cell, n, label, key): emit('stepper cx=%d cy=%d w=%d h=48 label="%s" key=%s' % (X0 + CELL * cell + (CELL * n - GAP) // 2, ry(row) + 104, CELL * n - 24, label, key))
def readout(row, cell, n, label, key): emit('readout cx=%d cy=%d w=%d h=48 label="%s" key=%s' % (X0 + CELL * cell + (CELL * n - GAP) // 2, ry(row) + 104, CELL * n - 24, label, key))
def qlinks(name, keys):
    assert len(keys) <= 16, (name, len(keys))
    emit('qlinks "%s" = %s' % (name, ",".join(keys)))

def knobs(row, cell, items):   # [(label, key)] one per cell; keys starting with "~" are toggles, "^" popups
    for i, (label, key) in enumerate(items):
        if key[0] == "~": toggle(row, cell + i, label, key[1:])
        elif key[0] == "^": popup(row, cell + i, label, key[1:])
        else: knob(row, cell + i, label, key)

# ---- GLOBAL ---------------------------------------------------------------------------------------------------------------------------
tab("GLOBAL")
frame(0, 0, 8, "PLAY")   # on the top row: the 83-entry ASSIGN list needs the most room below the field to fit the screen
for k in range(4):
    n = k + 1; knob(0, 2 * k, "PLAY %d" % n, "play_v%d" % n); popup(0, 2 * k + 1, "ASSIGN", "play%d" % n)
frame(1, 0, 8, "SOUND")
stepper(1, 0, 2, "BANK", "bank"); readout(1, 2, 2, "", "bank_name")
stepper(1, 4, 2, "SOUND", "program"); readout(1, 6, 2, "", "patch_name")
frame(2, 0, 4, "EFFECT"); popup(2, 0, "TYPE", "fx_type"); knobs(2, 1, [("PARAM 1", "fx_p1"), ("PARAM 2", "fx_p2"), ("PARAM 3", "fx_p3")])
frame(2, 4, 4, "VOICES"); knobs(2, 4, [("MODE", "^alloc"), ("ASSIGN", "^assign"), ("DETUNE", "detune"), ("DE-PAN", "depan")])
frame(3, 0, 4, "GLIDE"); knobs(3, 0, [("ACTIVE", "~glide_on"), ("TYPE", "^glide_type"), ("MODE", "^glide_mode"), ("TIME", "glide_time")])
frame(3, 4, 4, "OUTPUT"); knobs(3, 4, [("VOLUME", "volume"), ("PANNING", "pan"), ("CHORUS", "^chorus")])
qlinks("Play", ["play_v1", "play_v2", "play_v3", "play_v4", "program", "bank", "volume", "pan",
                "fx_type", "fx_p1", "fx_p2", "fx_p3", "glide_time", "detune", "depan", "chorus"])
qlinks("Voice", ["alloc", "assign", "detune", "depan", "glide_on", "glide_type", "glide_mode", "glide_time"])

GLOBAL_TEXT = "\n".join(OUT)

# ---- OSC ------------------------------------------------------------------------------------------------------------------------------
OSC = []
OUT = OSC
tab("OSC")
frame(0, 0, 7, "OSC 1"); knobs(0, 0, [("OCTAVE", "osc1_oct"), ("SEMI", "osc1_semi"), ("DETUNE", "osc1_detune"), ("BEND", "osc1_bend"),
                                       ("KEYTRACK", "osc1_keytrack"), ("FM AMT", "osc1_fm")])
knob(0, 6, "WAVETABLE", "wavetable")
frame(1, 0, 7, "OSC 2"); knobs(1, 0, [("OCTAVE", "osc2_oct"), ("SEMI", "osc2_semi"), ("DETUNE", "osc2_detune"), ("BEND", "osc2_bend"),
                                       ("KEYTRACK", "osc2_keytrack"), ("SYNC", "~osc2_sync"), ("LINK", "~osc2_link")])
frame(2, 0, 4, "QUALITY"); knobs(2, 0, [("ALIASING", "^aliasing"), ("QUANTIZE", "^quantize"), ("CLIPPING", "^clipping"), ("ACCURACY", "~accuracy")])
qlinks("Osc", ["osc1_oct", "osc1_semi", "osc1_detune", "osc1_bend", "osc1_keytrack", "osc1_fm", "wavetable", "aliasing",
               "osc2_oct", "osc2_semi", "osc2_detune", "osc2_bend", "osc2_keytrack", "osc2_sync", "osc2_link", "quantize"])

# ---- WAVE -----------------------------------------------------------------------------------------------------------------------------
WAVE = []
OUT = WAVE
tab("WAVE")
frame(0, 0, 6, "WAVE 1"); knobs(0, 0, [("START", "w1_start"), ("PHASE", "w1_phase"), ("ENV AMT", "w1_env"), ("ENV VELO", "w1_velo"),
                                        ("KEYTRACK", "w1_keytrack"), ("LIMIT", "~w1_limit")])
frame(1, 0, 7, "WAVE 2"); knobs(1, 0, [("START", "w2_start"), ("PHASE", "w2_phase"), ("ENV AMT", "w2_env"), ("ENV VELO", "w2_velo"),
                                        ("KEYTRACK", "w2_keytrack"), ("LIMIT", "~w2_limit"), ("LINK", "~w2_link")])
frame(2, 0, 5, "MIXER"); knobs(2, 0, [("WAVE 1", "mix_w1"), ("WAVE 2", "mix_w2"), ("RINGMOD", "mix_ring"), ("NOISE", "mix_noise"), ("EXTERNAL", "mix_ext")])
qlinks("Waves", ["w1_start", "w1_phase", "w1_env", "w1_velo", "w1_keytrack", "w1_limit", "mix_w1", "mix_w2",
                 "w2_start", "w2_phase", "w2_env", "w2_velo", "w2_keytrack", "w2_limit", "w2_link", "mix_ring"])
qlinks("Mixer", ["mix_w1", "mix_w2", "mix_ring", "mix_noise", "mix_ext"])

# ---- FILTER ---------------------------------------------------------------------------------------------------------------------------
FILT = []
OUT = FILT
tab("FILTER")
frame(0, 0, 7, "FILTER 1"); knobs(0, 0, [("CUTOFF", "f1_cutoff"), ("RESO", "f1_reso"), ("TYPE", "^f1_type"), ("KEYTRACK", "f1_keytrack"),
                                          ("ENV AMT", "f1_env"), ("ENV VELO", "f1_velo"), ("SPECIAL", "f1_special")])
frame(1, 0, 3, "FILTER 2"); knobs(1, 0, [("CUTOFF", "f2_cutoff"), ("TYPE", "^f2_type"), ("KEYTRACK", "f2_keytrack")])
frame(1, 3, 5, "FILTER ENV"); knobs(1, 3, [("ATTACK", "fenv_a"), ("DECAY", "fenv_d"), ("SUSTAIN", "fenv_s"), ("RELEASE", "fenv_r"), ("TRIGGER", "^fenv_trig")])
frame(2, 0, 5, "AMP ENV"); knobs(2, 0, [("ATTACK", "aenv_a"), ("DECAY", "aenv_d"), ("SUSTAIN", "aenv_s"), ("RELEASE", "aenv_r"), ("TRIGGER", "^aenv_trig")])
frame(2, 5, 3, "AMP"); knobs(2, 5, [("VELO", "amp_velo"), ("KEYTRACK", "amp_keytrack"), ("PAN KEYT", "pan_keytrack")])
qlinks("Filter", ["f1_cutoff", "f1_reso", "f1_type", "f1_keytrack", "f1_env", "f1_velo", "f1_special", "f2_cutoff",
                  "f2_type", "f2_keytrack", "fenv_a", "fenv_d", "fenv_s", "fenv_r", "fenv_trig", "volume"])
qlinks("Amp", ["aenv_a", "aenv_d", "aenv_s", "aenv_r", "aenv_trig", "amp_velo", "amp_keytrack", "pan_keytrack"])

# ---- ENV ------------------------------------------------------------------------------------------------------------------------------
ENV = []
OUT = ENV
tab("ENV")
frame(0, 0, 8, "WAVE ENV TIMES"); knobs(0, 0, [("TIME %d" % i, "wenv_t%d" % i) for i in range(1, 9)])
frame(1, 0, 8, "WAVE ENV LEVELS"); knobs(1, 0, [("LEVEL %d" % i, "wenv_l%d" % i) for i in range(1, 9)])
frame(2, 0, 7, "WAVE ENV LOOPS"); knobs(2, 0, [("TRIGGER", "^wenv_trig"), ("ON LOOP", "~wenv_on_loop"), ("ON START", "wenv_on_loop_start"),
                                                ("ON END", "wenv_on_loop_end"), ("OFF LOOP", "~wenv_off_loop"), ("OFF START", "wenv_off_loop_start"),
                                                ("OFF END", "wenv_off_loop_end")])
frame(2, 7, 1, "FREE"); popup(2, 7, "TRIGGER", "fre_trig", w=134)
frame(3, 0, 8, "FREE ENV"); knobs(3, 0, [("TIME 1", "fre_t1"), ("LEVEL 1", "fre_l1"), ("TIME 2", "fre_t2"), ("LEVEL 2", "fre_l2"), ("TIME 3", "fre_t3"),
                                          ("LEVEL 3", "fre_l3"), ("REL TIME", "fre_rt"), ("REL LEVEL", "fre_rl")])
qlinks("Wave Env", ["wenv_t%d" % i for i in range(1, 9)] + ["wenv_l%d" % i for i in range(1, 9)])
qlinks("Loops", ["wenv_trig", "wenv_on_loop", "wenv_on_loop_start", "wenv_on_loop_end", "wenv_off_loop", "wenv_off_loop_start", "wenv_off_loop_end", "fre_trig"])
qlinks("Free", ["fre_t1", "fre_l1", "fre_t2", "fre_l2", "fre_t3", "fre_l3", "fre_rt", "fre_rl"])

# ---- LFO / ARP ------------------------------------------------------------------------------------------------------------------------
LFO = []
OUT = LFO
tab("LFO ARP")
frame(0, 0, 6, "LFO 1"); knobs(0, 0, [("RATE", "lfo1_rate"), ("SHAPE", "^lfo1_shape"), ("DELAY", "lfo1_delay"), ("SYNC", "^lfo1_sync"), ("SYMM", "lfo1_sym"), ("HUMAN", "lfo1_human")])
frame(1, 0, 7, "LFO 2"); knobs(1, 0, [("RATE", "lfo2_rate"), ("SHAPE", "^lfo2_shape"), ("DELAY", "lfo2_delay"), ("SYNC", "^lfo2_sync"), ("SYMM", "lfo2_sym"),
                                       ("HUMAN", "lfo2_human"), ("PHASE", "lfo2_phase")])
frame(2, 0, 8, "ARP"); knobs(2, 0, [("ACTIVE", "^arp_on"), ("TEMPO", "arp_tempo"), ("CLOCK", "^arp_clock"), ("RANGE", "arp_range"), ("PATTERN", "arp_pattern"),
                                     ("DIRECTION", "^arp_dir"), ("NOTE ORDER", "^arp_order"), ("VELO", "^arp_velo")])
frame(3, 0, 1, "ARP RESET"); toggle(3, 0, "RESET", "arp_reset")
qlinks("LFO", ["lfo1_rate", "lfo1_shape", "lfo1_delay", "lfo1_sync", "lfo1_sym", "lfo1_human", "lfo2_rate", "lfo2_shape",
               "lfo2_delay", "lfo2_sync", "lfo2_sym", "lfo2_human", "lfo2_phase"])
qlinks("Arp", ["arp_on", "arp_tempo", "arp_clock", "arp_range", "arp_pattern", "arp_dir", "arp_order", "arp_velo", "arp_reset"])

# ---- MATRIX: 16 slots over two tabs, each slot in its own panel (source, destination, amount) ------------------------------------------
MAT = []
OUT = MAT
for page in range(2):
    tab("MOD %d-%d" % (8 * page + 1, 8 * page + 8))
    for r in range(4):
        for half in range(2):
            n = 8 * page + 2 * r + half + 1; c0 = 4 * half
            frame(r, c0, 4, "SLOT %d" % n)
            emit('popup cx=%d cy=%d w=150 h=48 label="SOURCE" key=m%d_src' % (cx(c0), ry(r) + 104, n))
            emit('popup cx=%d cy=%d w=150 h=48 label="DEST" key=m%d_dst' % (cx(c0 + 1), ry(r) + 104, n))
            knob(r, c0 + 2, "AMOUNT", "m%d_amt" % n)
    lo, hi = 8 * page + 1, 8 * page + 8
    qlinks("Mod %d-%d" % (lo, hi), ["m%d_amt" % n for n in range(lo, hi + 1)] + ["m%d_src" % n for n in range(lo, hi + 1)])
    qlinks("Dest %d-%d" % (lo, hi), ["m%d_dst" % n for n in range(lo, hi + 1)])

# ---- MODIFIERS ------------------------------------------------------------------------------------------------------------------------
MODS = []
OUT = MODS
tab("MODIFIERS")
for n in range(1, 5):
    frame(n - 1, 0, 4, "MODIFIER %d" % n)
    popup(n - 1, 0, "SRC 1", "mod%d_src1" % n); popup(n - 1, 1, "SRC 2", "mod%d_src2" % n); popup(n - 1, 2, "TYPE", "mod%d_op" % n); knob(n - 1, 3, "PARAM", "mod%d_par" % n)
frame(0, 4, 2, "CONTROL DELAY"); popup(0, 4, "SOURCE", "mdelay_src"); knob(0, 5, "TIME", "mdelay_time")
qlinks("Mods", ["mod1_src1", "mod1_src2", "mod1_op", "mod1_par", "mod2_src1", "mod2_src2", "mod2_op", "mod2_par",
                "mod3_src1", "mod3_src2", "mod3_op", "mod3_par", "mod4_src1", "mod4_src2", "mod4_op", "mod4_par"])
qlinks("Delay", ["mdelay_src", "mdelay_time"])

if __name__ == "__main__":
    import os
    banks = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "vst", "layout.banks.conf")).read()   # the SOUNDS tab: hand-written lists
    sys.stdout.write(GLOBAL_TEXT + "\n\n" + banks.rstrip("\n") + "\n" + "\n".join(OSC + WAVE + FILT + ENV + LFO + MAT + MODS) + "\n")
