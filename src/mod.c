#include <math.h>
#include "mod.h"

#define FS 40000.0f

float lfo_rate_hz(float rate) { return 0.02608f * exp2f(rate / 12.0f); }

float mod_amount_gain(int amount) {
    int a = amount - 64;
    if (a == 0) return 0.0f;
    float m = exp2f(((a < 0 ? -a : a) - 32) / 4.0f);
    return (a < 0 ? -2.0f : 2.0f) * m;
}

static float rnd(lfo_t *l) {   /* -1..1 */
    l->rng = l->rng * 1664525u + 1013904223u;
    return ((int32_t)l->rng) / 2147483648.0f;
}

void lfo_reset(lfo_t *l, uint32_t seed, int free_phase, float delay_s) {
    l->rng = seed * 2654435761u + 12345u;
    l->phase = free_phase ? (rnd(l) * 0.5f + 0.5f) : 0.0f;
    l->held = rnd(l); l->target = rnd(l);
    l->delay_left = delay_s; l->hz_c = 0; l->rate_c = 0;
}

/* Symmetry warps the phase so the rising half takes more (positive) or less (negative) of the cycle. */
static float warp(float p, int symmetry) {
    float hp = 0.5f + (symmetry - 64) / 128.0f * 0.98f;   /* 0.01..0.99 */
    return p < hp ? 0.5f * p / hp : 0.5f + 0.5f * (p - hp) / (1.0f - hp);
}

float lfo_eval(const lfo_t *l, int shape, int symmetry) {
    float p = warp(l->phase, symmetry);
    switch (shape) {
    case LFO_SIN: return sinf(6.2831853f * p);
    case LFO_TRI: return p < 0.25f ? 4 * p : p < 0.75f ? 2 - 4 * p : 4 * p - 4;
    case LFO_SQR: return p < 0.5f ? 1.0f : -1.0f;
    case LFO_SAW: return 2.0f * fmodf(p + 0.5f, 1.0f) - 1.0f;   /* the ramp passes through zero at the start (measured) */
    case LFO_RND: return l->held + (l->target - l->held) * l->phase;
    default:      return l->held;   /* sample & hold */
    }
}

float lfo_tick_n(lfo_t *l, int shape, float rate, int symmetry, int humanize, int n) {
    if (l->delay_left > 0) { l->delay_left -= (float)n / FS; return 0.0f; }
    if (l->hz_c == 0 || rate != l->rate_c) { l->hz_c = lfo_rate_hz(rate); l->rate_c = rate; }
    float hz = l->hz_c;
    if (humanize) hz *= 1.0f + rnd(l) * 0.002f * humanize;   /* random variation of the speed */
    l->phase += hz * n / FS;
    if (l->phase >= 1.0f) {
        l->phase -= 1.0f;
        l->held = l->target; l->target = rnd(l);
    }
    return lfo_eval(l, shape, symmetry);
}

float lfo_tick(lfo_t *l, int shape, float rate, int symmetry, int humanize) { return lfo_tick_n(l, shape, rate, symmetry, humanize, 1); }
