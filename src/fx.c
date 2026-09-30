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

void chorus_run(fx_t *f, int mode, float *l, float *r) {
    if (!mode) return;
    float in[2] = { *l, *r }, out[2];
    f->lfo_chorus += 0.5f / FS; if (f->lfo_chorus >= 1) f->lfo_chorus -= 1;
    for (int c = 0; c < 2; c++) f->cdl[c][f->cwr] = in[c];
    for (int c = 0; c < 2; c++) {
        /* measured: one tap, delay 128 samples * (1 + sin) (0..6.4 ms) at 0.5 Hz, the sides half a cycle apart, added to the dry signal at
         * full level (rms rises by about 1.4 for uncorrelated noise); chorus 1 and 2 behave alike */
        float d = 128.0f * (1.0f + sinf(TWO_PI * (f->lfo_chorus + 0.5f * c)));
        if (d < 1.0f) d = 1.0f;
        out[c] = in[c] + dread(f->cdl[c], f->cwr, d, 2047);
    }
    f->cwr = (f->cwr + 1) & 2047;
    *l = out[0]; *r = out[1];
}


void fx_run(fx_t *f, int type, int p1, int p2, int p3, float tempo_bpm, float *l, float *r) {
    float in[2] = { *l, *r }, out[2] = { *l, *r };
    float dry, wet;
    (void)tempo_bpm;
    for (int c = 0; c < 2; c++) f->dl[c][f->wr] = in[c];
    switch (type) {
    case FX_CHORUS: case FX_FLANGER1: case FX_FLANGER2: {
        /* p1 speed, p2 depth (chorus, flanger 1) or feedback (flanger 2), p3 mix. Measured with the oracle (docs/CALIBRATION.md): the LFO
         * is a sine at 0.0167*2^(p1/12) Hz, the delay is 128 samples * (1 + depth*sin) for the chorus, depth*128*(1 + sin) for flanger 1 and
         * 128*(1 + sin) for flanger 2, the right side runs half a cycle later; the wet signal is (x + delayed)/2 for chorus and flanger 1 and
         * half the delayed signal (with feedback) for flanger 2. */
        f->lfo += 0.0167f * exp2f(p1 / 12.0f) / FS; if (f->lfo >= 1) f->lfo -= 1;
        float depth = p2 / 127.0f, fb = type == FX_FLANGER2 ? 0.83f * p2 / 127.0f : 0.0f;
        dry_wet(p3, &dry, &wet);
        for (int c = 0; c < 2; c++) {
            float sn = sinf(TWO_PI * (f->lfo + 0.5f * c));
            float d = type == FX_CHORUS ? 128.0f * (1.0f + depth * sn) : type == FX_FLANGER1 ? 128.0f * depth * (1.0f + sn) : 128.0f * (1.0f + sn);
            if (d < 1.0f) d = 1.0f;
            float w = dread(f->dl[c], f->wr, d, N_MASK);
            if (fb != 0.0f) f->dl[c][f->wr] = in[c] + fb * w;   /* feedback into the line */
            float wetsig = type == FX_FLANGER2 ? 0.5f * w : 0.5f * (in[c] + w);
            out[c] = dry * in[c] + wet * wetsig;
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
        /* Measured (docs/CALIBRATION.md): hard clip of (1+drive)*x at +-0.1915 (firmware output units), scaled by 6.5*gain/(50+1.57*drive).
         * The amp type (p3) changed neither level nor harmonic content in any test, so it is ignored. */
        float d = 1.0f + p1, g = p2 <= 64 ? p2 / 64.0f : 1.0f + (p2 - 64) / 63.0f * 0.874f;
        float k = 6.5f * g / (50.0f + 1.57f * p1);
        for (int c = 0; c < 2; c++) { float y = d * in[c]; y = y > 0.1915f ? 0.1915f : y < -0.1915f ? -0.1915f : y; out[c] = k * y; }
        break;
    }
    case FX_AMPMOD: {
        /* p1 speed, p2 spread between left and right, p3 mix; a tremolo while the dry level is above half, a ring modulator below */
        f->lfo += 0.0167f * exp2f(p1 / 12.0f) / FS;   /* measured: 0.0167*2^(p/12) Hz, the left/right offset is spread/127 of half a cycle */
        if (f->lfo >= 1) f->lfo -= 1;
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
