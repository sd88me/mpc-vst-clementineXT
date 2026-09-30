#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/out.h"

static double hz, ph;
static void gen(void *c, float *lr) { (void)c; lr[0] = lr[1] = 0.5f * sinf((float)ph); ph += 2 * M_PI * hz / 40000; }

/* Goertzel-ish single-bin magnitude at f Hz (44.1k output), amplitude-normalised */
static double bin(const float *x, int n, double f) {
    double re = 0, im = 0;
    for (int i = 0; i < n; i++) { double a = 2 * M_PI * f * i / 44100; re += x[2 * i] * cos(a); im += x[2 * i] * sin(a); }
    return 2 * sqrt(re * re + im * im) / n;
}

int main(void) {
    static rs_t r; int fails = 0;
    for (int mode = 0; mode < 2; mode++) {
        rs_init(&r, mode);
        struct { double f, lo, hi; } t[] = { {1000, .48, .52}, {10000, .48, .52}, {15000, .45, .52} };
        for (int i = 0; i < 3; i++) {
            hz = t[i].f; ph = 0; rs_init(&r, mode);
            enum { N = 8192 }; static float o[2 * N];
            rs_render(&r, gen, NULL, o, N);
            double a = bin(o + 2 * 256, N - 256, t[i].f);   /* skip the filter start-up */
            int ok = a >= t[i].lo && a <= t[i].hi;
            printf("%s mode %d %5.0f Hz amp %.3f\n", ok ? "ok  " : "FAIL", mode, t[i].f, a); fails += !ok;
        }
        /* image of a 19 kHz tone appears at 40k-19k = 21 kHz (below output Nyquist); Clean must reject it */
        hz = 19000; ph = 0; rs_init(&r, mode);
        static float o[2 * 8192]; rs_render(&r, gen, NULL, o, 8192);
        double img = bin(o + 512, 8192 - 256, 21000), main_ = bin(o + 512, 8192 - 256, 19000);
        printf("info mode %d 19 kHz tone %.3f, 21 kHz image %.4f (%.1f dB)\n", mode, main_, img, 20 * log10(img / main_ + 1e-12));
    }
    printf(fails ? "FAILED\n" : "PASSED\n");
    return fails;
}
