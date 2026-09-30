#include <stdio.h>
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
        CHECK(t.mip[62][0] == 127 && t.mip[62][WAVE_LEN - 1] == -127, "  slot 62 is the square");
        CHECK(t.mip[63][0] < -120 && t.mip[63][WAVE_LEN - 1] > 120, "  slot 63 is the saw");
    }
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
