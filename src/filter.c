#include <math.h>
#include <string.h>
#include "filter.h"

#define FS 40000.0f

/* Effective pole frequency of a critically damped section per cutoff value, fitted to the firmware's 12 dB low-pass responses
 * (0.15 dB rms at every cutoff 32..127, docs/CALIBRATION.md). Below 32 the values are extrapolated. */
static const struct { float c, hz; } POLE[] = {
    { 0, 21.0f }, { 8, 30.0f }, { 16, 42.0f }, { 24, 59.0f }, { 32, 82.5f }, { 40, 119.9f }, { 48, 182.2f }, { 56, 285.5f },
    { 64, 456.2f }, { 72, 736.4f }, { 80, 1206.5f }, { 88, 2006.6f }, { 96, 3438.8f }, { 104, 6041.9f }, { 112, 10562.6f },
    { 120, 16220.0f }, { 127, 19500.0f },
};

/* Damping k = 1/Q per resonance value, from the firmware's 12 dB LP at cutoff 72: Q = 0.51 at 0, 1.6 at 80, 3 at 96, 11.7 at
 * 108; the filter self-oscillates above about 113 (manual). */
static const struct { float r, k; } DAMP[] = {
    { 0, 1.96f }, { 16, 1.75f }, { 32, 1.47f }, { 48, 1.19f }, { 64, 0.91f }, { 72, 0.77f }, { 80, 0.63f }, { 88, 0.48f },
    { 96, 0.33f }, { 100, 0.26f }, { 104, 0.18f }, { 106, 0.138f }, { 108, 0.086f }, { 110, 0.04f }, { 113, 0.008f }, { 127, 0.002f },
};

#define G_STEPS 8
static float g_tab[128 * G_STEPS + 2];
static int g_ready;

static void build_tables(void) {
    const int n = (int)(sizeof POLE / sizeof POLE[0]);
    for (int i = 0; i < 128 * G_STEPS + 2; i++) {
        float c = (float)i / G_STEPS, hz = POLE[n - 1].hz;
        for (int j = 0; j + 1 < n; j++)
            if (c <= POLE[j + 1].c) { float t = (c - POLE[j].c) / (POLE[j + 1].c - POLE[j].c); hz = POLE[j].hz * powf(POLE[j + 1].hz / POLE[j].hz, t); break; }
        g_tab[i] = tanf(3.14159265f * hz / FS);
    }
    g_ready = 1;
}

float filt_pole_g(float cutoff) {
    if (!g_ready) build_tables();
    if (cutoff < 0) cutoff = 0;
    if (cutoff > 127) cutoff = 127;
    float x = cutoff * G_STEPS; int i = (int)x; float f = x - i;
    return g_tab[i] + f * (g_tab[i + 1] - g_tab[i]);
}

float filt_damping(float reso) {
    const int n = (int)(sizeof DAMP / sizeof DAMP[0]);
    if (reso <= 0) return DAMP[0].k;
    for (int j = 0; j + 1 < n; j++)
        if (reso <= DAMP[j + 1].r) return DAMP[j].k + (DAMP[j + 1].k - DAMP[j].k) * (reso - DAMP[j].r) / (DAMP[j + 1].r - DAMP[j].r);
    return DAMP[n - 1].k;
}

static inline void svf_tick(svf_t *s, float x, float g, float k, float *lp, float *bp, float *hp) {
    float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    float v3 = x - s->ic2, v1 = a1 * s->ic1 + a2 * v3, v2 = s->ic2 + a2 * s->ic1 + a3 * v3;
    s->ic1 = 2 * v1 - s->ic1; s->ic2 = 2 * v2 - s->ic2;
    *lp = v2; *bp = v1; *hp = x - k * v1 - v2;
}

/* Calibrated: types 0 and 1. Everything else is a plausible structure with a level matched to the firmware's passband; the
 * shapes are within a few dB, not fitted (the firmware's band-pass and high-pass sections are not plain SVF outputs). */
float filter1_run(filt_t *f, int type, float x, float cutoff, float reso, int special) {
    float g = filt_pole_g(cutoff), k = filt_damping(reso), lp, bp, hp, lp2, bp2, hp2;
    switch (type) {
    case 0:   /* 24 dB LP */
        svf_tick(&f->a, x, g, 2.0f, &lp, &bp, &hp);
        svf_tick(&f->b, lp, g, k, &lp2, &bp2, &hp2);
        return lp2;
    case 1:   /* 12 dB LP */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        return lp;
    case 2:   /* 24 dB BP: two band-pass sections, level from the firmware (-5.5 dB vs the LP passband) */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        svf_tick(&f->b, k * bp, g, k, &lp2, &bp2, &hp2);
        return 0.53f * k * bp2;
    case 3:   /* 12 dB BP (-2.6 dB) */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        return 0.74f * k * bp;
    case 4:   /* 12 dB HP (-3.5 dB) */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        return 0.67f * hp;
    case 5:   /* sine waveshaper (about +9.5 dB small-signal) then 12 dB LP */
        svf_tick(&f->a, sinf(3.0f * x), g, k, &lp, &bp, &hp);
        return lp;
    case 6:   /* 12 dB LP then waveshaper; the shaping wave is not modelled yet (soft clip stands in) */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        return tanhf(6.0f * lp) * 0.17f;
    case 7: { /* dual: LP and BP in parallel, BP offset in semitones by the extra parameter */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        float gb = filt_pole_g(cutoff + (special - 64));
        svf_tick(&f->b, x, gb, k, &lp2, &bp2, &hp2);
        return 0.53f * (lp + k * bp2);
    }
    case 8:   /* FM filter: the oscillator 2 FM of the cutoff is not modelled yet */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        return lp;
    case 9: { /* sample and hold in front of a 12 dB LP; rate 127 passes the signal untouched */
        int period = special >= 127 ? 1 : 1 + (127 - special) / 4;
        if (++f->sphase >= period) { f->sphase = 0; f->shold = x; }
        svf_tick(&f->a, period == 1 ? x : f->shold, g, k, &lp, &bp, &hp);
        return lp;
    }
    case 10:  /* 24 dB notch: two notch sections */
        svf_tick(&f->a, x, g, 1.0f, &lp, &bp, &hp);
        svf_tick(&f->b, x - 1.0f * bp, g, 1.0f, &lp2, &bp2, &hp2);
        return (x - 1.0f * bp) - 1.0f * bp2;
    case 11:  /* 12 dB notch */
        svf_tick(&f->a, x, g, 1.0f, &lp, &bp, &hp);
        return x - 1.0f * bp;
    default:  /* 12: band stop, bandwidth from the extra parameter: LP and HP in parallel, HP moved up */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        svf_tick(&f->b, x, filt_pole_g(cutoff + special * 0.25f), k, &lp2, &bp2, &hp2);
        return lp + hp2;
    }
}

/* Filter 2: 6 dB slope, no resonance. The cutoff law is the same table until measured separately. */
float filter2_run(filt_t *f, int hp, float x, float cutoff) {
    float g = filt_pole_g(cutoff), a = g / (1.0f + g);
    float v = (x - f->f2) * a, lp = v + f->f2;
    f->f2 = lp + v;
    return hp ? x - lp : lp;
}
