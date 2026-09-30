#!/usr/bin/env python3
"""Magnitude response from an oracle `ext` noise run: filter_response.py <in.f32> <out.f32> [skip_samples]
Welch cross-spectrum, 2048-point Hann, 50% overlap. Prints dB at log-spaced frequencies (40 kHz sample rate)."""
import cmath, math, struct, sys

def load(p):
    d = open(p, 'rb').read()
    return struct.unpack('<%df' % (len(d) // 4), d)

def fft(x):
    n = len(x)
    if n == 1: return list(x)
    e, o = fft(x[0::2]), fft(x[1::2])
    t = [cmath.exp(-2j * math.pi * k / n) * o[k] for k in range(n // 2)]
    return [e[k] + t[k] for k in range(n // 2)] + [e[k] - t[k] for k in range(n // 2)]

def response(xin, xout, skip=20000, n=2048):
    win = [0.5 - 0.5 * math.cos(2 * math.pi * i / n) for i in range(n)]
    pxx = [0.0] * (n // 2 + 1); pxy = [0j] * (n // 2 + 1)
    for s in range(skip, len(xin) - n, n // 2):
        a = fft([xin[s + i] * win[i] for i in range(n)]); b = fft([xout[s + i] * win[i] for i in range(n)])
        for k in range(n // 2 + 1):
            pxx[k] += (a[k] * a[k].conjugate()).real; pxy[k] += b[k] * a[k].conjugate()
    return [abs(pxy[k]) / max(pxx[k], 1e-30) for k in range(n // 2 + 1)]

FREQS = [round(30 * (19000 / 30) ** (i / 47)) for i in range(48)]

def tsv_row(xin, xout, skip=20000, n=2048):
    h = response(xin, xout, skip, n)
    return [round(20 * math.log10(max(h[min(n // 2, round(f * n / 40000))], 1e-9)), 2) for f in FREQS]

def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--tsv':   # --tsv <in> <out> <label...>: one line of dB at FREQS
        print('\t'.join(sys.argv[4:] + [str(v) for v in tsv_row(load(sys.argv[2]), load(sys.argv[3]))]))
        return
    xin, xout = load(sys.argv[1]), load(sys.argv[2])
    skip = int(sys.argv[3]) if len(sys.argv) > 3 else 20000
    h = response(xin, xout, skip)
    n = 2048
    freqs = [31, 62, 125, 250, 500, 1000, 2000, 3000, 4000, 6000, 8000, 10000, 12000, 14000, 16000, 18000, 19500]
    print(' '.join('%6d' % f for f in freqs))
    print(' '.join('%6.1f' % (20 * math.log10(max(h[min(n // 2, round(f * n / 40000))], 1e-9))) for f in freqs))

main()
