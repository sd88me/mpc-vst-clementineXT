#include <math.h>
#include <string.h>
#include "filter.h"
#include "fx.h"

#define FS 40000.0f
#define TWO_PI 6.2831853f
#define N_MASK (FX_MAX_DELAY - 1)

static float dread(const float *line, int wr, float delay, int mask) {
    float pos = (float)wr - delay;
    while (pos < 0) pos += (float)(mask + 1);
    int i = (int)pos; float fr = pos - i;
    return line[i & mask] * (1 - fr) + line[(i + 1) & mask] * fr;
}

/* Mix parameter: shown as dry:wet = (127-m):m in the manual. */
static void dry_wet(int m, float *dry, float *wet) { *wet = m / 127.0f; *dry = 1.0f - *wet; }

static float tri(float ph) { return ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph; }

void chorus_run(fx_t *f, int mode, float *l, float *r) {
    if (!mode) return;
    float in[2] = { *l, *r }, out[2];
    f->lfo_chorus += 0.5f / FS; if (f->lfo_chorus >= 1) f->lfo_chorus -= 1;
    for (int c = 0; c < 2; c++) f->cdl[c][f->cwr] = in[c];
    for (int c = 0; c < 2; c++) {
        /* two short delays, modulated by a sine of about 0.5 Hz (manual); centre 12 ms and 18 ms, +-3 ms; the sides in antiphase */
        float ph = f->lfo_chorus + (c ? 0.5f : 0.0f);
        float m1 = sinf(TWO_PI * ph), m2 = sinf(TWO_PI * (ph + 0.25f));
        float d1 = (12.0f + 3.0f * m1) * 0.001f * FS, d2 = (18.0f + 3.0f * m2) * 0.001f * FS;
        out[c] = 0.5f * in[c] + 0.35f * (dread(f->cdl[c], f->cwr, d1, 2047) + dread(f->cdl[c], f->cwr, d2, 2047));
    }
    f->cwr = (f->cwr + 1) & 2047;
    *l = out[0]; *r = out[1];
}

static float speaker(fx_t *f, int c, float x, int amp) {
    /* Direct, Combo, Medium, Stack: progressively wider band limiting */
    static const float lo[4] = { 0, 180, 100, 60 }, hi[4] = { 0, 4500, 6500, 9000 };
    if (!amp) return x;
    float a = expf(-TWO_PI * hi[amp] / FS), b = expf(-TWO_PI * lo[amp] / FS);
    f->od_lp[c] = f->od_lp[c] * a + x * (1 - a);
    f->od_hp[c] = f->od_hp[c] * b + f->od_lp[c] * (1 - b);
    return f->od_lp[c] - f->od_hp[c];
}

void fx_run(fx_t *f, int type, int p1, int p2, int p3, float tempo_bpm, float *l, float *r) {
    float in[2] = { *l, *r }, out[2] = { *l, *r };
    float dry, wet;
    (void)tempo_bpm;
    for (int c = 0; c < 2; c++) f->dl[c][f->wr] = in[c];
    switch (type) {
    case FX_CHORUS: case FX_FLANGER1: case FX_FLANGER2: {
        /* p1 speed, p2 depth (flanger 1, chorus) or feedback (flanger 2), p3 mix. Short modulated delays. */
        float rate = 0.05f + p1 / 127.0f * (type == FX_CHORUS ? 4.0f : 3.0f);
        f->lfo += rate / FS; if (f->lfo >= 1) f->lfo -= 1;
        float depth = type == FX_FLANGER2 ? 0.5f : p2 / 127.0f, fb = type == FX_FLANGER2 ? p2 / 127.0f * 0.9f : 0.0f;
        float base = type == FX_CHORUS ? 0.012f : 0.0025f, swing = type == FX_CHORUS ? 0.008f : 0.0022f;
        dry_wet(p3, &dry, &wet);
        for (int c = 0; c < 2; c++) {
            float m = type == FX_CHORUS ? sinf(TWO_PI * (f->lfo + 0.25f * c)) : tri(fmodf(f->lfo + 0.25f * c, 1.0f));
            float d = (base + swing * depth * m) * FS;
            float w = dread(f->dl[c], f->wr, d, N_MASK);
            if (fb != 0.0f) f->dl[c][f->wr] = in[c] + fb * w;   /* feedback into the line */
            out[c] = dry * in[c] + wet * w;
        }
        break;
    }
    case FX_WAH_LP: case FX_WAH_BP: {
        /* p1 sense, p2 minimum cutoff, p3 resonance: a filter whose cutoff follows the signal level */
        for (int c = 0; c < 2; c++) {
            float a = fabsf(in[c]);
            f->env[c] += (a - f->env[c]) * (a > f->env[c] ? 0.01f : 0.0005f);
            float cutoff = p2 * 0.8f + p1 / 127.0f * 60.0f * fminf(f->env[c] * 8.0f, 1.0f);
            float g = filt_pole_g(cutoff), k = filt_damping((float)p3);
            float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            float v3 = in[c] - f->svf_ic2[c], v1 = a1 * f->svf_ic1[c] + a2 * v3, v2 = f->svf_ic2[c] + a2 * f->svf_ic1[c] + a3 * v3;
            f->svf_ic1[c] = 2 * v1 - f->svf_ic1[c]; f->svf_ic2[c] = 2 * v2 - f->svf_ic2[c];
            out[c] = type == FX_WAH_LP ? v2 : k * v1;
        }
        break;
    }
    case FX_OVERDRIVE: {
        /* p1 drive, p2 output gain, p3 speaker type (Direct, Combo, Medium, Stack in quarters of the range) */
        float pre = 1.0f + p1 / 127.0f * 24.0f, post = p2 / 127.0f * 1.5f;
        int amp = p3 * 4 / 128;
        for (int c = 0; c < 2; c++) out[c] = speaker(f, c, tanhf(pre * in[c]) * post, amp);
        break;
    }
    case FX_AMPMOD: {
        /* p1 speed, p2 spread between left and right, p3 mix; a tremolo while the dry level is above half, a ring modulator below */
        f->lfo += (0.1f + p1 / 127.0f * 20.0f) / FS; if (f->lfo >= 1) f->lfo -= 1;
        dry_wet(p3, &dry, &wet);
        for (int c = 0; c < 2; c++) {
            float m = sinf(TWO_PI * (f->lfo + (c ? p2 / 127.0f * 0.5f : 0.0f)));
            out[c] = dry * in[c] + wet * in[c] * m;
        }
        break;
    }
    case FX_DELAY: case FX_PANDELAY: case FX_MODDELAY: {
        float time = 0.12f * exp2f((p1 - 64) / 36.0f);   /* measured: 35 ms at 0, 120 ms at 64, 0.40 s at 127 (independent of the tempo setting) */
        float fb = type == FX_MODDELAY ? 0.35f : p2 * 0.744f / 127.0f;   /* measured: repeat ratio 0.744 * p / 127 */
        float d = time * FS;
        if (type == FX_MODDELAY) {
            f->lfo += (0.05f + p2 / 127.0f * 5.0f) / FS; if (f->lfo >= 1) f->lfo -= 1;
            d += sinf(TWO_PI * f->lfo) * p3 / 127.0f * 0.005f * FS;
        }
        if (d > FX_MAX_DELAY - 2) d = FX_MAX_DELAY - 2;
        if (type == FX_MODDELAY) { dry = 0.5f; wet = 0.5f; } else { wet = p3 / 127.0f; dry = 1.0f - wet; }   /* measured: linear dry:wet */
        float w0 = dread(f->dl[0], f->wr, d, N_MASK), w1 = dread(f->dl[1], f->wr, d, N_MASK);
        if (type == FX_PANDELAY) {   /* the first repeat is on the right, then left, right...; feedback closes after the left repeat (measured) */
            f->dl[0][f->wr] = 0.5f * (in[0] + in[1]) + fb * w1;
            f->dl[1][f->wr] = w0;
            out[0] = dry * in[0] + wet * w1; out[1] = dry * in[1] + wet * w0;
            break;
        } else { f->dl[0][f->wr] = in[0] + fb * w0; f->dl[1][f->wr] = in[1] + fb * w1; }
        out[0] = dry * in[0] + wet * w0; out[1] = dry * in[1] + wet * w1;
        break;
    }
    default: break;
    }
    f->wr = (f->wr + 1) & N_MASK;
    *l = out[0]; *r = out[1];
}
