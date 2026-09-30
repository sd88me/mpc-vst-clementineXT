/* Clementine engine: 10 voices at 40 kHz -> out.c resampler. Oscillators read the firmware's mip tables (waves.c, wavedata.c),
 * pitch follows the measured keytrack/tuning; envelope timing and output gain are placeholders until calibrated. */
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
#include "wavedata.h"

#define NV 10
#define CORE_HZ 40000.0f

typedef struct { int note, on, vel, stage; float ph1, ph2, env; } voice_t;
enum { ST_ATT, ST_DEC, ST_SUS, ST_REL };
typedef struct {
    patch_t cur;                 /* the engine state is the XT's SDATA block */
    patch_t bank[256];           /* A001..B128 from a user .syx file, when one is found */
    int have_bank, program;
    wavedata_t *wd;
    const table_t *tab;
    voice_t v[NV];
    rs_t rs;
} inst_t;
enum { P_OSC1_OCT = 1, P_OSC1_SEMI = 2, P_OSC1_DET = 3, P_OSC1_KT = 6, P_OSC2_OCT = 12, P_OSC2_SEMI = 13, P_OSC2_DET = 14,
       P_OSC2_SYNC = 16, P_OSC2_KT = 18, P_TABLE = 25, P_W1_START = 26, P_W1_PHASE = 27, P_W2_START = 36, P_W2_PHASE = 37,
       P_MIX_W1 = 47, P_MIX_W2 = 48, P_MIX_RING = 49, P_CLIP = 55, P_VOLUME = 77, P_AMP_VELO = 79,
       P_AENV_A = 119, P_AENV_D = 120, P_AENV_S = 121, P_AENV_R = 122 };

static void refresh(inst_t *s) { if (s->wd) s->tab = wavedata_table(s->wd, s->cur.d[P_TABLE]); }

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
    s->wd = wavedata_load(dir);
    if (!s->wd) { s->wd = calloc(1, sizeof *s->wd); }   /* no imported data: every table is an open-set stand-in */
    refresh(s);
    return s;
}
static void destroy(void *p) { inst_t *s = p; wavedata_free(s->wd); free(s); }

/* Phase parameter: 0 = free (random), 1..127 = 3..357 degrees. */
static float start_phase(int v) {
    if (!v) return (float)(rand() % 128);
    return (3.0f + (v - 1) * 354.0f / 126.0f) / 360.0f * 128.0f;
}

static void midi(void *p, const uint8_t *m, int len) {
    inst_t *s = p;
    if (len < 3) return;
    int st = m[0] & 0xF0, n = m[1];
    if (st == 0xB0) {   /* controllers follow the XT's Controller Number Assignment */
        if (n == 120 || n == 123) { for (int i = 0; i < NV; i++) s->v[i].on = 0; return; }
        patch_apply_cc(&s->cur, n, m[2]);
        refresh(s);
        return;
    }
    if (st == 0x90 && m[2]) {
        voice_t *v = &s->v[0];
        for (int i = 0; i < NV; i++) if (s->v[i].stage == ST_REL && s->v[i].env < v->env) v = &s->v[i];
        for (int i = 0; i < NV; i++) if (!s->v[i].on && s->v[i].env == 0) { v = &s->v[i]; break; }
        memset(v, 0, sizeof *v);
        v->note = n; v->on = 1; v->vel = m[2]; v->stage = ST_ATT;
        v->ph1 = start_phase(s->cur.d[P_W1_PHASE]); v->ph2 = start_phase(s->cur.d[P_W2_PHASE]);
    } else if (st == 0x80 || st == 0x90) {
        for (int i = 0; i < NV; i++) if (s->v[i].on && s->v[i].note == n) { s->v[i].on = 0; s->v[i].stage = ST_REL; }
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
        refresh(s);
        return;
    }
    if (!strcmp(k, "program")) {
        s->program = x < 0 ? 0 : x > 255 ? 255 : x;
        if (s->have_bank) s->cur = s->bank[s->program];   /* voices keep playing and pick the new values up next sample */
        refresh(s);
        return;
    }
    int i = patch_find(k);
    if (i < 0) return;
    const patch_field_t *f = &patch_fields[i];
    s->cur.d[i] = (uint8_t)(x < f->lo ? f->lo : x > f->hi ? f->hi : x);
    if (i == P_TABLE) refresh(s);
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

/* Semitones from A (note 69 = 440 Hz at +100% keytrack): keytrack pivots on note 64, so a keytrack of 0 holds note 64's pitch.
 * Octave and semitone are stored as 64 + offset, detune as 64 + n/128 semitone (all measured/from the manual). */
static float osc_hz(const patch_t *p, int note, int oct_i, int semi_i, int det_i, int kt_i) {
    float kt = (-100.0f + 300.0f * p->d[kt_i] / 72.0f) / 100.0f;
    float st = (note - 64) * kt - 5.0f + (p->d[oct_i] - 64) + (p->d[semi_i] - 64) + (p->d[det_i] - 64) / 128.0f;
    return 440.0f * exp2f(st / 12.0f);
}

/* One oscillator sample: pick the mip level whose harmonics still fit under 20 kHz, then interpolate linearly. */
static float osc_read(const int8_t *mip, float ph, float hz) {
    int lvl = 0;
    while (lvl < 7 && (64 >> lvl) * hz > 20000.0f && (64 >> lvl) > 1) lvl++;
    static const int off[8] = { 0, 128, 192, 224, 240, 248, 252, 254 };
    int n = 128 >> lvl;
    float x = ph * n / 128.0f; int i = (int)x; float fr = x - i;
    const int8_t *w = mip + off[lvl];
    return ((1 - fr) * w[i % n] + fr * w[(i + 1) % n]) / 128.0f;
}

/* Placeholder envelope timing: exponential in the 0..127 rate value (to be calibrated against the firmware). */
static float env_step(int r) { return 1.0f / (CORE_HZ * 0.001f * expf(r * 0.075f)); }

static float clip(float x, int overflow) {
    if (x > 1) return overflow ? (x > 3 ? 1 : 2 - x) : 1;
    if (x < -1) return overflow ? (x < -3 ? -1 : -2 - x) : -1;
    return x;
}

/* one 40 kHz core sample */
static float core(inst_t *s) {
    const patch_t *p = &s->cur;
    float sum = 0;
    for (int i = 0; i < NV; i++) {
        voice_t *v = &s->v[i];
        if (v->stage == ST_REL && v->env <= 1e-5f) { v->env = 0; v->on = 0; }
        if (!v->on && v->env == 0) continue;
        switch (v->stage) {
        case ST_ATT: v->env += env_step(p->d[P_AENV_A]); if (v->env >= 1) { v->env = 1; v->stage = ST_DEC; } break;
        case ST_DEC: v->env -= env_step(p->d[P_AENV_D]); if (v->env <= p->d[P_AENV_S] / 127.0f) { v->env = p->d[P_AENV_S] / 127.0f; v->stage = ST_SUS; } break;
        case ST_REL: v->env -= env_step(p->d[P_AENV_R]) * v->env * 4; break;
        default: break;
        }
        float hz1 = osc_hz(p, v->note, P_OSC1_OCT, P_OSC1_SEMI, P_OSC1_DET, P_OSC1_KT);
        float hz2 = osc_hz(p, v->note, P_OSC2_OCT, P_OSC2_SEMI, P_OSC2_DET, P_OSC2_KT);
        float w1 = osc_read(s->tab->mip[p->d[P_W1_START]], v->ph1, hz1);
        float w2 = osc_read(s->tab->mip[p->d[P_W2_START]], v->ph2, hz2);
        v->ph1 += 128.0f * hz1 / CORE_HZ;
        if (v->ph1 >= 128) { v->ph1 -= 128; if (p->d[P_OSC2_SYNC]) v->ph2 = start_phase(p->d[P_W2_PHASE]); }
        v->ph2 += 128.0f * hz2 / CORE_HZ; if (v->ph2 >= 128) v->ph2 -= 128;
        float mix = (w1 * p->d[P_MIX_W1] + w2 * p->d[P_MIX_W2] + w1 * w2 * p->d[P_MIX_RING]) / 128.0f;
        mix = clip(mix, p->d[P_CLIP]);
        float a = (p->d[P_AMP_VELO] - 64) / 64.0f;
        float vg = a >= 0 ? 1 - a * (1 - v->vel / 127.0f) : 1 + a * (v->vel / 127.0f);
        sum += mix * v->env * vg;
    }
    return sum * 0.1125f * (p->d[P_VOLUME] / 127.0f);   /* placeholder gain: matches the firmware at mid pitch (docs/CALIBRATION.md) */
}

/* Test hook: render the 40 kHz core directly (not exported from the plugin). */
void clementine_render40k(void *inst, float *out, int n) { for (int i = 0; i < n; i++) out[i] = core(inst); }

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
