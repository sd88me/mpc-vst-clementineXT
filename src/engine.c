/* Phase 1 skeleton: 10 voices, one stepped 8-bit saw wave read at 40 kHz, released amp, out.c resampler. Proves the build path only. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "out.h"

#define NV 10
#define CORE_HZ 40000.0f

typedef struct { int note, on; float ph, inc, env; } voice_t;
typedef struct { int volume, release; int8_t wave[128]; voice_t v[NV]; rs_t rs; } inst_t;

static void *create(const char *dir) {
    (void)dir;
    inst_t *s = calloc(1, sizeof *s);
    s->volume = 100; s->release = 30;
    rs_init(&s->rs, RS_CLEAN);
    for (int i = 0; i < 64; i++) s->wave[i] = (int8_t)(i * 2 - 64);   /* saw, first half */
    for (int n = 0; n < 64; n++) s->wave[64 + n] = (int8_t)-s->wave[63 - n];
    return s;
}
static void destroy(void *p) { free(p); }

static void midi(void *p, const uint8_t *m, int len) {
    inst_t *s = p;
    if (len < 3) return;
    int st = m[0] & 0xF0, n = m[1];
    if (st == 0x90 && m[2]) {
        voice_t *v = &s->v[0];
        for (int i = 0; i < NV; i++) if (!s->v[i].on && s->v[i].env < v->env) v = &s->v[i];
        v->note = n; v->on = 1; v->env = 1;
        v->inc = 128.0f * 440.0f * powf(2, (n - 69) / 12.0f) / CORE_HZ;
    } else if (st == 0x80 || st == 0x90) {
        for (int i = 0; i < NV; i++) if (s->v[i].on && s->v[i].note == n) s->v[i].on = 0;
    }
}

static void set_param(void *p, const char *k, const char *val) {
    inst_t *s = p; int x = atoi(val);
    if (!strcmp(k, "volume")) s->volume = x;
    else if (!strcmp(k, "amp_release")) s->release = x;
}
static int get_param(void *p, const char *k, char *buf, int n) {
    inst_t *s = p;
    if (!strcmp(k, "volume")) return snprintf(buf, n, "%d", s->volume);
    if (!strcmp(k, "amp_release")) return snprintf(buf, n, "%d", s->release);
    return 0;
}

/* one 40 kHz core sample */
static float core(inst_t *s) {
    float sum = 0, rel = 1.0f - 1.0f / (20.0f + (128 - s->release) * (128 - s->release) * 0.05f);
    for (int i = 0; i < NV; i++) {
        voice_t *v = &s->v[i];
        if (!v->on && v->env < 1e-4f) { v->env = 0; continue; }
        v->ph += v->inc; if (v->ph >= 128) v->ph -= 128;
        sum += s->wave[(int)v->ph] * (1.0f / 128) * v->env;
        if (!v->on) v->env *= rel;
    }
    return sum * 0.25f * (s->volume / 127.0f);
}

static void gen(void *p, float *lr) { lr[0] = lr[1] = core(p); }

static void render(void *p, int16_t *out, int frames) {
    inst_t *s = p;
    float buf[256];
    for (int done = 0; done < frames; ) {
        int n = frames - done < 128 ? frames - done : 128;
        rs_render(&s->rs, gen, s, buf, n);
        for (int i = 0; i < 2 * n; i++) out[2 * done + i] = (int16_t)fmaxf(-32767, fminf(32767, buf[i] * 32767));
        done += n;
    }
}

static const mpc_engine_t E = { create, destroy, midi, set_param, get_param, render, NULL };
const mpc_engine_t *mpc_engine(void) { return &E; }
