#!/usr/bin/env python3
"""Pitch of a mono float32 40 kHz file in cents relative to a note: measure_pitch.py <file> <note> [start_sample]"""
import math, struct, sys
d = open(sys.argv[1], 'rb').read(); x = struct.unpack('<%df' % (len(d) // 4), d)
note = int(sys.argv[2]); start = int(sys.argv[3]) if len(sys.argv) > 3 else 16000
seg = x[start:start + 16384]; f0 = 440 * 2 ** ((note - 69) / 12)
def mag(f):
    w = 2 * math.pi * f / 40000; re = im = 0.0
    for i, v in enumerate(seg):
        win = 0.5 - 0.5 * math.cos(2 * math.pi * i / len(seg)); re += v * win * math.cos(w * i); im += v * win * math.sin(w * i)
    return re * re + im * im
fr = [20 * 1.004 ** i for i in range(1700) if 20 * 1.004 ** i < 9000]
b = max((mag(f), f) for f in fr)[1]
b = max((mag(b * (1 + d / 2000.0)), b * (1 + d / 2000.0)) for d in range(-10, 11))[1]
print('%.0f' % (1200 * math.log2(b / f0)))
