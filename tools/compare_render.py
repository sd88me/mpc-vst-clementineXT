#!/usr/bin/env python3
"""Compare a firmware render (tools/oracle render) with ours (test/render_cmp): harmonic levels and RMS.
   compare_render.py <oracle.f32> <ours.f32> <note> [start_sample]  -- 40 kHz float32, mono"""
import math, struct, sys

SR = 40000

def load(p):
    d = open(p, 'rb').read()
    return list(struct.unpack('<%df' % (len(d) // 4), d))

def mag(seg, f):
    w = 2 * math.pi * f / SR
    re = im = 0.0
    n = len(seg)
    for i, v in enumerate(seg):
        win = 0.5 - 0.5 * math.cos(2 * math.pi * i / n)
        re += v * win * math.cos(w * i)
        im += v * win * math.sin(w * i)
    return 2 * math.hypot(re, im) / n * 2   # x2 window gain

def main():
    a, b, note = load(sys.argv[1]), load(sys.argv[2]), int(sys.argv[3])
    start = int(sys.argv[4]) if len(sys.argv) > 4 else 12000
    sa, sb = a[start:start + 8192], b[start:start + 8192]
    f0 = 440 * 2 ** ((note - 69) / 12)
    rms = lambda s: math.sqrt(sum(v * v for v in s) / len(s))
    print('note %d f0 %.1f Hz  rms oracle %.4f ours %.4f  ratio ours/oracle %.3f' % (note, f0, rms(sa), rms(sb), rms(sb) / max(rms(sa), 1e-9)))
    for k in range(1, 13):
        if f0 * k > 19000: break
        ma, mb = mag(sa, f0 * k), mag(sb, f0 * k)
        print('  h%-2d oracle %.5f  ours %.5f  diff %+5.1f dB' % (k, ma, mb, 20 * math.log10(max(mb, 1e-9) / max(ma, 1e-9))))

main()
