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
#include "presets.h"
#include "syx.h"
#include "wavedata.h"
#include "filter.h"
#include "fx.h"
#include "mod.h"

#define NV 10
#define CORE_HZ 40000.0f

typedef struct { float x1, y1; } shelf_t;
enum { ST_ATT, ST_DEC, ST_SUS, ST_REL };
typedef struct { int stage; float level; } env_t;
/* multi-segment envelope state (wave envelope: 8 segments, free envelope: 3 + release) */
typedef struct { int seg, phase; float level, timer; } xenv_t;   /* phase: 0 running (sustain part), 1 held at the sustain end, 2 release part, 3 finished */
/* key = the played note that owns the voice; pitch = its current pitch in notes (moves during glide), target = where it glides to,
 * det = unison/dual detune in notes, panoff = its pan offset from the spread (0..1, sign = side). */
typedef struct { int key, on, vel; float ug, ph1, ph2, pitch, target, det, panoff; env_t aenv, fenv; filt_t flt; lfo_t lfo[2]; float lfov[2]; uint32_t nrng; float nx1, ny1; xenv_t wenv, fren; float mst[4][2];
    /* control-rate state, refreshed every CTL_N samples (modulation moves slowly; the libm-heavy work happens here) */
    int ctl, fa, fd, fs, fr, aa, ad, as, ar, slot1, slot2, spec;
    float hz1, hz2, lf1, lf2, fmk, m1, m2, m3, m4, cut, reso, c2, gfac, panl, panr;
    int fm_on;
} voice_t;
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
    uint8_t cc[128];             /* last value of each MIDI controller (mod wheel 1, breath 2, foot 4, ...) */
    float bend, aftertouch;      /* -1..1 and 0..1 */
    float modgain[16];           /* the 16 matrix amounts as destination-unit multipliers (mod_amount_gain) */
    lfo_t glfo[2];               /* LFOs shared by all voices when Sync is on */
    float glfov[2];
    uint32_t seed;
    rs_t rs;
    struct { int note, vel; } arp_keys[20];   /* arpeggiator: keys in the order played */
    int arp_n, arp_down, arp_step, arp_idx, arp_dirn, arp_sound, arp_hold_clear;
    double arp_timer;
} inst_t;
enum { P_FX_TYPE = 76, P_FX_P1 = 81, P_CHORUS = 82, P_FX_P2 = 83, P_FX_P3 = 86 };
enum { P_OSC1_OCT = 1, P_OSC1_SEMI = 2, P_OSC1_DET = 3, P_OSC1_KT = 6, P_OSC2_OCT = 12, P_OSC2_SEMI = 13, P_OSC2_DET = 14,
       P_OSC2_SYNC = 16, P_OSC2_KT = 18, P_TABLE = 25, P_W1_START = 26, P_W1_PHASE = 27, P_W2_START = 36, P_W2_PHASE = 37,
       P_MIX_W1 = 47, P_MIX_W2 = 48, P_MIX_RING = 49, P_MIX_NOISE = 50, P_CLIP = 55, P_VOLUME = 77, P_AMP_VELO = 79, P_PAN = 84,
       P_AENV_A = 119, P_AENV_D = 120, P_AENV_S = 121, P_AENV_R = 122,
       P_F1_CUTOFF = 62, P_F1_RESO = 63, P_F1_TYPE = 64, P_F1_KT = 65, P_F1_ENV = 66, P_F1_VELO = 67, P_F1_SPECIAL = 70,
       P_F2_CUTOFF = 73, P_F2_TYPE = 74, P_F2_KT = 75, P_FENV_A = 113, P_FENV_D = 114, P_FENV_S = 115, P_FENV_R = 116 };

static void refresh(inst_t *s) {
    if (s->wd) s->tab = wavedata_table(s->wd, s->cur.d[P_TABLE]);
    for (int n = 0; n < 16; n++) s->modgain[n] = mod_amount_gain(s->cur.d[193 + 3 * n]);
}

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

static int tabs_ready;
static void build_env_tabs(void);
static void *create(const char *dir) {
    inst_t *s = calloc(1, sizeof *s);
    if (s) s->arp_sound = -1;
    patch_init(&s->cur);
    load_bank(s, dir);
    if (!s->have_bank) { presets_fill(s->bank); s->have_bank = 1; s->cur = s->bank[0]; }   /* built-in sounds when no bank is found */
    if (s->have_bank) s->cur = s->bank[0];
    rs_init(&s->rs, RS_CLEAN);
    s->wd = wavedata_load(dir);
    if (!s->wd) { s->wd = calloc(1, sizeof *s->wd); }   /* no imported data: every table is an open-set stand-in */
    wavedata_prewarm(s->wd); filt_init(); if (!tabs_ready) build_env_tabs();
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

static void release_voice(voice_t *v) { v->on = 0; v->aenv.stage = ST_REL; v->fenv.stage = ST_REL; v->wenv.phase = v->wenv.phase == 3 ? 3 : 4; v->fren.phase = v->fren.phase == 3 ? 3 : 4; }   /* 4 = release requested; the core moves to the release segments */

static int steal_voice(inst_t *s) {
    int best = 0;
    for (int i = 0; i < NV; i++) if (!s->v[i].on && s->v[i].aenv.level == 0) return i;
    for (int i = 0; i < NV; i++) if (!s->v[i].on && s->v[i].aenv.level < s->v[best].aenv.level) best = i;   /* quietest release */
    if (s->v[best].on) { best = 0; for (int i = 1; i < NV; i++) if (s->v[i].aenv.level < s->v[best].aenv.level) best = i; }
    return best;
}

static void start_voice(inst_t *s, int idx, int note, int vel, float det, float panoff, int legato, int nvoices) {
    voice_t *v = &s->v[idx];
    const patch_t *p = &s->cur;
    int gl = p->d[P_GLIDE_ON] && (p->d[P_GLIDE_TYPE] < 2 || legato);   /* types 2 and 3 (fingered) glide only on legato notes */
    float from = legato || (gl && s->last_pitch > 0) ? (legato ? v->pitch : s->last_pitch) : (float)note;
    memset(v, 0, sizeof *v);
    v->key = note; v->on = 1; v->vel = vel; v->det = det; v->panoff = panoff;
    v->ug = nvoices <= 1 ? 1.0f : 0.73f / sqrtf((float)nvoices);   /* voices sharing a note split the level: ~0.73/sqrt(n) (dual 0.52, ten 0.23; measured) */
    v->target = (float)note;
    v->pitch = gl ? from : (float)note;
    v->aenv.stage = ST_ATT; v->fenv.stage = ST_ATT;
    v->ph1 = start_phase(p->d[P_W1_PHASE]); v->ph2 = start_phase(p->d[P_W2_PHASE]);
    v->nrng = ++s->seed * 2246822519u + 3266489917u;
    for (int l = 0; l < 2; l++) {   /* delay: 0 runs free; 1..127 restarts the LFO at the note after 0.1 s per step */
        int dl = p->d[l ? 168 : 161];
        lfo_reset(&v->lfo[l], ++s->seed + idx * 7919u + l, dl == 0, dl > 0 ? dl * 0.1f - 0.004f : 0.0f);   /* measured: 0.1 s per step, retrigger (1) is 0.1 s */
    }
}

/* Spread of unison/dual voices: n voices evenly across the detune range; returns detune (notes) and pan side (-1..1).
 * Measured total spread: dual 0.755 cents per detune step (+-48 cents at 127), unison 2.36 cents per step (+-150 cents at 127, ten
 * voices about 33 cents apart). The pan layout is a fit (unison leans left of centre) and is only roughly right. */
static void spread(const patch_t *p, int i, int n, float *det, float *pan) {
    float pos = n > 1 ? (2.0f * i / (n - 1) - 1.0f) : 0.0f;
    float per_unit = p->d[P_ASSIGN] == 2 ? 0.0118f : 0.00377f;   /* half-range in notes per detune step */
    *det = pos * p->d[P_DETUNE] * per_unit;
    float f = p->d[P_DEPAN] / 127.0f;
    *pan = p->d[P_ASSIGN] == 2 ? (-14.0f * f + 1.3f * 63.5f * f * pos) / 63.5f : pos * f;
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
                if (retrigger_new) {   /* per envelope: normal restarts from zero, single leaves it running, retrigger restarts from the current level */
                    voice_t *w = &s->v[i];
                    int ta = p->d[123], tf = p->d[117], tw = p->d[141], tr = p->d[157];
                    if (ta != 1) { if (ta == 0) w->aenv.level = 0; w->aenv.stage = ST_ATT; }
                    if (tf != 1) { if (tf == 0) w->fenv.level = 0; w->fenv.stage = ST_ATT; }
                    if (tw == 0) memset(&w->wenv, 0, sizeof w->wenv); else if (tw == 2) { w->wenv.phase = 0; w->wenv.seg = 0; w->wenv.timer = 0; }
                    if (tr == 0) memset(&w->fren, 0, sizeof w->fren); else if (tr == 2) { w->fren.phase = 0; w->fren.seg = 0; w->fren.timer = 0; }
                }
            } else start_voice(s, i, key, vel, det, pan, 0, n);
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
                start_voice(s, vi, s->held[k].note, s->held[k].vel, det, pan, 0, per);
            }
        for (; vi < NV; vi++) if (s->v[vi].on) release_voice(&s->v[vi]);
        s->last_pitch = (float)s->held[s->nheld - 1].note;
        return;
    }
    int n = mode == 1 ? 2 : 1;   /* normal or dual poly: the newest key only (older ones keep their voices) */
    for (int i = 0; i < n; i++) {
        float det, pan; spread(p, i, n, &det, &pan);
        start_voice(s, steal_voice(s), s->held[s->nheld - 1].note, s->held[s->nheld - 1].vel, det, pan, 0, n);
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


/* Arpeggiator. Tempo, clock values and the 15 preset rhythms are not yet measured: the mappings below are guesses (docs/CALIBRATION.md). */
static const float ARP_BEATS[16] = { 0.0417f, 0.0625f, 0.0833f, 0.125f, 0.1667f, 0.25f, 0.3333f, 0.375f, 0.5f, 0.6667f, 0.75f, 1.0f, 1.333f, 1.5f, 2.0f, 4.0f };
static const uint16_t ARP_PRESET[16] = { 0xFFFF, 0xFFFF, 0x5555, 0x9249, 0xB6DB, 0x8888, 0xAAAA, 0xEEEE, 0xF0F0, 0x1111, 0xDDDD, 0xABAB, 0x7777, 0x9999, 0xF5F5, 0x6666 };

static int arp_step_on(const patch_t *p, int step) {
    if (p->d[96] == 16) { int len = p->d[101] + 1; int k = step % len; return (p->d[102 + k / 4] >> (k % 4)) & 1; }
    return (ARP_PRESET[p->d[96] & 15] >> (step & 15)) & 1;
}

static void arp_stop_sound(inst_t *s) { if (s->arp_sound >= 0) { note_off_now(s, s->arp_sound); s->arp_sound = -1; } }

static void arp_tick(inst_t *s) {
    const patch_t *p = &s->cur;
    if (!p->d[92]) { if (s->arp_sound >= 0) arp_stop_sound(s); if (s->arp_n) s->arp_n = 0; return; }
    float bpm = p->d[93] == 0 ? 120.0f : 50.0f + (p->d[93] - 1) * 250.0f / 126.0f;
    double step_s = ARP_BEATS[p->d[94] & 15] * 60.0 / bpm * 40000.0;
    if (s->arp_n == 0) { arp_stop_sound(s); s->arp_timer = 0; s->arp_step = 0; s->arp_idx = -1; return; }
    if (s->arp_sound >= 0 && s->arp_timer >= step_s * 0.8) arp_stop_sound(s);
    if (s->arp_timer < step_s && s->arp_timer > 0) { s->arp_timer += 1; return; }
    s->arp_timer = 1;
    arp_stop_sound(s);
    int n = s->arp_n, ord[20];
    for (int i = 0; i < n; i++) { ord[i] = s->arp_keys[i].note; }
    int mode = p->d[98];
    if (mode == 0 || mode == 1) for (int i = 1; i < n; i++) for (int j = i; j > 0 && ord[j] < ord[j - 1]; j--) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
    if (mode == 1 || mode == 3) for (int i = 0; i < n / 2; i++) { int t = ord[i]; ord[i] = ord[n - 1 - i]; ord[n - 1 - i] = t; }
    int range = p->d[95] < 1 ? 1 : p->d[95], total = n * range;
    int idx = s->arp_idx;
    switch (p->d[97]) {
    case 0: idx = idx < 0 ? 0 : (idx + 1) % total; break;
    case 1: idx = idx < 0 ? total - 1 : (idx + total - 1) % total; break;
    case 2: if (idx < 0) { idx = 0; s->arp_dirn = 1; } else if (total > 1) { idx += s->arp_dirn ? 1 : -1; if (idx >= total) { idx = total - 2; s->arp_dirn = 0; } else if (idx < 0) { idx = 1; s->arp_dirn = 1; } } break;
    default: s->seed = s->seed * 1664525u + 1013904223u; idx = (int)((s->seed >> 8) % (unsigned)total); break;
    }
    if (idx >= total) idx = total - 1;
    s->arp_idx = idx;
    int on = arp_step_on(p, s->arp_step++);
    if (!on) return;
    int note = ord[idx % n] + 12 * (idx / n);
    if (note > 127) note %= 12 + 108;
    int vel = p->d[99] ? s->arp_keys[n - 1].vel : s->arp_keys[0].vel;   /* root note or last note */
    note_on(s, note, vel); s->arp_sound = note;
}

static void arp_key(inst_t *s, int note, int vel, int down) {
    if (down) {
        if (s->arp_down == 0 && (s->cur.d[92] != 2 || s->arp_hold_clear)) { s->arp_n = 0; s->arp_hold_clear = 0; if (s->cur.d[100]) { s->arp_step = 0; s->arp_timer = 0; } }
        s->arp_down++;
        for (int i = 0; i < s->arp_n; i++) if (s->arp_keys[i].note == note) return;
        if (s->arp_n == 20) { memmove(&s->arp_keys[0], &s->arp_keys[1], 19 * sizeof s->arp_keys[0]); s->arp_n = 19; }
        s->arp_keys[s->arp_n].note = note; s->arp_keys[s->arp_n].vel = vel; s->arp_n++;
    } else {
        if (s->arp_down > 0) s->arp_down--;
        if (s->cur.d[92] == 2) { if (s->arp_down == 0) s->arp_hold_clear = 1; return; }
        for (int i = 0; i < s->arp_n; i++) if (s->arp_keys[i].note == note) { memmove(&s->arp_keys[i], &s->arp_keys[i + 1], (s->arp_n - i - 1) * sizeof s->arp_keys[0]); s->arp_n--; break; }
    }
}

static void midi(void *p, const uint8_t *m, int len) {
    inst_t *s = p;
    if (len < 2) return;
    int st = m[0] & 0xF0, n = m[1];
    if (len < 3 && st != 0xD0) return;
    if (st == 0xB0) {   /* controllers follow the XT's Controller Number Assignment */
        if (n == 120 || n == 123) { for (int i = 0; i < NV; i++) release_voice(&s->v[i]); s->nheld = 0; memset(s->deferred, 0, sizeof s->deferred); return; }
        if (n == 64) {
            int down = m[2] >= 64;
            if (s->pedal && !down) for (int k = 0; k < 128; k++) if (s->deferred[k]) { s->deferred[k] = 0; note_off_now(s, k); }
            s->pedal = down;
            return;
        }
        s->cc[n & 127] = m[2];
        patch_apply_cc(&s->cur, n, m[2]);
        refresh(s);
        return;
    }
    if (st == 0xE0) { s->bend = ((m[2] << 7 | m[1]) - 8192) / 8192.0f; return; }
    if (st == 0xD0) { s->aftertouch = m[1] / 127.0f; return; }
    if (s->cur.d[92] && (st == 0x90 || st == 0x80)) { arp_key(s, n, m[2], st == 0x90 && m[2]); return; }
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
    refresh(s);   /* table choice and the matrix amounts (the gains are cached) */
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
static float osc_hz(const patch_t *p, float note, int oct_i, int semi_i, int det_i, int kt_i, float extra) {
    float kt = (-100.0f + 300.0f * p->d[kt_i] / 72.0f) / 100.0f;
    float st = (note - 64) * kt - 5.0f + (p->d[oct_i] - 64) + (p->d[semi_i] - 64) + (p->d[det_i] - 64) / 128.0f + extra;
    return 440.0f * exp2f(st / 12.0f);
}

#ifndef MIP_LIMIT_HZ
#define MIP_LIMIT_HZ 30000.0f   /* tuned against firmware saw renders: within 0.5 dB at notes 60-84 */
#endif
static inline float mip_read(const int8_t *mip, float ph, int lvl) {
    static const int off[8] = { 0, 128, 192, 224, 240, 248, 252, 254 };
    static const float scale[8] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 0.015625f, 0.0078125f };   /* n / 128 for n = 128 >> lvl */
    int mask = (128 >> lvl) - 1;
    float x = ph * scale[lvl]; int i = (int)x; float fr = x - i;
    const int8_t *w = mip + off[lvl];
    float a = w[i & mask], b = w[(i + 1) & mask];
    return (a + fr * (b - a)) * (1.0f / 128.0f);
}

/* One oscillator sample. The mip level follows the pitch continuously (harmonics must stay under MIP_LIMIT_HZ) and the two
 * neighbouring levels are crossfaded: a hard switch was 2-3 dB off the firmware at high pitch (docs/CALIBRATION.md). */
/* log2 from the float's exponent and a polynomial for the mantissa (error < 2e-5): log2f is a libm call, slow per sample on the ARM devices. */
static inline float fast_log2(float x) {
    union { float f; uint32_t u; } v = { x };
    float e = (float)((int)(v.u >> 23) - 127);
    v.u = (v.u & 0x007FFFFF) | 0x3F800000;
    float m = v.f;
    return e + (-2.79415209f + (5.06975074f + (-3.52021112f + (1.61017228f + (-0.409473816f + 0.0439283931f * m) * m) * m) * m) * m);   /* degree-5 fit, error 1.5e-5 */
}
static float mip_lf(float hz) { return hz > 1e-6f ? fast_log2(64.0f * hz / MIP_LIMIT_HZ) : -20.0f; }
static float osc_read_lf(const int8_t *mip, float ph, float lf) {
    if (lf <= 0) return mip_read(mip, ph, 0);
    if (lf >= 7) return mip_read(mip, ph, 7);
    int l = (int)lf; float fr = lf - l;
    return (1 - fr) * mip_read(mip, ph, l) + fr * mip_read(mip, ph, l + 1);
}
static float osc_read(const int8_t *mip, float ph, float hz) { return osc_read_lf(mip, ph, mip_lf(hz)); }

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
static float att_tab[128], dec_tab[128], tau_tab[128], xseg_coef[128], xseg_coef_n[128], xseg_tau_tab[128], glide_tab[128];
static int tabs_ready;
static float xseg_tau(int t);
static void build_env_tabs(void) {
    for (int v = 0; v < 128; v++) {
        att_tab[v] = 1.0f / (CORE_HZ * attack_seconds(v));
        tau_tab[v] = decay_tau(v);
        dec_tab[v] = 1.0f - expf(-1.0f / (CORE_HZ * tau_tab[v]));
        glide_tab[v] = 1.0f - expf(-1.0f / (CORE_HZ * 2.0f * tau_tab[v]));
        xseg_tau_tab[v] = xseg_tau(v);
        xseg_coef[v] = 1.0f - expf(-1.0f / (CORE_HZ * xseg_tau_tab[v]));
        xseg_coef_n[v] = 1.0f - expf(-8.0f / (CORE_HZ * xseg_tau_tab[v]));   /* one control interval (CTL_N = 8) */
    }
    tabs_ready = 1;
}
static float att_step(int v) { if (!tabs_ready) build_env_tabs(); return att_tab[v & 127]; }
static float dec_coef(int v) { if (!tabs_ready) build_env_tabs(); return dec_tab[v & 127]; }

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

/* Glide (measured with a mono two-note run): exponential glide has time constant 2x the envelope decay constant for the same value (0.13 s at
 * 20, 0.8 s at 40), approaching the target exponentially in pitch; linear glide covers an octave in about 1.6x the decay constant. */
static void glide_step(voice_t *v, const patch_t *p) {
    if (v->pitch == v->target) return;
    if (!p->d[P_GLIDE_ON]) { v->pitch = v->target; return; }
    if (!tabs_ready) build_env_tabs();
    float d = v->target - v->pitch, t = tau_tab[p->d[P_GLIDE_TIME] & 127];
    if (p->d[P_GLIDE_MODE] == 0) {
        v->pitch += d * glide_tab[p->d[P_GLIDE_TIME] & 127];
        if (fabsf(d) < 1e-3f) v->pitch = v->target;
    } else {
        float step = 12.0f / (CORE_HZ * 1.6f * t);
        if (fabsf(d) <= step) v->pitch = v->target; else v->pitch += d > 0 ? step : -step;
    }
}

static int clampi(int x, int hi) { return x < 0 ? 0 : x > hi ? hi : x; }
static float clampf(float x, float hi) { return x < 0 ? 0 : x > hi ? hi : x; }

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

/* Segment time constant (measured on the wave envelope: 90% of a step takes 0.084 s at 24, 0.35 s at 40, 1.34 s at 56, doubling every 8
 * steps, so the segments are exponential approaches with tau = 0.0365 s * 2^((t-24)/8)). The rule that hands over to the next segment
 * (here: after three time constants) and the level/loop handling are approximations, not measured. */
static float xseg_tau(int t) { return 0.0365f * exp2f((t - 24) / 8.0f); }

/* One step of a segment envelope. times/levels are the raw SDATA values; `nseg` segments in the run, `sus_end`/`rel_end` are segment
 * indices; `loop_on`/`loop_start` implement key-on loop; bipolar levels map -64..63 to -1..1, otherwise 0..127 to 0..1. */
static void xenv_step(xenv_t *e, const uint8_t *times, const uint8_t *levels, int stride, int sus_end, int loop_on, int loop_start,
                      int rel_end, int rel_loop_on, int rel_loop_start, int bipolar) {
    if (e->phase == 3 || e->phase == 1) return;
    float target = bipolar ? (levels[e->seg * stride] - 64) / 64.0f : levels[e->seg * stride] / 127.0f;
    if (!tabs_ready) build_env_tabs();
    int tv = times[e->seg * stride] & 127; float tau = xseg_tau_tab[tv];
    e->level += (target - e->level) * xseg_coef_n[tv];   /* stepped once per control interval (8 samples) */
    e->timer += 8.0f / CORE_HZ;
    if (e->timer < 3.0f * tau) return;
    e->timer = 0;
    if (e->phase == 0) {
        if (e->seg >= sus_end) { if (loop_on) e->seg = loop_start; else e->phase = 1; }
        else e->seg++;
    } else {
        if (e->seg >= rel_end) { if (rel_loop_on) e->seg = rel_loop_start; else e->phase = 3; }
        else e->seg++;
    }
}
static void xenv_release(xenv_t *e, int sus_end, int max_seg) { if (e->phase == 0 || e->phase == 1 || e->phase == 4) { e->phase = 2; e->seg = sus_end + 1 > max_seg ? max_seg : sus_end + 1; e->timer = 0; } }

/* Noise generator (measured): white noise through a pole-zero pair, flat below ~1 kHz and falling to about -13 dB by 12 kHz
 * (pole 2.5 kHz, zero 12 kHz after removing the output shelf). NOISE_LEVEL is the white noise rms before shaping. */
#define NOISE_LEVEL 0.756f   /* matched to the firmware: rms 0.0254 at mixer level 127 */
static float noise_tick(voice_t *v) {
    static float b0, b1, a1; static int init;
    if (!init) {
        const float k = 2.0f * CORE_HZ, wz = 6.2831853f * 12000.0f, wp = 6.2831853f * 2500.0f;
        b0 = (1.0f + k / wz) / (1.0f + k / wp); b1 = (1.0f - k / wz) / (1.0f + k / wp); a1 = (1.0f - k / wp) / (1.0f + k / wp);
        init = 1;
    }
    v->nrng = v->nrng * 1664525u + 1013904223u;
    float w = ((int32_t)v->nrng) / 2147483648.0f * 1.7320508f * NOISE_LEVEL;   /* uniform -> unit rms times the level */
    float y = b0 * w + b1 * v->nx1 - a1 * v->ny1;
    v->nx1 = w; v->ny1 = y;
    return y;
}

/* one 40 kHz core sample, stereo */
/* Modifier operations. The stateless ones are calibrated against the firmware (docs/CALIBRATION.md, "Modifiers"); the
 * stateful ones (S&H, ramp, lag, filter, differentiator) follow the manual and are not yet measured. a, b, par are 0..1. */
static float mod_op(int op, float a, float b, float par, float st[2], float dt) {   /* dt: seconds since the last call */
    int ia = (int)lroundf(a * 127), ib = (int)lroundf(b * 127);
    float w;
    switch (op) {
    case 0: w = a + b; return w >= 1.0f ? w - 2.0f : 1.0f;      /* wraps at +1 like the firmware; below 1 it reads full scale */
    case 1: w = a - b; return w < 0 ? w : 1.0f;
    case 2: return a * b;
    case 3: return 0.0f;                                        /* measured about 0.01-0.03 for every input tried */
    case 4: return (ia ^ ib) / 128.0f;
    case 5: return (ia | ib) / 128.0f;
    case 6: return (ia & ib) / 128.0f;
    case 7: if (st[1] <= 0) { st[0] = a; st[1] = 0.001f + par * par * 2.0f; } st[1] -= dt; return st[0];
    case 8: if (a > 0.5f && st[1] < 0.5f) st[0] = 0;
            st[1] = a; st[0] += dt / (0.01f + par * par * 10.0f); if (st[0] > 1) st[0] = 1; return st[0];
    case 9: return a >= par ? 1.0f : 0.0f;
    case 10: return fabsf(a);
    case 11: w = 2.0f * par; return w >= 1.0f ? w - 2.0f : w;  /* depends on the parameter only (measured) */
    case 12: return a;
    case 13: { float step = 2.09f * exp2f((par * 127.0f - 64.0f) / 11.0f) * dt, d = a - st[0];   /* measured: linear ramp at 2.09 units/s for parameter 64, doubling every 11 steps */
               st[0] += d > step ? step : d < -step ? -step : d; return st[0]; }
    case 14: { float tau = 0.040f * exp2f((par * 127.0f - 100.0f) / 25.0f);   /* measured one-pole low-pass: about 40 ms at 100, 15 ms at 64 */
               st[0] += (a - st[0]) * (1.0f - expf(-dt / tau)); return st[0]; }
    default: { float d = a - st[0]; st[0] = a; return dt > 0 ? d / dt * 0.0125f : 0.0f; }
    }
}

/* The firmware's effect numbering (measured with the oracle: every other index leaves the sound untouched): 0 off, 1 Chorus,
 * 2 Flanger 1, 3 Flanger 2, 4 AutoWahLP, 5 AutoWahBP, 6 Overdrive, 7 Amp Mod, 32 Delay, 33 Pan Delay, 34 Mod Delay. */
static int fx_type_from_index(int i) {
    if (i >= 1 && i <= 7) return i - 1 + FX_CHORUS;
    if (i >= 32 && i <= 34) return FX_DELAY + (i - 32);
    return -1;
}

#define CTL_N 8
static inline int iround(float x) { return (int)(x >= 0 ? x + 0.5f : x - 0.5f); }

/* Everything that moves slowly for one voice: modulation sources and matrix, envelope times, LFOs, pitch, wave positions, mix levels, filter
 * controls, gain and pan. Runs every CTL_N samples (5 kHz); the per-sample path only reads the results. */
static void voice_control(inst_t *s, voice_t *v) {
    const patch_t *p = &s->cur;
    /* the wave and free envelopes step once per control interval */
    if (v->wenv.phase == 4) xenv_release(&v->wenv, p->d[144], 7);
    xenv_step(&v->wenv, p->d + 125, p->d + 126, 2, p->d[144], p->d[142], p->d[143], p->d[147], p->d[145], p->d[146], 0);
    if (v->fren.phase == 4) xenv_release(&v->fren, 2, 3);
    xenv_step(&v->fren, p->d + 149, p->d + 150, 2, 2, 0, 0, 3, 0, 0, 1);
    /* modulation sources for this voice (docs/CALIBRATION.md: keytrack/keyfollow are (note-64)/128, amounts use mod_amount_gain) */
    float note = ((p->d[P_GLIDE_ON] && (p->d[P_GLIDE_TYPE] & 1)) ? roundf(v->pitch) : v->pitch) + v->det;   /* gliss types step by semitone */
    float mw = s->cc[1] / 127.0f, src[32] = { 0 }, dest[36] = { 0 };
    for (int l = 0; l < 2; l++) {
        int sync = p->d[l ? 169 : 162] != 0;
        v->lfov[l] = sync ? s->glfov[l] : v->lfov[l];
    }
    src[1] = v->lfov[0]; src[2] = v->lfov[0] * mw; src[3] = v->lfov[0] * s->aftertouch; src[4] = v->lfov[1];
    src[5] = v->fenv.level; src[6] = v->aenv.level; src[7] = v->wenv.level; src[8] = v->fren.level;
    src[9] = (note - 64) / 128.0f; src[10] = (v->key - 64) / 128.0f;
    src[11] = v->vel / 127.0f; src[13] = s->aftertouch; src[15] = s->bend; src[16] = mw;
    src[17] = s->pedal ? 1.0f : 0.0f; src[18] = s->cc[4] / 127.0f; src[19] = s->cc[2] / 127.0f;
    src[20] = s->cc[4] / 127.0f; src[21] = s->cc[8] / 127.0f; src[22] = s->cc[11] / 127.0f; src[23] = s->cc[12] / 127.0f;   /* Controls W-Z (default CC numbers) */
    src[31] = 1.0f;
    for (int m = 0; m < 4; m++) {   /* modifiers 1-4 are sources 25-28 (sources 0 and 31 read 0 and 1) */
        int o = 176 + 4 * m;
        if (!p->d[o] && !p->d[o + 1] && !p->d[o + 2]) continue;   /* unused */
        src[25 + m] = mod_op(p->d[o + 2], src[p->d[o]], src[p->d[o + 1]], p->d[o + 3] / 127.0f, v->mst[m], (float)CTL_N / CORE_HZ);
    }
    for (int n = 0; n < 16; n++) {
        int si = p->d[192 + 3 * n];
        if (si && s->modgain[n] != 0.0f) dest[p->d[194 + 3 * n]] += s->modgain[n] * src[si];
    }
    /* envelope times can be modulated */
    v->fa = clampi(p->d[P_FENV_A] + iround(dest[14]), 127); v->fd = clampi(p->d[P_FENV_D] + iround(dest[15]), 127);
    v->fs = clampi(p->d[P_FENV_S] + iround(dest[16]), 127); v->fr = clampi(p->d[P_FENV_R] + iround(dest[17]), 127);
    v->aa = clampi(p->d[P_AENV_A] + iround(dest[18]), 127); v->ad = clampi(p->d[P_AENV_D] + iround(dest[19]), 127);
    v->as = clampi(p->d[P_AENV_S] + iround(dest[20]), 127); v->ar = clampi(p->d[P_AENV_R] + iround(dest[21]), 127);
    for (int l = 0; l < 2; l++) {   /* per-voice LFOs (used when Sync is off); advanced by the whole control interval */
        if (p->d[l ? 169 : 162]) continue;
        int o = l ? 166 : 159;
        if (l && p->d[172]) {   /* LFO 2 locked to LFO 1 at a phase offset */
            v->lfo[1].phase = fmodf(v->lfo[0].phase + (3.0f + (p->d[172] - 1) * 354.0f / 126.0f) / 360.0f, 1.0f);
            v->lfov[1] = lfo_eval(&v->lfo[1], p->d[o + 1], p->d[o + 4]);
        } else v->lfov[l] = lfo_tick_n(&v->lfo[l], p->d[o + 1], p->d[o] + dest[l ? 28 : 26], p->d[o + 4], p->d[o + 5], CTL_N);
    }
    /* pitch: bend range 0..120 semitones, 121 harmonic (treated as 2 here), 122 global (2 until the global range exists) */
    float bend1 = p->d[5] <= 120 ? p->d[5] : 2.0f, bend2 = p->d[17] <= 120 ? p->d[17] : 2.0f;
    float st1 = dest[0] + dest[1] + s->bend * bend1, st2 = dest[0] + dest[2] + s->bend * bend2;
    v->hz1 = osc_hz(p, note, P_OSC1_OCT, P_OSC1_SEMI, P_OSC1_DET, P_OSC1_KT, st1);
    v->hz2 = osc_hz(p, note, P_OSC2_OCT, P_OSC2_SEMI, P_OSC2_DET, P_OSC2_KT, p->d[19] ? st1 : st2);   /* Link: osc 2 uses osc 1's modulation */
    v->lf1 = mip_lf(v->hz1); v->lf2 = mip_lf(v->hz2);
    /* wave position: start wave + keytrack (1 slot per semitone at +100%) + wave envelope amounts + matrix (1 slot per m for the position destination).
     * Wave 2 with Link uses wave 1's keytrack, envelope amounts and matrix; the envelope amounts move the position by about 1.1 slots per step (measured). */
    int lk = p->d[42];
    float wk1 = (p->d[30] - 64) * 0.03125f * (note - 64), wk2 = (p->d[lk ? 30 : 40] - 64) * 0.03125f * (note - 64);
    float we1 = 1.1f * ((p->d[28] - 64) * v->wenv.level + (p->d[29] - 64) * (v->vel / 127.0f));
    float we2 = 1.1f * ((p->d[lk ? 28 : 38] - 64) * v->wenv.level + (p->d[lk ? 29 : 39] - 64) * (v->vel / 127.0f));
    v->slot1 = clampi(iround(p->d[P_W1_START] + wk1 + we1 + 0.5f * dest[3]), p->d[31] ? 60 : 63);
    v->slot2 = clampi(iround(p->d[P_W2_START] + wk2 + we2 + 0.5f * (lk ? dest[3] : dest[4])), p->d[41] ? 60 : 63);
    float fma = clampf(p->d[7] + dest[34], 127.0f);
    v->fm_on = fma > 0; v->fmk = fma > 0 ? 0.085f * powf(fma / 16.0f, 2.8f) : 0.0f;
    v->m1 = clampf(p->d[P_MIX_W1] + dest[5], 127.0f); v->m2 = clampf(p->d[P_MIX_W2] + dest[6], 127.0f); v->m3 = clampf(p->d[P_MIX_RING] + dest[7], 127.0f);
    v->m4 = clampf(p->d[P_MIX_NOISE] + dest[8], 127.0f);
    float a = (p->d[P_AMP_VELO] - 64) / 64.0f;
    float vg = a >= 0 ? 1 - a * (1 - v->vel / 127.0f) : 1 + a * (v->vel / 127.0f);
    /* Filter 1 cutoff: base value + keytrack (semitones from note 64 at 3.125% per step) + envelope and velocity amounts + matrix.
     * Measured: keytrack is 1 cutoff unit per semitone at +100%; the envelope amount is 2 units per step at full envelope
     * (the velocity amount is assumed to use the same scale). */
    float kt = (p->d[P_F1_KT] - 64) * 0.03125f * (note - 64);
    float ea = 2.0f * ((p->d[P_F1_ENV] - 64) * v->fenv.level + (p->d[P_F1_VELO] - 64) * (v->vel / 127.0f));
    v->cut = p->d[P_F1_CUTOFF] + kt + ea + dest[9];
    v->reso = clampf(p->d[P_F1_RESO] + dest[10], 127.0f);
    v->spec = clampi(p->d[P_F1_SPECIAL] + iround(dest[35]), 127);
    v->c2 = p->d[P_F2_CUTOFF] + (p->d[P_F2_KT] - 64) * 0.03125f * (note - 64) + dest[11];
    float vol = clampf(p->d[P_VOLUME] + dest[12], 127.0f);
    v->gfac = vg * (vol / 127.0f) * v->ug;
    float pan = p->d[P_PAN] + v->panoff * 63.5f + dest[13];   /* unison/dual spread moves the voice off the sound's pan position */
    pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
    int pi = (int)(pan + 0.5f);
    v->panl = pan_gain_left(pi); v->panr = pan_gain_left(127 - pi);
}

static void core(inst_t *s, float *lr) {
    arp_tick(s);
    const patch_t *p = &s->cur;
    float suml = 0, sumr = 0;
    for (int l = 0; l < 2; l++) {   /* LFOs shared by all voices (Sync on) */
        int o = l ? 166 : 159;
        s->glfov[l] = lfo_tick(&s->glfo[l], p->d[o + 1], p->d[o], p->d[o + 4], p->d[o + 5]);
    }
    if (p->d[172]) s->glfov[1] = lfo_eval(&(lfo_t){ .phase = fmodf(s->glfo[0].phase + (3.0f + (p->d[172] - 1) * 354.0f / 126.0f) / 360.0f, 1.0f) }, p->d[167], p->d[170]);
    for (int i = 0; i < NV; i++) {
        voice_t *v = &s->v[i];
        if (v->aenv.stage == ST_REL && v->aenv.level <= 0) v->on = 0;
        if (!v->on && v->aenv.level == 0) continue;
        if (v->ctl <= 0) { voice_control(s, v); v->ctl = CTL_N; }
        v->ctl--;
        env_step(&v->aenv, v->aa, v->ad, v->as, v->ar);
        env_step(&v->fenv, v->fa, v->fd, v->fs, v->fr);
        glide_step(v, p);
        float w2 = osc_read_lf(s->tab->mip[v->slot2], v->ph2, v->lf2);
        /* Oscillator FM (measured): oscillator 2 scales oscillator 1's frequency by (1 + k*w2) with k = 0.085*(amount/16)^2.8 (sidebands within 3%; the carrier level depends on start phases and is not matched);
         * the sidebands fall as 1/(modulator/carrier ratio), so it is frequency (not phase) modulation. */
        float hz1m = v->fm_on ? v->hz1 * (1.0f + v->fmk * w2) : v->hz1;
        float w1 = v->fm_on ? osc_read(s->tab->mip[v->slot1], v->ph1, fabsf(hz1m)) : osc_read_lf(s->tab->mip[v->slot1], v->ph1, v->lf1);
        v->ph1 += 128.0f * hz1m / CORE_HZ;
        if (v->ph1 < 0) v->ph1 += 128;
        if (v->ph1 >= 128) { v->ph1 -= 128; if (p->d[P_OSC2_SYNC]) v->ph2 = start_phase(p->d[P_W2_PHASE]); }
        v->ph2 += 128.0f * v->hz2 / CORE_HZ; if (v->ph2 >= 128) v->ph2 -= 128;
        float mix = (w1 * v->m1 + w2 * v->m2 + w1 * w2 * v->m3 + (v->m4 > 0 ? noise_tick(v) * v->m4 : 0.0f)) / 128.0f;
        mix = clip(mix, p->d[P_CLIP]);
        float fl = filter1_run(&v->flt, p->d[P_F1_TYPE], mix, v->cut, v->reso, v->spec);
        fl = filter2_run(&v->flt, p->d[P_F2_TYPE], fl, v->c2);
        float g = fl * v->aenv.level * v->gfac;
        suml += g * v->panl;
        sumr += g * v->panr;
    }
    float l = suml * OUT_GAIN, r = sumr * OUT_GAIN;
    /* Effect index order follows the manual's list for 0..9 (the firmware's real numbering is to be confirmed); others are off. */
    int fxt = fx_type_from_index(p->d[P_FX_TYPE]);
    if (fxt >= 0) fx_run(&s->fx, fxt, p->d[P_FX_P1], p->d[P_FX_P2], p->d[P_FX_P3], 120.0f, &l, &r);
    chorus_run(&s->fx, p->d[P_CHORUS], &l, &r);
    lr[0] = shelf_run(&s->shelf[0], l);
    lr[1] = shelf_run(&s->shelf[1], r);
}

/* Test hook: render the 40 kHz core directly (not exported from the plugin). */
void clementine_render40k(void *inst, float *out, int n) { for (int i = 0; i < n; i++) { float lr[2]; core(inst, lr); out[i] = lr[0]; } }

static void gen(void *p, float *lr) { core(p, lr); }

/* Flush denormals to zero while rendering: on the 32-bit ARM devices VFP denormal arithmetic is very slow (filters and delay lines decay into them). */
#if defined(__arm__) && !defined(__SOFTFP__)
static unsigned fz_on(void) { unsigned old; __asm__ volatile("vmrs %0, fpscr" : "=r"(old)); unsigned n = old | (1u << 24); __asm__ volatile("vmsr fpscr, %0" : : "r"(n)); return old; }
static void fz_restore(unsigned old) { __asm__ volatile("vmsr fpscr, %0" : : "r"(old)); }
#elif defined(__SSE__)
#include <xmmintrin.h>
static unsigned fz_on(void) { unsigned old = _mm_getcsr(); _mm_setcsr(old | 0x8040); return old; }
static void fz_restore(unsigned old) { _mm_setcsr(old); }
#else
static unsigned fz_on(void) { return 0; }
static void fz_restore(unsigned old) { (void)old; }
#endif

static void render(void *p, int16_t *out, int frames) {
    inst_t *s = p;
    unsigned fpsave = fz_on();
    float buf[256];
    for (int done = 0; done < frames; ) {
        int n = frames - done < 128 ? frames - done : 128;
        rs_render(&s->rs, gen, s, buf, n);
        for (int i = 0; i < 2 * n; i++) out[2 * done + i] = (int16_t)fmaxf(-32767, fminf(32767, buf[i] * 32767));
        done += n;
    }
    fz_restore(fpsave);
}

static const mpc_engine_t E = { create, destroy, midi, set_param, get_param, render, NULL };
const mpc_engine_t *mpc_engine(void) { return &E; }
