#!/usr/bin/env python3
"""Compare our Filter 1 with the firmware's measured responses (tools/oracle ext sweep -> filt.tsv).
   filter_compare.py <filt.tsv> <filter_run binary> <in.f32 (the noise the firmware saw)> [types]"""
import math, subprocess, sys, os
sys.argv_saved = sys.argv
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import importlib.util
spec = importlib.util.spec_from_file_location('fr', os.path.join(here, 'filter_response.py'))
src = open(os.path.join(here, 'filter_response.py')).read().replace('\nmain()\n', '\n')
ns = {}
exec(compile(src, 'filter_response', 'exec'), ns)
FREQS, response, load = ns['FREQS'], ns['response'], ns['load']

def shelf_db(f): return 20 * math.log10(abs((1 + 1j * f / 437.0) / (1 + 1j * f / 280.0)))

def main():
    tsv, binary, xin_path = sys.argv[1:4]
    types = [int(t) for t in sys.argv[4].split(',')] if len(sys.argv) > 4 else None
    xin = load(xin_path)
    worst = {}
    for line in open(tsv):
        p = line.rstrip('\n').split('\t')
        if len(p) < 52: continue
        t, c, r, sp = map(int, p[:4])
        if types and t not in types: continue
        fw = [float(v) for v in p[4:]]
        subprocess.run([binary, str(t), str(c), str(r), str(sp), xin_path, '/tmp/fc_out.f32'], check=True)
        h = response(xin, load('/tmp/fc_out.f32'), 20000)
        ours = [20 * math.log10(max(h[min(1024, round(f * 2048 / 40000))], 1e-9)) for f in FREQS]
        # firmware minus shelf; align the passband (mean of the lowest 6 points) and compare where the firmware is above -70 dB relative
        fwn = [a - shelf_db(f) for a, f in zip(fw, FREQS)]
        off = -19.1 if os.environ.get('ABS') else sum(fwn[i] - ours[i] for i in range(0, 6)) / 6   # ABS=1: compare levels too (the 12 dB LP passband sits at -19.1)
        errs = [(fwn[i] - off) - ours[i] for i in range(48) if fwn[i] - off > -75 and ours[i] > -75]
        bias = sum(errs) / max(len(errs), 1)
        rms = math.sqrt(sum(e * e for e in errs) / max(len(errs), 1))
        print('type %2d cutoff %3d reso %3d special %3d: rms %5.2f dB  passband offset %+.1f dB  mean error %+.1f dB' % (t, c, r, sp, rms, off, bias))
        worst.setdefault(t, []).append(rms)
    print('mean rms per type:', {t: round(sum(v) / len(v), 2) for t, v in worst.items()})

main()
