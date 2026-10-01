/* Clementine-XT as a Schwung (Ableton Move) sound generator: plugin API v2 around the engine in ../../src.
 *
 * Schwung runs every entry point on the SPI audio callback (src/vendor/schwung/plugin_api_v1.h, "THREADING CONTRACT"): no file I/O, no
 * allocation, no blocking, no logging. The engine does all of those when it is created (it scans the ROMS folder, reads the ROM and the sound banks and
 * builds its tables) and when a bank is loaded. So:
 *   - create_instance only allocates the small wrapper and starts a worker thread (demoted to SCHED_OTHER on cores 0-2, as the contract demands);
 *   - the worker creates the engine and publishes it; until then render_block writes silence and MIDI and parameters are dropped;
 *   - slow parameter writes (the bank) go through a small lock-free queue to the worker; while one runs, program and bank writes are ignored;
 *   - destroy_instance asks the worker to tear everything down (the worker polls a flag every few milliseconds) and returns at once.
 * Everything the audio thread calls on the engine (midi, render, set_param of an ordinary parameter, get_param) is the same code the MPC build runs
 * in its audio callback, which avoids allocation and file access by design (docs/PERFORMANCE.md). */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "engine.h"
#include "plugin_api_v1.h"
#include "schwung_meta.h"

#define QSIZE 8
typedef struct { char key[24]; char val[32]; } slow_cmd_t;

typedef struct {
    const mpc_engine_t *eng;
    void *inst;                       /* the engine instance, published by the worker */
    char data_dir[512];
    atomic_int state;                 /* 0 loading, 1 ready, 2 failed */
    atomic_int closing;               /* set by destroy_instance; the worker then frees everything */
    atomic_int busy;                  /* a slow command (bank load) is running */
    slow_cmd_t q[QSIZE];
    atomic_uint qhead, qtail;         /* single producer (the audio thread), single consumer (the worker) */
    pthread_t worker;
} wrap_t;

static const host_api_v1_t *g_host;

static void sleep_ms(int ms) { struct timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }

static void *worker_main(void *arg) {
    wrap_t *w = arg;
    struct sched_param sp = { .sched_priority = 0 };   /* threads inherit the callback's SCHED_FIFO 90: demote first and keep off core 3 */
    sched_setscheduler(0, SCHED_OTHER, &sp);
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set); CPU_SET(1, &set); CPU_SET(2, &set);
    sched_setaffinity(0, sizeof set, &set);
    void *inst = w->eng->create(w->data_dir);
    if (inst) { w->inst = inst; atomic_store(&w->state, 1); } else atomic_store(&w->state, 2);
    while (!atomic_load(&w->closing)) {
        unsigned t = atomic_load(&w->qtail);
        if (t != atomic_load(&w->qhead)) {
            slow_cmd_t c = w->q[t % QSIZE];
            atomic_store(&w->busy, 1);
            if (inst) w->eng->set_param(inst, c.key, c.val);
            atomic_store(&w->busy, 0);
            atomic_store(&w->qtail, t + 1);
        } else sleep_ms(4);
    }
    if (inst) w->eng->destroy(inst);
    free(w);
    return NULL;
}

static void *sw_create(const char *module_dir, const char *json_defaults) {
    (void)module_dir; (void)json_defaults;
    wrap_t *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->eng = mpc_engine();
    const char *d = getenv("CLEMENTINE_XT_DATA");   /* the user's ROMs and banks live outside the module folder so module updates keep them */
    snprintf(w->data_dir, sizeof w->data_dir, "%s", d && *d ? d : "/data/UserData/schwung/clementine-xt");
    if (pthread_create(&w->worker, NULL, worker_main, w)) { free(w); return NULL; }
    pthread_detach(w->worker);
    return w;
}

static void sw_destroy(void *p) { if (p) atomic_store(&((wrap_t *)p)->closing, 1); }   /* the worker owns the teardown */

static int ready(wrap_t *w) { return atomic_load(&w->state) == 1; }

static void sw_midi(void *p, const uint8_t *msg, int len, int source) {
    (void)source;
    wrap_t *w = p;
    if (w && ready(w) && len >= 2) w->eng->midi(w->inst, msg, len);
}

/* enum parameters arrive as an option label or an index; the engine takes the index */
static int enum_index(const char *key, const char *val) {
    for (int i = 0; i < SCHWUNG_NENUMS; i++)
        if (!strcmp(SCHWUNG_ENUMS[i].key, key))
            for (int k = 0; k < SCHWUNG_ENUMS[i].nopt; k++)
                if (!strcmp(SCHWUNG_ENUMS[i].opt[k], val)) return k;
    return -1;
}

static void sw_set(void *p, const char *key, const char *val) {
    wrap_t *w = p;
    if (!w || !ready(w) || !key || !val) return;
    char num[16];
    char *endp; (void)strtod(val, &endp);
    if (val[0] && *endp) {   /* not a number: an option label */
        int i = enum_index(key, val);
        if (i < 0) return;
        snprintf(num, sizeof num, "%d", i); val = num;
    }
    if (!strcmp(key, "preset")) key = "program";
    if (!strcmp(key, "bank")) {
        unsigned h = atomic_load(&w->qhead);
        if (h - atomic_load(&w->qtail) >= QSIZE) return;
        slow_cmd_t *c = &w->q[h % QSIZE];
        snprintf(c->key, sizeof c->key, "%s", key); snprintf(c->val, sizeof c->val, "%s", val);
        atomic_store(&w->qhead, h + 1);
        atomic_store(&w->busy, 1);   /* until the worker has taken it */
        return;
    }
    if (atomic_load(&w->busy) && !strcmp(key, "program")) return;   /* a bank is loading: the program numbers are about to change */
    w->eng->set_param(w->inst, key, val);
}

/* the bank names as the picker's list: [{"index":0,"label":"..."},...] */
static int bank_list(wrap_t *w, char *buf, int n) {
    int o = snprintf(buf, (size_t)n, "[");
    for (int i = 1; i <= 12 && o < n - 64; i++) {
        char name[64] = "";
        char k[24]; snprintf(k, sizeof k, "bank_slot_%d", i);
        if (w->eng->get_param(w->inst, k, name, sizeof name) <= 0 || !name[0]) break;
        const char *s = name[0] == '>' && name[1] == ' ' ? name + 2 : name;
        for (char *c = name; *c; c++) if (*c == '"' || *c == '\\') *c = '\'';
        o += snprintf(buf + o, (size_t)(n - o), "%s{\"index\":%d,\"label\":\"%s\"}", i > 1 ? "," : "", i - 1, s);
    }
    return o + snprintf(buf + o, (size_t)(n - o), "]");
}

static int copy_str(char *buf, int n, const char *s) {
    int len = (int)strlen(s);
    if (len >= n) return -1;
    memcpy(buf, s, (size_t)len + 1);
    return len;
}

static int sw_get(void *p, const char *key, char *buf, int n) {
    wrap_t *w = p;
    if (!w || !key || !buf || n < 2) return -1;
    if (!strcmp(key, "chain_params")) return copy_str(buf, n, SCHWUNG_CHAIN_PARAMS);
    if (!strcmp(key, "ui_hierarchy")) return copy_str(buf, n, SCHWUNG_UI_HIERARCHY);
    if (!strcmp(key, "module_id")) return copy_str(buf, n, "clementine-xt");
    if (!strcmp(key, "ready")) return snprintf(buf, (size_t)n, "%d", atomic_load(&w->state));
    if (!ready(w)) {
        if (!strcmp(key, "preset_name")) return copy_str(buf, n, atomic_load(&w->state) == 2 ? "(load failed)" : "(loading)");
        return -1;
    }
    if (!strcmp(key, "preset_count")) return snprintf(buf, (size_t)n, "256");
    if (!strcmp(key, "preset_name")) key = "patch_name";
    else if (!strcmp(key, "preset")) key = "program";
    else if (!strcmp(key, "bank_list")) return bank_list(w, buf, n);
    int r = w->eng->get_param(w->inst, key, buf, n);
    return r > 0 ? r : -1;
}

static int sw_error(void *p, char *buf, int n) {
    wrap_t *w = p;
    if (w && atomic_load(&w->state) == 2 && n > 8) return copy_str(buf, n, "Clementine-XT could not start");
    return 0;
}

static void sw_render(void *p, int16_t *out, int frames) {
    wrap_t *w = p;
    if (!w || !ready(w)) { memset(out, 0, (size_t)frames * 4); return; }
    w->eng->render(w->inst, out, frames);
}

static plugin_api_v2_t api = {
    .api_version = MOVE_PLUGIN_API_VERSION_2,
    .create_instance = sw_create, .destroy_instance = sw_destroy, .on_midi = sw_midi,
    .set_param = sw_set, .get_param = sw_get, .get_error = sw_error, .render_block = sw_render,
};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) { g_host = host; (void)g_host; return &api; }
