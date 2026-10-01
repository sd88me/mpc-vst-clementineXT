#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "wavedata.h"

static FILE *open_in(const char *dir, const char *name) {
    char p[1024];
    snprintf(p, sizeof p, "%s/wavedata/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (f) return f;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return fopen(p, "rb");
}

/* ---- import from the user's own ROM dump ----
 * The 256 KB ROM is two 128 KB chips interleaved byte by byte (chip A = even bytes, starts C0 00; chip B = odd bytes, starts DE 00), or a
 * combined image starting C0 DE. Layout found by matching the firmware's own wave and table dumps (test/test_rom.c):
 *   waves (64 samples each, ^ 0x80, the first half of the cycle):
 *     0..191   chip A at 0x11000 + 64n      192..306  chip B at 0xE000 + 64n
 *     368..505 chip B at 0xC180 + 64n       (307..367 hold no waves; the tables use 0..245 and 368..421, 300 in all)
 *   control tables (64 big-endian 16-bit wave numbers, 0xFFFF = empty):
 *     0..27    chip A at 0x10000 + 128n     52..63    chip B at 0x10000 + 128(n-52)
 *   28..51 are algorithmic (no control table); the user tables 96..127 hold no ROM data. */
#define ROM_WAVES 506

static const uint8_t *rom_wave(const uint8_t *A, const uint8_t *B, int n) {
    if (n < 192) return A ? A + 0x11000 + 64 * n : NULL;
    if (n < 307) return B ? B + 0xE000 + 64 * n : NULL;
    if (n < 368) return NULL;   /* 307..367 are not waves (the firmware returns code bytes for them); no table uses them */
    return B ? B + 0xC180 + 64 * n : NULL;
}

/* Table 44 is not stored as a table: the firmware builds it from raw ROM bytes (found by matching its output against the ROM, no mismatches in
 * all 61 slots). Slot s is 4+s samples of a smooth ramp (chip B at 0x1322B, negated) followed by the combined image (chip A even, chip B odd bytes) from
 * 0xF3CC, 64 samples in all. Read from the user's ROM only; nothing of it is stored here. */
static int8_t rom_s8(uint8_t b) { return (int8_t)(b ^ 0x80); }
static void rom_table44(wavedata_t *w, const uint8_t *A, const uint8_t *B) {
    if (!A || !B || w->built[44]) return;
    static wave_t tw[TABLE_SLOTS];
    table_ctl_t c;
    for (int s = 0; s < 61; s++) {
        for (int k = 0; k < 64; k++) {
            int v;
            if (k < 4 + s) { v = -rom_s8(B[0x1322B + k]); if (v > 127) v = 127; }
            else { unsigned o = 0xF3CCu + (unsigned)(k - (4 + s)); v = rom_s8((o & 1) ? B[o >> 1] : A[o >> 1]); }
            tw[s].half[k] = (int8_t)v;
        }
        c.slot[s] = (int16_t)s;
    }
    for (int s = 61; s < TABLE_SLOTS; s++) c.slot[s] = TABLE_EMPTY;
    w->built[44] = malloc(sizeof(table_t));
    table_build(&c, tw, TABLE_SLOTS, w->built[44]);
    w->from_rom[44] = 1; w->ntables++;
}

static int rom_extract(wavedata_t *w, const uint8_t *A, const uint8_t *B) {
    if (!A) return 0;
    for (int n = 0; n < ROM_WAVES; n++) {
        const uint8_t *p = rom_wave(A, B, n);
        if (!p) continue;
        for (int i = 0; i < 64; i++) w->waves[n].half[i] = (int8_t)(p[i] ^ 0x80);
        w->have[n] = 1; w->nwaves++;
    }
    for (int t = 0; t < WD_TABLES; t++) {
        const uint8_t *e = t < 28 ? A + 0x10000 + 128 * t : (t >= 52 && t < 64 && B) ? B + 0x10000 + 128 * (t - 52) : NULL;
        if (!e) continue;
        table_ctl_t c;
        for (int k = 0; k < 64; k++) {
            unsigned v = (unsigned)e[2 * k] << 8 | e[2 * k + 1];
            c.slot[k] = v < ROM_WAVES && w->have[v] ? (int16_t)v : TABLE_EMPTY;   /* 0xFFFF empty; anything else is not a ROM wave */
        }
        if (c.slot[0] == TABLE_EMPTY) continue;
        w->built[t] = malloc(sizeof(table_t));
        table_build(&c, w->waves, WD_WAVES, w->built[t]);
        w->from_rom[t] = 1; w->ntables++;
    }
    rom_table44(w, A, B);
    return w->nwaves > 0;
}

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > (4 << 20)) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)len);
    if (b && fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
    fclose(f);
    *n = (size_t)len;
    return b;
}

/* Look for a ROM in dir/ROMS (then dir, dir/wavedata, dir/import): a 256 KB image, or the two 128 KB halves (either may be missing: the importer then
 * takes what that chip holds). Fills chip A (even bytes) and chip B (odd bytes); returns 1 if at least chip A was found. */
static int find_rom(const char *dir, uint8_t **A, uint8_t **B) {
    char sub[1024]; static const char *const subs[] = { "/ROMS", "", "/wavedata", "/import" };   /* ROMS is the folder the plugin creates for the user's own files */
    *A = *B = NULL;
    for (int k = 0; k < 4; k++) {
        snprintf(sub, sizeof sub, "%s%s", dir, subs[k]);
        DIR *d = opendir(sub);
        if (!d) continue;
        for (struct dirent *e; (e = readdir(d));) {
            size_t n = strlen(e->d_name);
            if (n < 5 || strcasecmp(e->d_name + n - 4, ".bin")) continue;
            char path[2048]; snprintf(path, sizeof path, "%s/%s", sub, e->d_name);
            size_t sz; uint8_t *b = slurp(path, &sz);
            if (!b) continue;
            if (sz == 262144 && b[0] == 0xC0 && b[1] == 0xDE) {   /* combined image: split into the two chips */
                free(*A); free(*B);
                *A = malloc(131072); *B = malloc(131072);
                for (int i = 0; i < 131072; i++) { (*A)[i] = b[2 * i]; (*B)[i] = b[2 * i + 1]; }
                free(b); closedir(d);
                return 1;
            }
            if (sz == 131072 && b[0] == 0xC0 && b[1] == 0x00 && !*A) { *A = b; continue; }   /* chip A */
            if (sz == 131072 && b[0] == 0xDE && b[1] == 0x00 && !*B) { *B = b; continue; }   /* chip B */
            free(b);
        }
        closedir(d);
    }
    return *A != NULL;
}

wavedata_t *wavedata_load(const char *dir) {
    if (dir) {   /* the user's own ROM first (docs/DESIGN.md section 10) */
        uint8_t *a, *b;
        if (find_rom(dir, &a, &b)) {
            wavedata_t *w = calloc(1, sizeof *w);
            int ok = rom_extract(w, a, b);
            free(a); free(b);
            if (ok) return w;
            free(w);
        }
    }
    return wavedata_load_cache(dir);
}

wavedata_t *wavedata_load_cache(const char *dir) {
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
    for (int i = 0; i < WD_TABLES; i++) if (w->from_rom[i]) free(w->built[i]);
    for (int i = 0; i < OPEN_TABLES; i++) free(w->open_b[i]);
    free(w);
}

void wavedata_prewarm(wavedata_t *w) { for (int n = 0; n < WD_TABLES; n++) wavedata_table(w, n); }

const table_t *wavedata_table(wavedata_t *w, int n) {
    if (n < 0 || n >= WD_TABLES) n = 0;
    if (w->built[n]) return w->built[n];
    /* algorithmic tables (28-51) have no control table, and unloaded data leaves gaps: stand in with an open table so a sound
     * still plays. Rebuilt tables are in algo_table; 44 comes from the ROM. */
    {   /* a rebuilt algorithmic table (28-51), else the open stand-in */
        static wave_t aw[TABLE_SLOTS]; table_ctl_t ac;
        if (n >= 28 && n <= 51 && !algo_table(n, aw, &ac)) {
            w->built[n] = malloc(sizeof(table_t));
            table_build(&ac, aw, TABLE_SLOTS, w->built[n]);
            w->from_rom[n] = 1;   /* owned by this entry */
            return w->built[n];
        }
    }
    int o = n % OPEN_TABLES;
    if (!w->open_b[o]) {
        static wave_t ow[TABLE_SLOTS]; table_ctl_t c;
        open_table(o, ow, &c, NULL);
        w->open_b[o] = malloc(sizeof(table_t));
        table_build(&c, ow, TABLE_SLOTS, w->open_b[o]);
    }
    w->built[n] = w->open_b[o];
    return w->built[n];
}
