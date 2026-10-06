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
    table_t *open_b[OPEN_TABLES];   /* shared stand-ins, one per open table (built[n] of a missing table points at one) */
    int nwaves, ntables;
} wavedata_t;
wavedata_t *wavedata_load(const char *dir);              /* the user's ROM (256 KB image or two halves) in dir, dir/wavedata or dir/import; else the dev cache; NULL if nothing */
uint8_t *wavedata_rom_image(const char *dir);           /* the user's ROM as one 256 KB interleaved image (malloc'd), or NULL unless both chips are there */
wavedata_t *wavedata_load_cache(const char *dir);        /* the dev cache written by tools/oracle only */
void wavedata_free(wavedata_t *w);
void wavedata_prewarm(wavedata_t *w);                    /* build every table now, so the audio thread never has to */
const table_t *wavedata_table(wavedata_t *w, int n);     /* the table, or an open-set stand-in when its data is missing */
