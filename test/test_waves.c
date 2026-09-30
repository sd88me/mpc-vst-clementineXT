#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/waves.h"
static int fails;
#define CHECK(c, m) do { int ok_ = (c); printf("%s %s\n", ok_ ? "ok  " : "FAIL", m); fails += !ok_; } while (0)
int main(void) {
    wave_t w; int8_t f[WAVE_LEN], g[WAVE_LEN];
    for (int i = 0; i < WAVE_HALF; i++) w.half[i] = (int8_t)(i * 3 - 90);
    wave_expand(&w, f);
    int sym = 1; for (int n = 0; n < 64; n++) sym &= f[64 + n] == -f[63 - n];
    CHECK(sym, "expand: w[64+n] == -w[63-n]");
    wave_t w2; wave_pack(f, &w2); wave_expand(&w2, g); CHECK(!memcmp(f, g, WAVE_LEN), "pack/expand round trip");
    int8_t m[WAVE_MIPS]; wave_mips(f, m); CHECK(!memcmp(m, f, WAVE_LEN), "mip 0 is the wave");
    static wave_t waves[TABLE_SLOTS]; static table_ctl_t ctl; static table_t t;
    for (int n = 0; n < OPEN_TABLES; n++) {
        const char *nm; CHECK(!open_table(n, waves, &ctl, &nm), nm);
        table_build(&ctl, waves, TABLE_SLOTS, &t);
        int nz = 1; for (int s = 0; s < TABLE_SLOTS; s++) { int any = 0; for (int i = 0; i < WAVE_LEN; i++) any |= t.mip[s][i] != 0; nz &= any; }
        CHECK(nz, "  every slot non-silent");
        CHECK(t.mip[62][0] == -64 && t.mip[62][3] == 64 && t.mip[62][66] == 64 && t.mip[62][67] == -64, "  slot 62 is the square (+-64, rotated by 3)");
        CHECK(t.mip[63][0] == -62 && t.mip[63][2] == -64 && t.mip[63][3] == 64 && t.mip[63][66] == 1 && t.mip[63][67] == -1, "  slot 63 is the saw, 64 down to -64");
        CHECK(t.mip[61][0] == -8 && t.mip[61][3] == 2 && t.mip[61][34] == 95 && t.mip[61][35] == 95, "  slot 61 is the triangle, peak 95");
    }

    /* Local-only: build a real table from oracle dumps and compare with the firmware's own DSP memory (never committed).
     * CLEMENTINE_ORACLE_DIR holds waves.bin, waves2.bin, tables.bin and diff_0_1.bin.b (see tools/oracle). */
    const char *dir = getenv("CLEMENTINE_ORACLE_DIR");
    if (dir) {
        char path[512]; static wave_t all[512]; static int have[512];
        const char *wf[2] = {"waves.bin", "waves2.bin"};
        for (int k = 0; k < 2; k++) {
            snprintf(path, sizeof path, "%s/%s", dir, wf[k]); FILE *f = fopen(path, "rb"); if (!f) continue;
            uint16_t idx; int8_t buf[64];
            while (fread(&idx, 2, 1, f) == 1 && fread(buf, 1, 64, f) == 64) if (idx < 512) { memcpy(all[idx].half, buf, 64); have[idx] = 1; }
            fclose(f);
        }
        snprintf(path, sizeof path, "%s/tables.bin", dir); FILE *f = fopen(path, "rb");
        table_ctl_t c1; int got = 0; uint16_t ti, ent[64];
        while (f && fread(&ti, 2, 1, f) == 1 && fread(ent, 2, 64, f) == 64) if (ti == 1) { for (int i = 0; i < 64; i++) c1.slot[i] = ent[i] < 512 && have[ent[i]] ? (int16_t)ent[i] : TABLE_EMPTY; got = 1; }
        if (f) fclose(f);
        snprintf(path, sizeof path, "%s/diff_0_1.bin.b", dir); FILE *g = fopen(path, "rb");
        static uint32_t region[0x4000];
        if (got && g && fread(region, 4, 0x4000, g) == 0x4000) {
            static table_t built; table_build(&c1, all, 512, &built);
            long exact = 0, off1 = 0, worse = 0, key_bad = 0, lv0_bad = 0; int maxd = 0;
            for (int sl = 0; sl < 64; sl++) for (int i = 0; i < 256; i++) {
                int fw = (int8_t)((region[sl * 256 + i] >> 16) & 0xFF), d = built.mip[sl][i] - fw; if (d < 0) d = -d;
                if (d > maxd) maxd = d;
                if (!d) exact++; else if (d == 1) off1++; else { worse++; if (i < 128) lv0_bad++; }
                if (d && c1.slot[sl] != TABLE_EMPTY) key_bad++;
            }
            printf("info table 1 vs firmware: %ld exact, %ld off by 1, %ld off by more (max %d) of %d words\n", exact, off1, worse, maxd, 64 * 256);
            CHECK(maxd <= 8, "every word within 8 LSB of the firmware (the blend of empty slots is not exact yet)");
            CHECK(key_bad == 0, "keyframe slots (and fixed waves) match the firmware exactly, all mip levels");
        } else printf("skip firmware table check (oracle files not found)\n");
        if (g) fclose(g);
    } else printf("skip firmware table check (set CLEMENTINE_ORACLE_DIR)\n");
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
