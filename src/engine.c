/* Phase 1 skeleton: 10 voices, one stepped 8-bit saw wave read at 40 kHz, released amp, out.c resampler. Proves the build path only. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "engine.h"
#include <dirent.h>
#include <stdio.h>
#include "out.h"
#include "patch.h"
#include "syx.h"

#define NV 10
#define CORE_HZ 40000.0f

typedef struct { int note, on; float ph, inc, env; } voice_t;
typedef struct {
    patch_t cur;                 /* the engine state is the XT's SDATA block */
    patch_t bank[256];           /* A001..B128 from a user .syx file, when one is found */
    int have_bank, program;
    int8_t wave[128];
    voice_t v[NV];
    rs_t rs;
} inst_t;
enum { P_VOLUME = 77, P_AENV_R = 122 };

static void bank_cb(const patch_t *p, int bank, int num, void *ctx) {
    inst_t *s = ctx;
    int slot = (bank & 1) << 7 | (num & 127);
    s->bank[slot] = *p;
    s->have_bank = 1;
}

/* First .syx (by name) in dir; sounds land at their own location (single dumps at 0/1 bank + number). Never on the audio thread. */
static void load_bank(inst_t *s, const char *dir) {
    if (!dir) return;
    DIR *d = opendir(dir);
    if (!d) return;
    char best[512] = "";
    for (struct dirent *e; (e = readdir(d));) {
        size_t n = strlen(e->d_name);
        if (n > 4 && !strcasecmp(e->d_name + n - 4, ".syx") && n < 256 && (!best[0] || strcmp(e->d_name, best) < 0)) strcpy(best, e->d_name);
    }
    closedir(d);
    if (!best[0]) return;
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, best);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0 && n < (8 << 20)) {
        uint8_t *buf = malloc((size_t)n);
        if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) syx_scan(buf, n, bank_cb, s);
        free(buf);
    }
    fclose(f);
}

static void *create(const char *dir) {
    inst_t *s = calloc(1, sizeof *s);
    patch_init(&s->cur);
    load_bank(s, dir);
    if (s->have_bank) s->cur = s->bank[0];
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
    if (st == 0xB0) {   /* controllers follow the XT's Controller Number Assignment */
        if (n == 120 || n == 123) { for (int i = 0; i < NV; i++) s->v[i].on = 0; return; }
        patch_apply_cc(&s->cur, n, m[2]);
        return;
    }
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
    if (!strcmp(k, "state")) {   /* "P<program> <512 hex digits of SDATA>": the whole sound, so a project reloads it as saved */
        if (val[0] != 'P') return;
        s->program = atoi(val + 1);
        const char *h = strchr(val, ' ');
        if (!h || strlen(h + 1) < 2 * PATCH_SIZE) return;
        for (int i = 0; i < PATCH_SIZE; i++) { unsigned v; sscanf(h + 1 + 2 * i, "%2x", &v); s->cur.d[i] = (uint8_t)v; }
        patch_clamp(&s->cur);
        return;
    }
    if (!strcmp(k, "program")) {
        s->program = x < 0 ? 0 : x > 255 ? 255 : x;
        if (s->have_bank) s->cur = s->bank[s->program];   /* voices keep playing and pick the new values up next sample */
        return;
    }
    int i = patch_find(k);
    if (i < 0) return;
    const patch_field_t *f = &patch_fields[i];
    s->cur.d[i] = (uint8_t)(x < f->lo ? f->lo : x > f->hi ? f->hi : x);
}
static int get_param(void *p, const char *k, char *buf, int n) {
    inst_t *s = p;
    if (!strcmp(k, "state")) {
        if (n < 2 * PATCH_SIZE + 16) return 0;
        int o = snprintf(buf, n, "P%d ", s->program);
        for (int i = 0; i < PATCH_SIZE; i++) o += snprintf(buf + o, n - o, "%02x", s->cur.d[i]);
        return o;
    }
    if (!strcmp(k, "program")) return snprintf(buf, n, "%d", s->program);
    if (!strcmp(k, "patch_name")) { char nm[PATCH_NAME_LEN + 1]; patch_get_name(&s->cur, nm); return snprintf(buf, n, "%s", nm); }
    int i = patch_find(k);
    return i < 0 ? 0 : snprintf(buf, n, "%d", s->cur.d[i]);
}

/* one 40 kHz core sample */
static float core(inst_t *s) {
    int r = s->cur.d[P_AENV_R];
    float sum = 0, rel = 1.0f - 1.0f / (20.0f + (128 - r) * (128 - r) * 0.05f);
    for (int i = 0; i < NV; i++) {
        voice_t *v = &s->v[i];
        if (!v->on && v->env < 1e-4f) { v->env = 0; continue; }
        v->ph += v->inc; if (v->ph >= 128) v->ph -= 128;
        sum += s->wave[(int)v->ph] * (1.0f / 128) * v->env;
        if (!v->on) v->env *= rel;
    }
    return sum * 0.25f * (s->cur.d[P_VOLUME] / 127.0f);
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
