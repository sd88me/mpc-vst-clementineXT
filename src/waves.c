#include <math.h>
#include <string.h>
#include "waves.h"

void wave_expand(const wave_t *w, int8_t out[WAVE_LEN]) {
    for (int n = 0; n < WAVE_HALF; n++) { out[n] = w->half[n]; int v = -w->half[WAVE_HALF - 1 - n]; out[WAVE_HALF + n] = (int8_t)(v > 127 ? 127 : v); }   /* -(-128) saturates (seen in the algorithmic tables) */
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


/* ---- algorithmic tables 28-51 ----
 * The firmware generates these with code, not from stored waves. The ones below were rebuilt from the shapes the firmware produces
 * (observed with the dev-only oracle; no firmware data is used or shipped here): the three pulse/step tables 29, 41 and 42 reproduce the
 * firmware exactly, the sine sweeps 38-40, the saw sweeps 32-34 and the decaying ramp 31 are close approximations (waveform correlation
 * 0.8-0.97). Tables 28, 30, 35-37 and 43-51 are not rebuilt yet and fall back to the open set. */
static int8_t c8(double v) { return (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v); }
static void mirror_half(int8_t w[WAVE_LEN]) { for (int i = 0; i < WAVE_HALF; i++) { int v = -w[WAVE_HALF - 1 - i]; w[WAVE_HALF + i] = (int8_t)(v > 127 ? 127 : v); } }

static void gen_sine(int8_t w[WAVE_LEN], double m) { for (int i = 0; i < WAVE_LEN; i++) w[i] = c8(floor(128.0 * sin(2 * M_PI * m * (i + 0.5) / WAVE_LEN) + 0.5)); }
static void gen_saw(int8_t w[WAVE_LEN], double m) { for (int i = 0; i < WAVE_LEN; i++) { double p = fmod(m * (i + 0.5) / WAVE_LEN, 1.0); w[i] = c8(floor(-128.0 + 256.0 * p)); } }

/* Keyframe waves every `per` slots, the slots between blended with the truncating integer rule the ROM tables use. */
static void keyframes(int8_t out[TABLE_SLOTS][WAVE_LEN], void (*gen)(int8_t *, double), double m0, double step, int per) {
    int8_t key[40][WAVE_LEN]; int nk = 61 / per + 2;
    for (int k = 0; k < nk && k < 40; k++) gen(key[k], m0 + k * step);
    for (int s = 0; s < 61; s++) {
        int a = s / per, b = a + 1, sa = a * per, sb = b * per;
        for (int i = 0; i < WAVE_LEN; i++)
            out[s][i] = (s == sa) ? key[a][i] : (int8_t)((key[a][i] * (sb - s) + key[b][i] * (s - sa)) / (sb - sa));
    }
}

int algo_table(int n, wave_t *waves, table_ctl_t *ctl) {
    static int8_t all[TABLE_SLOTS][WAVE_LEN];
    switch (n) {
    case 29: for (int s = 0; s < 61; s++) { int k = 64 - s; for (int i = 0; i < WAVE_HALF; i++) all[s][i] = i < k ? 32 : 0; mirror_half(all[s]); } break;
    case 41: for (int s = 0; s < 61; s++) { int n1 = 60 - s; for (int i = 0; i < WAVE_HALF; i++) all[s][i] = i < n1 ? 127 : -128; mirror_half(all[s]); } break;
    case 42: for (int s = 0; s < 61; s++) { int k = 60 - s; for (int i = 0; i < WAVE_HALF; i++) all[s][i] = (int8_t)(i < k ? 2 * i : -128 + 2 * (i - k)); mirror_half(all[s]); } break;
    case 38: keyframes(all, gen_sine, 1, 1, 8); break;
    case 39: keyframes(all, gen_sine, 2, 1, 4); break;
    case 40: keyframes(all, gen_sine, 4, 1, 2); break;
    case 32: keyframes(all, gen_saw, 2, 1, 30); break;
    case 33: keyframes(all, gen_saw, 2, 1, 10); break;
    case 34: for (int s = 0; s < 61; s++) gen_saw(all[s], floor(2.0 + 14.0 * s / 60.0 + 0.5)); break;
    case 31:   /* a decaying ramp whose start level and slope sweep with the slot, with an exponential-looking fall in the last 14 samples */
        for (int s = 0; s < 61; s++) {
            double init = 127.0 * (60 - s) / 60.0, slope = 1.0 - fabs(s - 30) / 30.0;
            for (int i = 0; i < WAVE_HALF; i++) {
                double v = init - slope * i; int j = i - 50;
                if (j >= 0) { int k = j / 2 + 1; v = k <= 7 ? v * (128 - (1 << k)) / 128.0 : 0; }
                all[s][i] = c8(v);
            }
            mirror_half(all[s]);
        }
        break;
    default: return -1;
    }
    for (int s = 0; s < TABLE_SLOTS; s++) ctl->slot[s] = s < 61 ? s : TABLE_EMPTY;
    for (int s = 0; s < 61; s++) wave_pack(all[s], &waves[s]);
    return 0;
}

/* ---- open set ---- */
static const char *names[OPEN_TABLES] = { "Saw Harmonics", "Pulse Width", "Sync Sweep", "Formant", "Odd Harmonics", "Wave Fold", "Soft Pulse", "Saw Pair", "Comb Saw", "Bell Partials", "Vowel Sweep", "Fuzz Morph" };

static void additive(int8_t out[WAVE_LEN], int nh, int mode) {
    double buf[WAVE_LEN] = {0}, peak = 1e-9;
    for (int h = 1; h <= nh; h++) {
        double a = mode == 0 ? 1.0 / h : mode == 1 ? (h & 1) / (double)h : 1.0 / h;
        for (int i = 0; i < WAVE_LEN; i++) buf[i] += a * sin(2 * M_PI * h * (i + 0.5) / WAVE_LEN);
    }
    for (int i = 0; i < WAVE_LEN; i++) if (fabs(buf[i]) > peak) peak = fabs(buf[i]);
    for (int i = 0; i < WAVE_LEN; i++) out[i] = clamp8(127 * buf[i] / peak);
}

static void norm(int8_t out[WAVE_LEN], const double buf[WAVE_LEN]) {
    double peak = 1e-9;
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
        case 4: { double buf[WAVE_LEN] = {0}; int nh = 1 + 2 * (int)(t * 15); for (int h = 1; h <= nh; h += 2) for (int i = 0; i < WAVE_LEN; i++) buf[i] += sin(2 * M_PI * h * (i + 0.5) / WAVE_LEN) / h; norm(f, buf); break; }
        case 5: { double buf[WAVE_LEN]; double a = 0.2 + 6.0 * t; for (int i = 0; i < WAVE_LEN; i++) buf[i] = sin(a * sin(2 * M_PI * (i + 0.5) / WAVE_LEN)); norm(f, buf); break; }
        case 6: { double buf[WAVE_LEN]; for (int i = 0; i < WAVE_LEN; i++) buf[i] = tanh(6.0 * (sin(2 * M_PI * (i + 0.5) / WAVE_LEN) - 0.9 * t)); norm(f, buf); break; }
        case 7: { double buf[WAVE_LEN] = {0}; for (int h = 1; h <= 24; h++) for (int i = 0; i < WAVE_LEN; i++) { double p = (i + 0.5) / WAVE_LEN; buf[i] += (sin(2 * M_PI * h * p) + t * sin(2 * M_PI * h * 2 * p + 1.0)) / h; } norm(f, buf); break; }
        case 8: { double buf[WAVE_LEN] = {0}; for (int h = 1; h <= 30; h++) for (int i = 0; i < WAVE_LEN; i++) buf[i] += (1.0 - cos(M_PI * h * (0.04 + 0.92 * t))) / h * sin(2 * M_PI * h * (i + 0.5) / WAVE_LEN); norm(f, buf); break; }
        case 9: { double buf[WAVE_LEN] = {0}; for (int h = 1; h <= 16; h++) for (int i = 0; i < WAVE_LEN; i++) buf[i] += exp(-h * (0.6 - 0.5 * t)) * (h % 2 ? 1.0 : 0.6 + 0.4 * t) * sin(2 * M_PI * h * (i + 0.5) / WAVE_LEN); norm(f, buf); break; }
        case 10: { double buf[WAVE_LEN] = {0}; double c[3] = { 2 + 2 * t, 5 + 4 * t, 9 + 3 * t };
                   for (int h = 1; h <= 20; h++) { double a = 0; for (int k = 0; k < 3; k++) a += exp(-(h - c[k]) * (h - c[k]) / 2.4) / (1 + k);
                                                   for (int i = 0; i < WAVE_LEN; i++) buf[i] += a * sin(2 * M_PI * h * (i + 0.5) / WAVE_LEN); }
                   norm(f, buf); break; }
        case 11: { double buf[WAVE_LEN] = {0}; uint32_t r = 2463534242u;
                   for (int h = 1; h <= 24; h++) { r = r * 1664525u + 1013904223u; double ph = (r >> 8) / 16777216.0 * 2 * M_PI, rw = pow(h, -0.7);
                                                   for (int i = 0; i < WAVE_LEN; i++) buf[i] += (h == 1 ? (1 - t) : t * rw) * sin(2 * M_PI * h * (i + 0.5) / WAVE_LEN + ph * (h == 1 ? 0 : 1)); }
                   norm(f, buf); break; }
        default: for (int i = 0; i < WAVE_LEN; i++) { double p = (i + 0.5) / WAVE_LEN, c = 3 + 10 * t; f[i] = clamp8(127 * (sin(2 * M_PI * p) + 0.6 * sin(2 * M_PI * c * p)) / 1.6); }
        }
        /* the stored half must be antisymmetric-consistent: rebuild from the first half */
        wave_pack(f, &waves[s]);
    }
    return 0;
}
