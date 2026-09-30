/* Local-only: the ROM importer against the firmware's own dumps.
 *   CLEMENTINE_ROM_DIR    folder with the user's ROM (two 128 KB halves or one 256 KB image)
 *   CLEMENTINE_ORACLE_DIR folder with waves.bin, waves2.bin, tables.bin from tools/oracle */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wavedata.h"
int main(void) {
    const char *rom = getenv("CLEMENTINE_ROM_DIR"), *orc = getenv("CLEMENTINE_ORACLE_DIR");
    if (!rom || !orc) { printf("skip (set CLEMENTINE_ROM_DIR and CLEMENTINE_ORACLE_DIR)\n"); return 0; }
    int fails = 0;
#define CHECK(c, m) do { int ok_ = (c); printf("%s %s\n", ok_ ? "ok  " : "FAIL", m); fails += !ok_; } while (0)
    wavedata_t *a = wavedata_load(rom), *b = wavedata_load_cache(orc);
    CHECK(a && b, "both loaders found data");
    if (!a || !b) return 1;
    int same = 0, diff = 0, missing = 0;
    for (int n = 0; n < 506; n++) { if (n >= 307 && n < 368) continue;   /* not waves */
        if (!a->have[n] || !b->have[n]) missing++; else if (memcmp(a->waves[n].half, b->waves[n].half, 64)) diff++; else same++; }
    printf("info waves: %d identical, %d different, %d missing on one side\n", same, diff, missing);
    CHECK(same == 445 && !diff && !missing, "waves 0-306 and 368-505 (445) equal the firmware's");
    int tsame = 0, tdiff = 0, ta = 0, tb = 0;
    for (int t = 0; t < WD_TABLES; t++) {
        int ha = a->from_rom[t], hb = b->from_rom[t]; ta += ha; tb += hb;
        if (ha && hb) { if (memcmp(a->built[t], b->built[t], sizeof(table_t))) tdiff++; else tsame++; }
    }
    printf("info tables: ROM has %d control tables, firmware dump %d; %d identical, %d different\n", ta, tb, tsame, tdiff);
    CHECK(tdiff == 0 && tsame >= 40, "every ROM table that the dump also has is identical (built the same way)");
    printf("info user tables 96-127 present in the ROM image: %d\n", ta - tsame);
    wavedata_free(a); wavedata_free(b);
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
