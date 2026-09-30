#include <math.h>
#include <string.h>
#include "waves.h"

void wave_expand(const wave_t *w, int8_t out[WAVE_LEN]) {
    for (int n = 0; n < WAVE_HALF; n++) { out[n] = w->half[n]; out[WAVE_HALF + n] = (int8_t)-w->half[WAVE_HALF - 1 - n]; }
}
void wave_pack(const int8_t in[WAVE_LEN], wave_t *w) { memcpy(w->half, in, WAVE_HALF); }

/* Each level halves the length by averaging pairs. Levels are laid out 128, 64, ..., 1 back to back. */
void wave_mips(const int8_t full[WAVE_LEN], int8_t mip[WAVE_MIPS]) {
    memset(mip, 0, WAVE_MIPS);
    memcpy(mip, full, WAVE_LEN);
    int8_t *src = mip, *dst = mip + WAVE_LEN;
    for (int len = WAVE_LEN / 2; len >= 1; len /= 2) {
        for (int i = 0; i < len; i++) dst[i] = (int8_t)((src[2 * i] + src[2 * i + 1]) / 2);
        src = dst; dst += len;
    }
}

static int8_t clamp8(double x) { return (int8_t)(x > 127 ? 127 : x < -127 ? -127 : lround(x)); }

static void fixed_wave(int slot, int8_t out[WAVE_LEN]) {
    for (int i = 0; i < WAVE_LEN; i++) {
        double p = (i + 0.5) / WAVE_LEN;
        out[i] = clamp8(127 * (slot == 61 ? (p < .5 ? 4 * p - 1 : 3 - 4 * p) : slot == 62 ? (p < .5 ? 1 : -1) : 2 * p - 1));
    }
}

void table_build(const table_ctl_t *ctl, const wave_t *waves, int nwaves, table_t *out) {
    int8_t full[TABLE_SLOTS][WAVE_LEN];
    int filled[TABLE_SLOTS];
    for (int s = 0; s < TABLE_SLOTS; s++) {
        if (s >= 61) { fixed_wave(s, full[s]); filled[s] = 1; continue; }
        int n = ctl->slot[s];
        filled[s] = n >= 0 && n < nwaves;
        if (filled[s]) wave_expand(&waves[n], full[s]);
    }
    for (int s = 0; s < TABLE_SLOTS; s++) {
        if (!filled[s]) {
            int a = s - 1, b = s + 1;
            while (a >= 0 && !filled[a]) a--;
            while (b < TABLE_SLOTS && !filled[b]) b++;
            if (a < 0 && b >= TABLE_SLOTS) memset(full[s], 0, WAVE_LEN);
            else if (a < 0) memcpy(full[s], full[b], WAVE_LEN);
            else if (b >= TABLE_SLOTS) memcpy(full[s], full[a], WAVE_LEN);
            else for (int i = 0; i < WAVE_LEN; i++) {
                double t = (double)(s - a) / (b - a);
                full[s][i] = clamp8((1 - t) * full[a][i] + t * full[b][i]);
            }
        }
    }
    for (int s = 0; s < TABLE_SLOTS; s++) wave_mips(full[s], out->mip[s]);
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
