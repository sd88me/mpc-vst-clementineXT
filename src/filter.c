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

void filt_init(void) { if (!g_ready) build_tables(); if (!res_ready) build_res(); }

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

/* Filter coefficients depend only on (type, cutoff, resonance, special), which change slowly: they are recomputed only when one of
 * them moves (the lookups and tan/pow calls are far too expensive to do at 40 kHz on the 32-bit ARM devices). */
static void filt_prep(filt_t *f, int type, float cutoff, float reso, int special) {
    f->kt = type; f->kc = cutoff; f->kr = reso; f->ks = special; f->kvalid = 1;
    f->cg = filt_pole_g(cutoff); f->ck = filt_damping(reso);
    filt_res_coefs(cutoff, reso, &f->cgr, &f->ckr, &f->cg24);
    switch (type) {
    case 2:
        f->cgh = tanf(0.745f * atanf(f->cg)); f->cgq = tanf(0.745f * atanf(f->cgr)); f->cgl = fminf(f->cg * 4.7f, 5.0f);
        { float cdb = cutoff <= 72 ? 4.5f : cutoff <= 96 ? 4.5f + (cutoff - 72) * 0.096f : 6.8f + (cutoff - 96) * 0.23f;   /* level vs the LP passband, measured */
          f->cgain = powf(10.0f, cdb / 20.0f); }
        break;
    case 7: { float g24; filt_res_coefs(cutoff + (special - 64), reso, &f->cgr2, &f->ckr2, &g24); break; }
    case 12: f->cgx = filt_pole_g(cutoff + special * 0.25f); break;
    default: break;
    }
}

/* Calibrated: types 0-4, 7, 10, 11 fitted to the firmware (docs/CALIBRATION.md); 5, 6, 8, 9, 12 are rough. */
float filter1_run(filt_t *f, int type, float x, float cutoff, float reso, int special) {
    if (!f->kvalid || type != f->kt || cutoff != f->kc || reso != f->kr || special != f->ks) filt_prep(f, type, cutoff, reso, special);
    float g = f->cg, k = f->ck, gr = f->cgr, kr = f->ckr, lp, bp, hp, lp2, bp2, hp2;
    switch (type) {
    case 0: /* 24 dB LP: a critically damped section, then the resonant one */
        svf_tick(&f->a, x, g, 2.0f, &lp, &bp, &hp);
        svf_tick(&f->b, lp, f->cg24, kr, &lp2, &bp2, &hp2);
        return lp2;
    case 1: /* 12 dB LP */
        svf_tick(&f->a, x, gr, kr, &lp, &bp, &hp);
        return lp;
    case 2: { /* 24 dB BP (fitted): one-pole HP and the resonant 2-pole LP at 0.745x the 12 dB LP's pole, then a critically damped 2-pole LP at
               * 4.7x the nominal pole (capped below Nyquist) */
        float gh = f->cgh;
        float v = (x - f->c.ic1) * (gh / (1.0f + gh)), l1 = v + f->c.ic1; f->c.ic1 = l1 + v;   /* TPT one-pole low-pass state */
        svf_tick(&f->a, x - l1, f->cgq, kr, &lp, &bp, &hp);
        svf_tick(&f->b, lp, f->cgl, 2.0f, &lp2, &bp2, &hp2);
        return lp2 * f->cgain;
    }
    case 3: /* 12 dB BP (fitted): twice the raw band-pass output of the 12 dB LP's (pole, Q) section, so the peak gain is 2Q */
        svf_tick(&f->a, x, gr, kr, &lp, &bp, &hp);
        return 2.0f * bp;
    case 4: /* 12 dB HP: the 12 dB LP's (pole, Q) set as a high-pass, then a fixed critically damped 2-pole LP near 12.5 kHz (fitted, 0.1-1.2 dB rms) */
        svf_tick(&f->a, x, gr, kr, &lp, &bp, &hp);
        svf_tick(&f->b, hp, 1.5f, 2.0f, &lp2, &bp2, &hp2);
        return lp2;
    case 5:   /* sine waveshaper (about +9.5 dB small-signal) then 12 dB LP */
        svf_tick(&f->a, sinf(3.0f * x), g, k, &lp, &bp, &hp);
        return lp;
    case 6:   /* 12 dB LP then waveshaper; the shaping wave is not modelled yet (soft clip stands in) */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        return tanhf(6.0f * lp) * 0.17f;
    case 7: /* dual: half the 12 dB LP plus the raw band-pass of a second section moved by (special - 64) steps */
        svf_tick(&f->a, x, gr, kr, &lp, &bp, &hp);
        svf_tick(&f->b, x, f->cgr2, f->ckr2, &lp2, &bp2, &hp2);
        return 0.5f * lp + 1.0f * bp2;
    case 8:   /* FM filter: the oscillator 2 FM of the cutoff is not modelled yet */
        svf_tick(&f->a, x, g, k, &lp, &bp, &hp);
        return lp;
    case 9: { /* sample and hold in front of a 12 dB LP; rate 127 passes the signal untouched */
        int period = special >= 127 ? 1 : 1 + (127 - special) / 4;
        if (++f->sphase >= period) { f->sphase = 0; f->shold = x; }
        svf_tick(&f->a, period == 1 ? x : f->shold, g, k, &lp, &bp, &hp);
        return lp;
    }
    case 10: { /* 24 dB notch (fitted): a wide notch (critically damped, at the nominal pole) and the 12 dB section's own (pole, Q) notch, unity passband */
        svf_tick(&f->a, x, g * 0.95f, 2.0f, &lp, &bp, &hp);
        float y = x - 2.0f * bp;
        svf_tick(&f->b, y, gr, kr, &lp2, &bp2, &hp2);
        return y - kr * bp2;
    }
    case 11: /* 12 dB notch (fitted, 0.1-0.6 dB rms at cutoff 48-96): the 12 dB section's (pole, Q) notch at half level */
        svf_tick(&f->a, x, gr, kr, &lp, &bp, &hp);
        return 0.5f * (x - kr * bp);
    default: /* 12: band stop (rough fit at special 64): both sections sit special/4 steps above the cutoff, passing -6 dB below and +5 dB above */
        svf_tick(&f->a, x, f->cgx, 2.0f, &lp, &bp, &hp);
        svf_tick(&f->b, x, f->cgx, 2.0f, &lp2, &bp2, &hp2);
        return 0.5f * lp + 1.8f * hp2;
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
    if (cutoff != f->f2c || f->f2a == 0) { float g = filt2_g(cutoff); f->f2c = cutoff; f->f2a = g / (1.0f + g); }
    float a = f->f2a;
    float v = (x - f->f2) * a, lp = v + f->f2;
    f->f2 = lp + v;
    return hp ? x - lp : lp;
}
