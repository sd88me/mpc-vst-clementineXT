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

/* ---- resonant section: pole frequency and Q as a function of cutoff AND resonance ----
 * Free (fp, Q) fits of the firmware's 12 dB LP at every measured (cutoff, resonance) fit to 0.15-0.3 dB, and both move with cutoff
 * and resonance: Q at a given resonance is higher at high cutoffs, and the pole sits on the nominal semitone law (440 Hz * 2^((c-64)/12))
 * at high resonance but above it at low resonance (1.5x at cutoff 112, resonance 0). Grid values from those fits. */
#define NC 8
#define NR 5
static const float C_GRID[NC] = { 40, 56, 72, 80, 88, 96, 104, 112 };
static const float R_GRID[NR] = { 0, 32, 64, 96, 104 };
static const float FPR[NC][NR] = {
    { 1.08f, 1.02f, 0.98f, 0.96f, 0.96f }, { 1.04f, 1.02f, 1.00f, 1.00f, 1.00f }, { 1.06f, 1.04f, 1.02f, 1.00f, 1.00f }, { 1.08f, 1.06f, 1.04f, 1.00f, 1.00f },
    { 1.14f, 1.10f, 1.06f, 1.02f, 1.00f }, { 1.24f, 1.16f, 1.08f, 1.02f, 1.02f }, { 1.36f, 1.22f, 1.12f, 1.04f, 1.02f }, { 1.50f, 1.28f, 1.14f, 1.04f, 1.02f } };
static const float QTAB[NC][NR] = {
    { 0.52f, 0.66f, 0.98f, 2.14f, 3.02f }, { 0.50f, 0.66f, 1.08f, 2.87f, 4.68f }, { 0.50f, 0.70f, 1.14f, 3.33f, 5.97f }, { 0.50f, 0.70f, 1.19f, 3.49f, 5.97f },
    { 0.50f, 0.73f, 1.19f, 3.67f, 7.26f }, { 0.50f, 0.73f, 1.32f, 3.85f, 7.26f }, { 0.50f, 0.77f, 1.45f, 4.46f, 8.01f }, { 0.50f, 0.89f, 1.76f, 5.42f, 11.26f } };
/* second section of the 24 dB LP: extra frequency factor at resonance 0 (audible-range fits), fading out with resonance */
static const float R24_C[11] = { 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127 };
static const float R24_V[11] = { 1.02f, 1.02f, 1.02f, 1.04f, 1.08f, 1.24f, 1.70f, 2.88f, 1.86f, 1.22f, 1.0f };

static float g_res[128][128], k_res[128][128], g_res24[128][128];
static int res_ready;

static float lerp_grid(const float *grid, int n, float x, int *i0) {   /* index of the lower node and the fraction */
    if (x <= grid[0]) { *i0 = 0; return 0; }
    if (x >= grid[n - 1]) { *i0 = n - 2; return 1; }
    int i = 0; while (i + 2 < n && x > grid[i + 1]) i++;
    *i0 = i; return (x - grid[i]) / (grid[i + 1] - grid[i]);
}

static void res_at(int c, int r, float *fp, float *q) {
    int ci, ri;
    float fc = lerp_grid(C_GRID, NC, (float)c, &ci);
    float rr = r > 104 ? 104.0f : (float)r, fr = lerp_grid(R_GRID, NR, rr, &ri);
    float ratio = 0, lq = 0;
    for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
        float w = (a ? fc : 1 - fc) * (b ? fr : 1 - fr);
        ratio += w * FPR[ci + a][ri + b]; lq += w * logf(QTAB[ci + a][ri + b]);
    }
    *fp = 440.0f * exp2f((c - 64) / 12.0f) * ratio;
    float qq = expf(lq);
    if (r > 104) {   /* self-oscillation region: damping falls to ~0 at 113 (manual) */
        float k104 = 1.0f / qq, t = (r - 104) / 9.0f;
        float k = t >= 1 ? 0.004f : k104 + (0.004f - k104) * t;
        qq = 1.0f / k;
    }
    *q = qq;
}

static void build_res(void) {
    for (int c = 0; c < 128; c++) {
        int i0; float f = lerp_grid(R24_C, 11, (float)c, &i0);
        float r24 = c < R24_C[0] ? 1.0f : R24_V[i0] + f * (R24_V[i0 + 1] - R24_V[i0]);
        for (int r = 0; r < 128; r++) {
            float fp, q; res_at(c, r, &fp, &q);
            float adj = 1.0f + (r24 - 1.0f) * (r >= 104 ? 0.0f : 1.0f - r / 104.0f);
            g_res[c][r] = tanf(3.14159265f * fminf(fp, 19500.0f) / FS);
            g_res24[c][r] = tanf(3.14159265f * fminf(fp * adj, 19500.0f) / FS);
            k_res[c][r] = 1.0f / q;
        }
    }
    res_ready = 1;
}

/* Bilinear lookup of a [128][128] table at fractional cutoff and resonance. */
static float tab2(const float t[128][128], float c, float r) {
    c = c < 0 ? 0 : c > 126.999f ? 126.999f : c;
    r = r < 0 ? 0 : r > 126.999f ? 126.999f : r;
    int ci = (int)c, ri = (int)r; float fc = c - ci, fr = r - ri;
    return (1 - fc) * ((1 - fr) * t[ci][ri] + fr * t[ci][ri + 1]) + fc * ((1 - fr) * t[ci + 1][ri] + fr * t[ci + 1][ri + 1]);
}

void filt_res_coefs(float cutoff, float reso, float *g, float *k, float *g24) {
    if (!res_ready) build_res();
    *g = tab2(g_res, cutoff, reso); *k = tab2(k_res, cutoff, reso); *g24 = tab2(g_res24, cutoff, reso);
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
    case 0: { /* 24 dB LP: a critically damped section, then the resonant one */
        float gr, kr, g24;
        filt_res_coefs(cutoff, reso, &gr, &kr, &g24);
        svf_tick(&f->a, x, g, 2.0f, &lp, &bp, &hp);
        svf_tick(&f->b, lp, g24, kr, &lp2, &bp2, &hp2);
        return lp2;
    }
    case 1: { /* 12 dB LP */
        float gr, kr, g24;
        filt_res_coefs(cutoff, reso, &gr, &kr, &g24);
        svf_tick(&f->a, x, gr, kr, &lp, &bp, &hp);
        return lp;
    }
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

/* Filter 2: 6 dB slope, no resonance. Pole frequency per cutoff value, fitted to the firmware's one-pole responses (0.2 dB rms up to
 * cutoff 48, about 1.5 dB at the top); at cutoff 127 it is wide open. */
static const struct { float c, hz; } POLE2[] = {
    { 0, 107.6f }, { 8, 131.3f }, { 16, 202.7f }, { 24, 328.8f }, { 32, 509.9f }, { 40, 756.2f }, { 48, 1077.5f }, { 56, 1497.5f },
    { 64, 2030.0f }, { 72, 2724.5f }, { 80, 3675.0f }, { 88, 4981.8f }, { 96, 6923.8f }, { 104, 9964.8f }, { 112, 14557.6f }, { 120, 19500.0f },
};
static float filt2_g(float cutoff) {
    const int n = (int)(sizeof POLE2 / sizeof POLE2[0]);
    if (cutoff >= 127) return 1e4f;   /* open */
    if (cutoff <= 0) return tanf(3.14159265f * POLE2[0].hz / FS);
    if (cutoff >= POLE2[n - 1].c) return tanf(3.14159265f * POLE2[n - 1].hz / FS);
    int i = (int)(cutoff / 8); float t = (cutoff - POLE2[i].c) / 8.0f;
    return tanf(3.14159265f * POLE2[i].hz * powf(POLE2[i + 1].hz / POLE2[i].hz, t) / FS);
}

float filter2_run(filt_t *f, int hp, float x, float cutoff) {
    float g = filt2_g(cutoff), a = g / (1.0f + g);
    float v = (x - f->f2) * a, lp = v + f->f2;
    f->f2 = lp + v;
    return hp ? x - lp : lp;
}
