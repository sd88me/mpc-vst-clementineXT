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
#include "filter.h"
#include "fx.h"

#define NV 10
#define CORE_HZ 40000.0f

typedef struct { float x1, y1; } shelf_t;
enum { ST_ATT, ST_DEC, ST_SUS, ST_REL };
typedef struct { int stage; float level; } env_t;
/* key = the played note that owns the voice; pitch = its current pitch in notes (moves during glide), target = where it glides to,
 * det = unison/dual detune in notes, panoff = its pan offset from the spread (0..1, sign = side). */
typedef struct { int key, on, vel; float ph1, ph2, pitch, target, det, panoff; env_t aenv, fenv; filt_t flt; } voice_t;
typedef struct {
    patch_t cur;                 /* the engine state is the XT's SDATA block */
    struct { int note, vel; } held[16];   /* keys currently down, oldest first */
    int nheld, pedal;
    uint8_t deferred[128];       /* note-offs waiting for the sustain pedal */
    float last_pitch;            /* pitch of the previous note, for glide */
    patch_t bank[256];           /* A001..B128 from a user .syx file, when one is found */
    int have_bank, program;
    wavedata_t *wd;
    const table_t *tab;
    voice_t v[NV];
    shelf_t shelf[2];
    fx_t fx;
    rs_t rs;
} inst_t;
enum { P_FX_TYPE = 76, P_FX_P1 = 81, P_CHORUS = 82, P_FX_P2 = 83, P_FX_P3 = 86 };
enum { P_OSC1_OCT = 1, P_OSC1_SEMI = 2, P_OSC1_DET = 3, P_OSC1_KT = 6, P_OSC2_OCT = 12, P_OSC2_SEMI = 13, P_OSC2_DET = 14,
       P_OSC2_SYNC = 16, P_OSC2_KT = 18, P_TABLE = 25, P_W1_START = 26, P_W1_PHASE = 27, P_W2_START = 36, P_W2_PHASE = 37,
       P_MIX_W1 = 47, P_MIX_W2 = 48, P_MIX_RING = 49, P_CLIP = 55, P_VOLUME = 77, P_AMP_VELO = 79, P_PAN = 84,
       P_AENV_A = 119, P_AENV_D = 120, P_AENV_S = 121, P_AENV_R = 122,
       P_F1_CUTOFF = 62, P_F1_RESO = 63, P_F1_TYPE = 64, P_F1_KT = 65, P_F1_ENV = 66, P_F1_VELO = 67, P_F1_SPECIAL = 70,
       P_F2_CUTOFF = 73, P_F2_TYPE = 74, P_F2_KT = 75, P_FENV_A = 113, P_FENV_D = 114, P_FENV_S = 115, P_FENV_R = 116 };

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

/* ---- voice allocation: poly/mono, normal/dual/unison, held keys, sustain pedal, glide ----
 * The detune spread and the glide time law are placeholders (not measured against the firmware yet). */
enum { P_GLIDE_ON = 87, P_GLIDE_TYPE = 88, P_GLIDE_MODE = 89, P_GLIDE_TIME = 90, P_ALLOC = 108, P_ASSIGN = 109, P_DETUNE = 110, P_DEPAN = 112 };

static void release_voice(voice_t *v) { v->on = 0; v->aenv.stage = ST_REL; v->fenv.stage = ST_REL; }

static int steal_voice(inst_t *s) {
    int best = 0;
    for (int i = 0; i < NV; i++) if (!s->v[i].on && s->v[i].aenv.level == 0) return i;
    for (int i = 0; i < NV; i++) if (!s->v[i].on && s->v[i].aenv.level < s->v[best].aenv.level) best = i;   /* quietest release */
    if (s->v[best].on) { best = 0; for (int i = 1; i < NV; i++) if (s->v[i].aenv.level < s->v[best].aenv.level) best = i; }
    return best;
}

static void start_voice(inst_t *s, int idx, int note, int vel, float det, float panoff, int legato) {
    voice_t *v = &s->v[idx];
    const patch_t *p = &s->cur;
    int gl = p->d[P_GLIDE_ON] && (p->d[P_GLIDE_TYPE] < 2 || legato);   /* types 2 and 3 (fingered) glide only on legato notes */
    float from = legato || (gl && s->last_pitch > 0) ? (legato ? v->pitch : s->last_pitch) : (float)note;
    memset(v, 0, sizeof *v);
    v->key = note; v->on = 1; v->vel = vel; v->det = det; v->panoff = panoff;
    v->target = (float)note;
    v->pitch = gl ? from : (float)note;
    v->aenv.stage = ST_ATT; v->fenv.stage = ST_ATT;
    v->ph1 = start_phase(p->d[P_W1_PHASE]); v->ph2 = start_phase(p->d[P_W2_PHASE]);
}

/* Spread of unison/dual voices: n voices evenly across the detune range; returns detune (notes) and pan side (-1..1). */
static void spread(const patch_t *p, int i, int n, float *det, float *pan) {
    float pos = n > 1 ? (2.0f * i / (n - 1) - 1.0f) : 0.0f;
    *det = pos * (p->d[P_DETUNE] / 127.0f) * 0.5f;   /* placeholder: +-0.25 notes at detune 127 */
    *pan = pos * (p->d[P_DEPAN] / 127.0f);
}

/* Give every held key its share of the voices (unison) or two voices each (dual), or one (normal). */
static void assign_voices(inst_t *s, int retrigger_new) {
    const patch_t *p = &s->cur;
    int mode = p->d[P_ASSIGN], mono = p->d[P_ALLOC];
    if (s->nheld == 0) return;
    if (mono) {   /* only the newest key sounds; dual uses 2 voices, unison all of them */
        int n = mode == 2 ? NV : mode == 1 ? 2 : 1, key = s->held[s->nheld - 1].note, vel = s->held[s->nheld - 1].vel;
        int legato = 0; for (int i = 0; i < NV; i++) if (s->v[i].on) legato = 1;
        for (int i = 0; i < n; i++) {
            float det, pan; spread(p, i, n, &det, &pan);
            if (legato && s->v[i].on) {   /* the voice keeps sounding: glide (or jump) to the new key and retrigger its envelopes */
                s->v[i].key = key; s->v[i].target = (float)key; s->v[i].det = det; s->v[i].panoff = pan; s->v[i].vel = vel;
                if (!p->d[P_GLIDE_ON]) s->v[i].pitch = (float)key;
                if (retrigger_new) { s->v[i].aenv.stage = ST_ATT; s->v[i].fenv.stage = ST_ATT; }
            } else start_voice(s, i, key, vel, det, pan, 0);
        }
        for (int i = n; i < NV; i++) if (s->v[i].on) release_voice(&s->v[i]);
        s->last_pitch = (float)key;
        return;
    }
    if (mode == 2) {   /* unison: restart all voices divided over the held keys */
        int per = NV / s->nheld; if (per < 1) per = 1;
        int vi = 0;
        for (int k = 0; k < s->nheld && vi < NV; k++)
            for (int j = 0; j < per && vi < NV; j++, vi++) {
                float det, pan; spread(p, j, per, &det, &pan);
                start_voice(s, vi, s->held[k].note, s->held[k].vel, det, pan, 0);
            }
        for (; vi < NV; vi++) if (s->v[vi].on) release_voice(&s->v[vi]);
        s->last_pitch = (float)s->held[s->nheld - 1].note;
        return;
    }
    int n = mode == 1 ? 2 : 1;   /* normal or dual poly: the newest key only (older ones keep their voices) */
    for (int i = 0; i < n; i++) {
        float det, pan; spread(p, i, n, &det, &pan);
        start_voice(s, steal_voice(s), s->held[s->nheld - 1].note, s->held[s->nheld - 1].vel, det, pan, 0);
    }
    s->last_pitch = (float)s->held[s->nheld - 1].note;
}

static void note_on(inst_t *s, int n, int vel) {
    for (int i = 0; i < s->nheld; i++) if (s->held[i].note == n) { memmove(&s->held[i], &s->held[i + 1], (s->nheld - i - 1) * sizeof s->held[0]); s->nheld--; break; }
    if (s->nheld == 16) { memmove(&s->held[0], &s->held[1], 15 * sizeof s->held[0]); s->nheld = 15; }
    s->held[s->nheld].note = n; s->held[s->nheld].vel = vel; s->nheld++;
    s->deferred[n] = 0;
    assign_voices(s, 1);
}

static void note_off_now(inst_t *s, int n) {
    int found = 0;
    for (int i = 0; i < s->nheld; i++) if (s->held[i].note == n) { memmove(&s->held[i], &s->held[i + 1], (s->nheld - i - 1) * sizeof s->held[0]); s->nheld--; found = 1; break; }
    const patch_t *p = &s->cur;
    if (p->d[P_ALLOC] || p->d[P_ASSIGN] == 2) {           /* mono and unison: fall back to the remaining keys */
        if (s->nheld) { if (found) assign_voices(s, 0); return; }
    }
    for (int i = 0; i < NV; i++) if (s->v[i].on && (s->v[i].key == n || p->d[P_ALLOC] || p->d[P_ASSIGN] == 2)) release_voice(&s->v[i]);
}

static void midi(void *p, const uint8_t *m, int len) {
    inst_t *s = p;
    if (len < 3) return;
    int st = m[0] & 0xF0, n = m[1];
    if (st == 0xB0) {   /* controllers follow the XT's Controller Number Assignment */
        if (n == 120 || n == 123) { for (int i = 0; i < NV; i++) release_voice(&s->v[i]); s->nheld = 0; memset(s->deferred, 0, sizeof s->deferred); return; }
        if (n == 64) {
            int down = m[2] >= 64;
            if (s->pedal && !down) for (int k = 0; k < 128; k++) if (s->deferred[k]) { s->deferred[k] = 0; note_off_now(s, k); }
            s->pedal = down;
            return;
        }
        patch_apply_cc(&s->cur, n, m[2]);
        refresh(s);
        return;
    }
    if (st == 0x90 && m[2]) note_on(s, n, m[2]);
    else if (st == 0x80 || st == 0x90) { if (s->pedal) s->deferred[n] = 1; else note_off_now(s, n); }
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
static float osc_hz(const patch_t *p, float note, int oct_i, int semi_i, int det_i, int kt_i) {
    float kt = (-100.0f + 300.0f * p->d[kt_i] / 72.0f) / 100.0f;
    float st = (note - 64) * kt - 5.0f + (p->d[oct_i] - 64) + (p->d[semi_i] - 64) + (p->d[det_i] - 64) / 128.0f;
    return 440.0f * exp2f(st / 12.0f);
}

#ifndef MIP_LIMIT_HZ
#define MIP_LIMIT_HZ 30000.0f   /* tuned against firmware saw renders: within 0.5 dB at notes 60-84 */
#endif
static float mip_read(const int8_t *mip, float ph, int lvl) {
    static const int off[8] = { 0, 128, 192, 224, 240, 248, 252, 254 };
    int n = 128 >> lvl;
    float x = ph * n / 128.0f; int i = (int)x; float fr = x - i;
    const int8_t *w = mip + off[lvl];
    return ((1 - fr) * w[i % n] + fr * w[(i + 1) % n]) / 128.0f;
}

/* One oscillator sample. The mip level follows the pitch continuously (harmonics must stay under MIP_LIMIT_HZ) and the two
 * neighbouring levels are crossfaded: a hard switch was 2-3 dB off the firmware at high pitch (docs/CALIBRATION.md). */
static float osc_read(const int8_t *mip, float ph, float hz) {
    float lf = log2f(64.0f * hz / MIP_LIMIT_HZ);
    if (lf <= 0) return mip_read(mip, ph, 0);
    if (lf >= 7) return mip_read(mip, ph, 7);
    int l = (int)lf; float fr = lf - l;
    return (1 - fr) * mip_read(mip, ph, l) + fr * mip_read(mip, ph, l + 1);
}

/* Envelope timing, measured on the firmware (docs/CALIBRATION.md): attack is a linear ramp, decay and release are exponential
 * (toward the sustain level, toward zero), sustain is linear in the value. Times in seconds, interpolated in log domain
 * between measured points every 8 steps of the 0..127 rate value. */
static float interp_log(const float *t, int v) {   /* 17 points: v = 0, 8, ..., 120, 128 */
    int i = v >> 3; float f = (v & 7) / 8.0f;
    return t[i] * powf(t[i + 1] / t[i], f);
}
static float attack_seconds(int v) {   /* time for the full 0..1 ramp */
    static const float t[17] = { 0.001f, 0.012f, 0.03f, 0.069f, 0.156f, 0.30f, 0.487f, 0.731f, 1.038f, 1.431f, 1.906f, 2.475f, 3.162f, 4.3f, 6.5f, 10.475f, 17.0f };
    return interp_log(t, v);
}
static float decay_tau(int v) {        /* time constant of decay and release (identical in the firmware); 127 is effectively a hold */
    static const float t[17] = { 0.010f, 0.020f, 0.043f, 0.09f, 0.20f, 0.40f, 0.82f, 1.64f, 3.29f, 6.6f, 7.9f, 9.8f, 12.0f, 16.4f, 24.1f, 90.0f, 500.0f };
    return interp_log(t, v);
}
static float att_step(int v) { return 1.0f / (CORE_HZ * attack_seconds(v)); }
static float dec_coef(int v) { return 1.0f - expf(-1.0f / (CORE_HZ * decay_tau(v))); }

/* Pan law (measured): amplitude falls linearly from 1 at hard left to 0.75 at centre and to 0 at hard right. */
static float pan_gain_left(int pan) { return pan <= 64 ? 1.0f - pan / 256.0f : 0.75f * (127 - pan) / 63.0f; }

/* One envelope sample: linear attack, exponential decay toward the sustain level, exponential release (times from the tables above). */
static void env_step(env_t *e, int a, int d, int su, int r) {
    switch (e->stage) {
    case ST_ATT: e->level += att_step(a); if (e->level >= 1) { e->level = 1; e->stage = ST_DEC; } break;
    case ST_DEC: { float sus = su / 127.0f; e->level += (sus - e->level) * dec_coef(d); if (e->level - sus < 1e-4f && e->level >= sus) { e->level = sus; e->stage = ST_SUS; } break; }
    case ST_REL: e->level -= e->level * dec_coef(r); if (e->level < 1e-5f) e->level = 0; break;
    default: break;
    }
}

/* Glide: exponential (a fixed fraction of the remaining distance per sample) or linear (constant speed); the time law is a
 * placeholder, not yet measured. */
static float glide_seconds(int v) { return 0.002f * expf(v * 0.062f); }
static void glide_step(voice_t *v, const patch_t *p) {
    if (v->pitch == v->target) return;
    if (!p->d[P_GLIDE_ON]) { v->pitch = v->target; return; }
    float d = v->target - v->pitch, t = glide_seconds(p->d[P_GLIDE_TIME]);
    if (p->d[P_GLIDE_MODE] == 0) {
        v->pitch += d * (1.0f - expf(-3.0f / (CORE_HZ * t)));
        if (fabsf(d) < 1e-3f) v->pitch = v->target;
    } else {
        float step = 12.0f / (CORE_HZ * t);
        if (fabsf(d) <= step) v->pitch = v->target; else v->pitch += d > 0 ? step : -step;
    }
}

static float clip(float x, int overflow) {
    if (x > 1) return overflow ? (x > 3 ? 1 : 2 - x) : 1;
    if (x < -1) return overflow ? (x < -3 ? -1 : -2 - x) : -1;
    return x;
}

/* Output shelf, measured on the firmware for every signal path: one pole at 280 Hz and one zero at 437 Hz (-3.86 dB at high
 * frequencies). Bilinear transform at 40 kHz. */
static float shelf_run(shelf_t *f, float x) {
    static float b0, b1, a1; static int init;
    if (!init) {
        const float k = 2.0f * CORE_HZ, wz = 6.2831853f * 437.0f, wp = 6.2831853f * 280.0f;
        b0 = (1.0f + k / wz) / (1.0f + k / wp); b1 = (1.0f - k / wz) / (1.0f + k / wp); a1 = (1.0f - k / wp) / (1.0f + k / wp);
        init = 1;
    }
    float y = b0 * x + b1 * f->x1 - a1 * f->y1;
    f->x1 = x; f->y1 = y;
    return y;
}
#define OUT_GAIN 0.1885f   /* -14.5 dB: measured ratio firmware/ours for one oscillator, notes 36-84 within 0.15 dB */

/* one 40 kHz core sample, stereo */
static void core(inst_t *s, float *lr) {
    const patch_t *p = &s->cur;
    float suml = 0, sumr = 0;
    for (int i = 0; i < NV; i++) {
        voice_t *v = &s->v[i];
        if (v->aenv.stage == ST_REL && v->aenv.level <= 0) v->on = 0;
        if (!v->on && v->aenv.level == 0) continue;
        env_step(&v->aenv, p->d[P_AENV_A], p->d[P_AENV_D], p->d[P_AENV_S], p->d[P_AENV_R]);
        env_step(&v->fenv, p->d[P_FENV_A], p->d[P_FENV_D], p->d[P_FENV_S], p->d[P_FENV_R]);
        glide_step(v, p);
        float note = ((p->d[P_GLIDE_ON] && (p->d[P_GLIDE_TYPE] & 1)) ? roundf(v->pitch) : v->pitch) + v->det;   /* gliss types step by semitone */
        float hz1 = osc_hz(p, note, P_OSC1_OCT, P_OSC1_SEMI, P_OSC1_DET, P_OSC1_KT);
        float hz2 = osc_hz(p, note, P_OSC2_OCT, P_OSC2_SEMI, P_OSC2_DET, P_OSC2_KT);
        float w1 = osc_read(s->tab->mip[p->d[P_W1_START]], v->ph1, hz1);
        float w2 = osc_read(s->tab->mip[p->d[P_W2_START]], v->ph2, hz2);
        v->ph1 += 128.0f * hz1 / CORE_HZ;
        if (v->ph1 >= 128) { v->ph1 -= 128; if (p->d[P_OSC2_SYNC]) v->ph2 = start_phase(p->d[P_W2_PHASE]); }
        v->ph2 += 128.0f * hz2 / CORE_HZ; if (v->ph2 >= 128) v->ph2 -= 128;
        float mix = (w1 * p->d[P_MIX_W1] + w2 * p->d[P_MIX_W2] + w1 * w2 * p->d[P_MIX_RING]) / 128.0f;
        mix = clip(mix, p->d[P_CLIP]);
        float a = (p->d[P_AMP_VELO] - 64) / 64.0f;
        float vg = a >= 0 ? 1 - a * (1 - v->vel / 127.0f) : 1 + a * (v->vel / 127.0f);
        /* Filter 1 cutoff: base value + keytrack (semitones from note 64 at 3.125% per step) + envelope and velocity amounts.
         * Measured: keytrack is 1 cutoff unit per semitone at +100%; the envelope amount is 2 units per step at full envelope
         * (the velocity amount is assumed to use the same scale). */
        float kt = (p->d[P_F1_KT] - 64) * 0.03125f * (note - 64);
        float ea = 2.0f * ((p->d[P_F1_ENV] - 64) * v->fenv.level + (p->d[P_F1_VELO] - 64) * (v->vel / 127.0f));
        float cut = p->d[P_F1_CUTOFF] + kt + ea;
        float fl = filter1_run(&v->flt, p->d[P_F1_TYPE], mix, cut, (float)p->d[P_F1_RESO], p->d[P_F1_SPECIAL]);
        float c2 = p->d[P_F2_CUTOFF] + (p->d[P_F2_KT] - 64) * 0.03125f * (note - 64);
        fl = filter2_run(&v->flt, p->d[P_F2_TYPE], fl, c2);
        float g = fl * v->aenv.level * vg * (p->d[P_VOLUME] / 127.0f);
        float pan = p->d[P_PAN] + v->panoff * 63.5f;   /* unison/dual spread moves the voice off the sound's pan position */
        pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
        suml += g * pan_gain_left((int)(pan + 0.5f));
        sumr += g * pan_gain_left(127 - (int)(pan + 0.5f));
    }
    float l = suml * OUT_GAIN, r = sumr * OUT_GAIN;
    /* Effect index order follows the manual's list for 0..9 (the firmware's real numbering is to be confirmed); others are off. */
    if (p->d[P_FX_TYPE] < FX_TYPES) fx_run(&s->fx, p->d[P_FX_TYPE], p->d[P_FX_P1], p->d[P_FX_P2], p->d[P_FX_P3], 120.0f, &l, &r);
    chorus_run(&s->fx, p->d[P_CHORUS], &l, &r);
    lr[0] = shelf_run(&s->shelf[0], l);
    lr[1] = shelf_run(&s->shelf[1], r);
}

/* Test hook: render the 40 kHz core directly (not exported from the plugin). */
void clementine_render40k(void *inst, float *out, int n) { for (int i = 0; i < n; i++) { float lr[2]; core(inst, lr); out[i] = lr[0]; } }

static void gen(void *p, float *lr) { core(p, lr); }

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
