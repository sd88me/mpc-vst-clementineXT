#include <math.h>
#include <string.h>
#include "waves.h"

void wave_expand(const wave_t *w, int8_t out[WAVE_LEN]) {
    for (int n = 0; n < WAVE_HALF; n++) { out[n] = w->half[n]; out[WAVE_HALF + n] = (int8_t)-w->half[WAVE_HALF - 1 - n]; }
}
void wave_pack(const int8_t in[WAVE_LEN], wave_t *w) { memcpy(w->half, in, WAVE_HALF); }

void wave_rotate(const int8_t in[WAVE_LEN], int8_t out[WAVE_LEN]) {
    for (int i = 0; i < WAVE_LEN; i++) out[i] = in[(i - WAVE_ROT) & (WAVE_LEN - 1)];
}

/* Level k+1 = floor(([1 2 1] filter of level k, centred two samples back) / 4), decimated by two, with the filter run at
 * full precision from level 0 (rounding each level separately does not match the firmware). */
void wave_mips(const int8_t level0[WAVE_LEN], int8_t mip[WAVE_MIPS]) {
    memset(mip, 0, WAVE_MIPS);
    memcpy(mip, level0, WAVE_LEN);
    double cur[WAVE_LEN], next[WAVE_LEN / 2];
    for (int i = 0; i < WAVE_LEN; i++) cur[i] = level0[i];
    int8_t *dst = mip + WAVE_LEN;
    for (int n = WAVE_LEN; n >= 2; n /= 2) {
        int m = n / 2;
        for (int i = 0; i < m; i++)
            next[i] = (cur[(2 * i - 3) & (n - 1)] + 2 * cur[(2 * i - 2) & (n - 1)] + cur[(2 * i - 1) & (n - 1)]) / 4;
        for (int i = 0; i < m; i++) { dst[i] = (int8_t)floor(next[i]); cur[i] = next[i]; }
        dst += m;
    }
}

static int8_t clamp8(double x) { return (int8_t)(x > 127 ? 127 : x < -128 ? -128 : lround(x)); }

/* The fixed waves as stored (before rotation): only the first half is stored, the second half mirrors it. */
static void fixed_wave(int slot, int8_t out[WAVE_LEN]) {
    wave_t h;
    for (int i = 0; i < WAVE_HALF; i++)
        h.half[i] = (int8_t)(slot == 61 ? 3 * (i < 32 ? i : 63 - i) + 2 : slot == 62 ? 64 : 64 - i);
    wave_expand(&h, out);
}

void table_build(const table_ctl_t *ctl, const wave_t *waves, int nwaves, table_t *out) {
    int8_t lvl0[TABLE_SLOTS][WAVE_LEN];
    int filled[TABLE_SLOTS];
    for (int s = 0; s < TABLE_SLOTS; s++) {
        int8_t raw[WAVE_LEN];
        int n = ctl->slot[s];
        filled[s] = s >= 61 || (n >= 0 && n < nwaves);
        if (!filled[s]) continue;
        if (s >= 61) fixed_wave(s, raw); else wave_expand(&waves[n], raw);
        wave_rotate(raw, lvl0[s]);
    }
    for (int s = 0; s < TABLE_SLOTS; s++) {
        if (filled[s]) continue;
        int a = s - 1, b = s + 1;
        while (a >= 0 && !filled[a]) a--;
        while (b < TABLE_SLOTS && !filled[b]) b++;
        if (a < 0 && b >= TABLE_SLOTS) memset(lvl0[s], 0, WAVE_LEN);
        else if (a < 0) memcpy(lvl0[s], lvl0[b], WAVE_LEN);
        else if (b >= TABLE_SLOTS) memcpy(lvl0[s], lvl0[a], WAVE_LEN);
        else for (int i = 0; i < WAVE_LEN; i++)
            lvl0[s][i] = (int8_t)((lvl0[a][i] * (b - s) + lvl0[b][i] * (s - a)) / (b - a));   /* C division truncates toward zero, as the firmware does */
    }
    for (int s = 0; s < TABLE_SLOTS; s++) wave_mips(lvl0[s], out->mip[s]);
}

/* ---- open set ---- */
static const char *names[OPEN_TABLES] = { "Saw Harmonics", "Pulse Width", "Sync Sweep", "Formant" };

static void additive(int8_t out[WAVE_LEN], int nh, int mode) {
    double buf[WAVE_LEN] = {0}, peak = 1e-9;
    for (int h = 1; h <= nh; h++) {
        double a = mode == 0 ? 1.0 / h : mode == 1 ? (h & 1) / (double)h : 1.0 / h;
        for (int i = 0; i < WAVE_LEN; i++) buf[i] += a * sin(2 * M_PI * h * (i + 0.5) / WAVE_LEN);
    }
    for (int i = 0; i < WAVE_LEN; i++) if (fabs(buf[i]) > peak) peak = fabs(buf[i]);
    for (int i = 0; i < WAVE_LEN; i++) out[i] = clamp8(127 * buf[i] / peak);
}

int open_table(int n, wave_t *waves, table_ctl_t *ctl, const char **name) {
    if (n < 0 || n >= OPEN_TABLES) return -1;
    if (name) *name = names[n];
    for (int s = 0; s < TABLE_SLOTS; s++) ctl->slot[s] = s < 61 && s % 4 == 0 ? s : TABLE_EMPTY;   /* keyframes every 4 slots, rest interpolated */
    for (int s = 0; s < 61; s += 4) {
        int8_t f[WAVE_LEN]; double t = s / 60.0;
        switch (n) {
        case 0: additive(f, 1 + (int)(t * 31), 0); break;
        case 1: for (int i = 0; i < WAVE_LEN; i++) f[i] = (i + 0.5) / WAVE_LEN < 0.5 - 0.45 * t ? 100 : -100; break;
        case 2: for (int i = 0; i < WAVE_LEN; i++) f[i] = clamp8(127 * (2 * fmod((i + 0.5) / WAVE_LEN * (1 + 7 * t), 1.0) - 1)); break;
        default: for (int i = 0; i < WAVE_LEN; i++) { double p = (i + 0.5) / WAVE_LEN, c = 3 + 10 * t; f[i] = clamp8(127 * (sin(2 * M_PI * p) + 0.6 * sin(2 * M_PI * c * p)) / 1.6); }
        }
        /* the stored half must be antisymmetric-consistent: rebuild from the first half */
        wave_pack(f, &waves[s]);
    }
    return 0;
}
