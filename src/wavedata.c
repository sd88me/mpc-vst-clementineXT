#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wavedata.h"

static FILE *open_in(const char *dir, const char *name) {
    char p[1024];
    snprintf(p, sizeof p, "%s/wavedata/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (f) return f;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return fopen(p, "rb");
}

wavedata_t *wavedata_load(const char *dir) {
    if (!dir) return NULL;
    wavedata_t *w = calloc(1, sizeof *w);
    static const char *const wf[] = { "waves.bin", "waves2.bin" };
    for (int k = 0; k < 2; k++) {
        FILE *f = open_in(dir, wf[k]);
        if (!f) continue;
        uint16_t idx; int8_t buf[64];
        while (fread(&idx, 2, 1, f) == 1 && fread(buf, 1, 64, f) == 64)
            if (idx < WD_WAVES) { memcpy(w->waves[idx].half, buf, 64); if (!w->have[idx]) w->nwaves++; w->have[idx] = 1; }
        fclose(f);
    }
    FILE *f = open_in(dir, "tables.bin");
    uint16_t ti, ent[64];
    while (f && fread(&ti, 2, 1, f) == 1 && fread(ent, 2, 64, f) == 64) {
        if (ti >= WD_TABLES) continue;
        table_ctl_t c;
        for (int i = 0; i < 64; i++) c.slot[i] = ent[i] < WD_WAVES && w->have[ent[i]] ? (int16_t)ent[i] : TABLE_EMPTY;   /* >1249 or 0xFFFF = empty */
        if (c.slot[0] == TABLE_EMPTY) continue;   /* the first slot must be valid; otherwise the table has no usable data */
        w->built[ti] = malloc(sizeof(table_t));
        table_build(&c, w->waves, WD_WAVES, w->built[ti]);
        w->from_rom[ti] = 1; w->ntables++;
    }
    if (f) fclose(f);
    if (!w->nwaves && !w->ntables) { free(w); return NULL; }
    return w;
}

void wavedata_free(wavedata_t *w) {
    if (!w) return;
    for (int i = 0; i < WD_TABLES; i++) free(w->built[i]);
    free(w);
}

const table_t *wavedata_table(wavedata_t *w, int n) {
    if (n < 0 || n >= WD_TABLES) n = 0;
    if (w->built[n]) return w->built[n];
    /* algorithmic tables (28-51) have no control table, and unloaded data leaves gaps: stand in with an open table so a sound
     * still plays. The real algorithmic tables come from the firmware's memory (docs/DESIGN.md). */
    static wave_t ow[TABLE_SLOTS]; table_ctl_t c;
    open_table(n % OPEN_TABLES, ow, &c, NULL);
    w->built[n] = malloc(sizeof(table_t));
    table_build(&c, ow, TABLE_SLOTS, w->built[n]);
    return w->built[n];
}
