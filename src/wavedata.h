/* The wave store: ROM waves, control tables, and the 128 built tables. Loaded from the user's own import cache, never shipped.
 * Dev cache format (written by tools/oracle): waves.bin/waves2.bin = records of u16 index + 64 int8; tables.bin = records of
 * u16 table + 64 x u16 wave numbers (0xFFFF empty). Later the on-device importer writes the same thing. */
#pragma once
#include "waves.h"
#define WD_WAVES 512
#define WD_TABLES 128
typedef struct {
    wave_t waves[WD_WAVES];
    uint8_t have[WD_WAVES];
    table_t *built[WD_TABLES];   /* NULL until built */
    int from_rom[WD_TABLES];     /* 1 when built from loaded data, 0 for the open fallback */
    int nwaves, ntables;
} wavedata_t;
wavedata_t *wavedata_load(const char *dir);              /* dir itself or dir/wavedata; NULL if nothing found */
void wavedata_free(wavedata_t *w);
const table_t *wavedata_table(wavedata_t *w, int n);     /* the table, or an open-set stand-in when its data is missing */
