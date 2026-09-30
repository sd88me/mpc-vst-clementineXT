#include <math.h>
#include <string.h>
#include "out.h"

static double bessel_i0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
    return s;
}

/* Kaiser-windowed sinc. Clean: cutoff 20 kHz, steep. Vintage: cutoff 17 kHz and a soft window, so some 40 kHz grit stays. */
void rs_init(rs_t *r, rs_mode_t mode) {
    memset(r, 0, sizeof *r);
    double fc = (mode == RS_VINTAGE ? 17000.0 : 20000.0) / 40000.0;   /* cycles per input sample */
    double beta = mode == RS_VINTAGE ? 4.0 : 8.0, i0b = bessel_i0(beta);
    for (int p = 0; p < RS_PHASES; p++) {
        double frac = (double)p / RS_PHASES, sum = 0;
        for (int k = 0; k < RS_TAPS; k++) {
            /* tap k weights input sample (i - RS_TAPS/2 + 1 + k); output time is i + frac */
            double x = (k - RS_TAPS / 2 + 1) - frac, w = x / (RS_TAPS / 2);
            double win = fabs(w) < 1 ? bessel_i0(beta * sqrt(1 - w * w)) / i0b : 0;
            double s = fabs(x) < 1e-9 ? 2 * fc : sin(2 * M_PI * fc * x) / (M_PI * x);
            r->bank[p][k] = (float)(s * win);
            sum += s * win;
        }
        for (int k = 0; k < RS_TAPS; k++) r->bank[p][k] /= (float)sum;   /* unity DC gain per phase */
    }
    r->need = RS_TAPS;   /* prime the history */
}

static void push(rs_t *r, const float *lr) {
    r->hist[r->wr][0] = r->hist[r->wr + RS_TAPS][0] = lr[0];
    r->hist[r->wr][1] = r->hist[r->wr + RS_TAPS][1] = lr[1];
    r->wr = (r->wr + 1) % RS_TAPS;
}

void rs_render(rs_t *r, rs_gen_t gen, void *ctx, float *out, int frames) {
    for (int n = 0; n < frames; n++) {
        while (r->need > 0) { float f[2]; gen(ctx, f); push(r, f); r->need--; }
        const float *b = r->bank[r->phase];
        const float (*h)[2] = &r->hist[r->wr];   /* oldest sample first */
        float l = 0, rr = 0;
        for (int k = 0; k < RS_TAPS; k++) { l += b[k] * h[k][0]; rr += b[k] * h[k][1]; }
        out[2 * n] = l; out[2 * n + 1] = rr;
        r->phase += RS_STEP;
        while (r->phase >= RS_PHASES) { r->phase -= RS_PHASES; r->need++; }
    }
}
